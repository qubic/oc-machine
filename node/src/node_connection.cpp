#include "node/node_connection.h"
#include "oc_common/oc_wire.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace node
{

NodeConnection::NodeConnection(const oc_common::Config& config, RequestHandler& handler)
    : _config(config), _handler(handler)
{
}

bool NodeConnection::isWhitelisted(const std::string& ip) const
{
    for (const auto& allowed : _config.whitelist)
    {
        if (allowed == ip)
        {
            return true;
        }
    }
    return false;
}

bool NodeConnection::start()
{
    _listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (_listenFd < 0)
    {
        std::cerr << "NodeConnection: socket() failed: " << std::strerror(errno) << "\n";
        return false;
    }

    int opt = 1;
    ::setsockopt(_listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_config.port);
    if (_config.bindAddress.empty() || _config.bindAddress == "0.0.0.0")
    {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    else if (::inet_pton(AF_INET, _config.bindAddress.c_str(), &addr.sin_addr) != 1)
    {
        std::cerr << "NodeConnection: invalid bind address " << _config.bindAddress << "\n";
        ::close(_listenFd);
        _listenFd = -1;
        return false;
    }

    if (::bind(_listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "NodeConnection: bind() failed on port " << _config.port << ": "
                  << std::strerror(errno) << "\n";
        ::close(_listenFd);
        _listenFd = -1;
        return false;
    }

    if (::listen(_listenFd, 16) < 0)
    {
        std::cerr << "NodeConnection: listen() failed: " << std::strerror(errno) << "\n";
        ::close(_listenFd);
        _listenFd = -1;
        return false;
    }

    std::cout << "NodeConnection: listening on port " << _config.port << "\n";
    return true;
}

void NodeConnection::run()
{
    if (_listenFd < 0)
    {
        std::cerr << "NodeConnection: run() called before successful start()\n";
        return;
    }

    _running = true;

    while (_running)
    {
        // poll() with a timeout so stop() (which clears _running and closes _listenFd) is
        // noticed promptly instead of blocking indefinitely in accept().
        pollfd pfd{};
        pfd.fd = _listenFd;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, 500);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break; // listen fd closed by stop(), or fatal error
        }
        if (ready == 0)
        {
            continue; // timeout; re-check _running
        }

        sockaddr_in peerAddr{};
        socklen_t peerLen = sizeof(peerAddr);
        const int connFd = ::accept(_listenFd, reinterpret_cast<sockaddr*>(&peerAddr), &peerLen);
        if (connFd < 0)
        {
            if (errno == EINTR || !_running)
            {
                continue;
            }
            std::cerr << "NodeConnection: accept() failed: " << std::strerror(errno) << "\n";
            break;
        }

        char ipStr[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &peerAddr.sin_addr, ipStr, sizeof(ipStr));
        if (!isWhitelisted(ipStr))
        {
            std::cerr << "NodeConnection: rejecting non-whitelisted peer " << ipStr << "\n";
            ::close(connFd);
            continue;
        }

        std::cout << "NodeConnection: accepted Core node " << ipStr << "\n";

        // Serve this Core node on its own thread so other Core nodes can connect concurrently;
        // the Core keeps each connection open persistently.
        std::lock_guard<std::mutex> lock(_connectionThreadsMutex);
        _connectionThreads.emplace_back([this, connFd, ip = std::string(ipStr)]() {
            serveConnection(connFd, ip.c_str());
            ::close(connFd);
            std::cout << "NodeConnection: connection from " << ip << " closed\n";
        });
    }

    _running = false;
}

namespace
{

// Read exactly `len` bytes into `dst`, looping over partial reads. Returns true on success,
// false on peer close (0) or error.
bool recvAll(int fd, std::uint8_t* dst, std::uint32_t len)
{
    std::uint32_t got = 0;
    while (got < len)
    {
        const ssize_t n = ::recv(fd, dst + got, len - got, 0);
        if (n == 0)
        {
            return false; // peer closed
        }
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false; // error
        }
        got += static_cast<std::uint32_t>(n);
    }
    return true;
}

} // namespace

void NodeConnection::serveConnection(int connFd, const char* peerIp)
{
    // A Core node keeps the connection open and streams framed messages. Each is an 8-byte
    // RequestResponseHeader whose size() gives the TOTAL message length (header + body),
    // followed by size()-8 body bytes.
    std::vector<std::uint8_t> buffer(sizeof(oc_common::RequestResponseHeader) +
                                     oc_common::MAX_OC_MACHINE_INVOCATION_BODY_SIZE);

    while (_running)
    {
        // 1. Read the fixed framing header.
        if (!recvAll(connFd, buffer.data(), sizeof(oc_common::RequestResponseHeader)))
        {
            return; // peer closed or error
        }

        const auto* header = reinterpret_cast<const oc_common::RequestResponseHeader*>(buffer.data());
        const std::uint32_t totalSize = header->size();

        // 2. Validate the declared total size against framing + capacity. A size smaller than the
        //    framing header, or larger than our buffer, means the stream is desynced/corrupt; this
        //    is unrecoverable, so drop the connection (mirrors the OM machine).
        if (totalSize < sizeof(oc_common::RequestResponseHeader) || totalSize > buffer.size())
        {
            std::cerr << "NodeConnection: " << peerIp << " sent bad message size " << totalSize
                      << "; dropping connection\n";
            return;
        }

        // 3. Read the remaining body bytes into the buffer for EVERY message — the Core node
        //    multiplexes all outgoing peer traffic onto this connection (tick data, peer gossip,
        //    etc.), so we must consume each message in full to keep the stream in sync.
        const std::uint32_t bodySize = totalSize - sizeof(oc_common::RequestResponseHeader);
        if (bodySize > 0 &&
            !recvAll(connFd, buffer.data() + sizeof(oc_common::RequestResponseHeader), bodySize))
        {
            return; // peer closed mid-message
        }

        // 4. Only act on our message type; skip everything else (mirrors the OM machine, which
        //    consumes the body then `continue`s on a type mismatch).
        if (header->type() != oc_common::OC_MACHINE_INVOCATION_TYPE)
        {
            continue;
        }

        // Serialize the dispatch: several Core nodes may be served concurrently, but the
        // external effect in the interface handler must not be run by two threads at once.
        HandleResult result;
        {
            std::lock_guard<std::mutex> lock(_dispatchMutex);
            result = _handler.handleFramedMessage(buffer.data(), totalSize);
        }
        if (result != HandleResult::Ok)
        {
            std::cerr << "NodeConnection: OcMachineInvocation from " << peerIp << " not handled (result "
                      << static_cast<int>(result) << ")\n";
        }
    }
}

void NodeConnection::stop()
{
    _running = false;
    if (_listenFd >= 0)
    {
        ::close(_listenFd); // unblocks poll()/accept() in run()
        _listenFd = -1;
    }

    // Join all per-connection worker threads. Each returns once its peer closes; closing the
    // listen fd above stops new ones from being spawned.
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(_connectionThreadsMutex);
        threads.swap(_connectionThreads);
    }
    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

} // namespace node
