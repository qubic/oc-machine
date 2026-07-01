#include "node/node_connection.h"
#include "oc_common/oc_wire.h"

#include <algorithm>
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

    // Honor a stop that arrived before run() started, so we don't set _running = true over it.
    if (_stopRequested.load())
    {
        return;
    }
    _running = true;

    while (_running && !_stopRequested.load())
    {
        // Poll with a timeout so stop() clearing _running is noticed without blocking in accept().
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
            break;
        }
        if (ready == 0)
        {
            continue;
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

        // Track the fd so stop() can shut it down to unblock the worker.
        {
            std::lock_guard<std::mutex> lock(_activeConnectionFdsMutex);
            _activeConnectionFds.push_back(connFd);
        }

        // Serve each connection on its own thread so multiple Core nodes can stay connected.
        cleanupFinishedThreads();
        std::lock_guard<std::mutex> lock(_connectionThreadsMutex);
        _connectionThreads.emplace_back([this, connFd, ip = std::string(ipStr)]() {
            serveConnection(connFd, ip.c_str());
            deregisterConnectionFd(connFd);
            ::close(connFd);
            std::cout << "NodeConnection: connection from " << ip << " closed\n";
            std::lock_guard<std::mutex> lock(_connectionThreadsMutex);
            _finishedThreadIds.insert(std::this_thread::get_id());
        });
    }

    _running = false;

    // Close the listen socket on this thread (not from stop()): closing it while this thread
    // polls/accepts on it would be undefined and risk fd-number reuse.
    if (_listenFd >= 0)
    {
        ::close(_listenFd);
        _listenFd = -1;
    }
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
    // Each framed message is an 8-byte header whose size() is the total length (header + body).
    std::vector<std::uint8_t> buffer(sizeof(oc_common::RequestResponseHeader) +
                                     oc_common::MAX_OC_MACHINE_INVOCATION_BODY_SIZE);

    while (_running)
    {
        if (!recvAll(connFd, buffer.data(), sizeof(oc_common::RequestResponseHeader)))
        {
            return;
        }

        const std::uint32_t totalSize =
            reinterpret_cast<const oc_common::RequestResponseHeader*>(buffer.data())->size();

        // A size below the framing header means the stream is corrupt; drop the connection.
        if (totalSize < sizeof(oc_common::RequestResponseHeader))
        {
            std::cerr << "NodeConnection: " << peerIp << " sent bad message size " << totalSize
                      << "; dropping connection\n";
            return;
        }

        // The Core multiplexes all peer traffic here (tick data, gossip), some larger than an
        // OcMachineInvocation. Grow the buffer to fit, capped to bound memory against a hostile
        // size; beyond the cap the message can't be legitimate, so drop the connection.
        constexpr std::uint32_t MAX_ALLOWED_MESSAGE_SIZE = 16 * 1024 * 1024;
        if (totalSize > MAX_ALLOWED_MESSAGE_SIZE)
        {
            std::cerr << "NodeConnection: " << peerIp << " sent message exceeding the maximum ("
                      << totalSize << " bytes); dropping connection\n";
            return;
        }
        if (totalSize > buffer.size())
        {
            buffer.resize(totalSize); // preserves the header already read
        }

        // Read the body for every message, so the stream stays in sync even for ones we skip.
        const std::uint32_t bodySize = totalSize - sizeof(oc_common::RequestResponseHeader);
        if (bodySize > 0 &&
            !recvAll(connFd, buffer.data() + sizeof(oc_common::RequestResponseHeader), bodySize))
        {
            return;
        }

        // Act only on OcMachineInvocation; skip other types. Re-read the header: a resize may
        // have reallocated the buffer.
        const std::uint8_t type =
            reinterpret_cast<const oc_common::RequestResponseHeader*>(buffer.data())->type();
        if (type != oc_common::OC_MACHINE_INVOCATION_TYPE)
        {
            continue;
        }

        // Serialize dispatch: the external effect must not run on two threads at once.
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
    // _stopRequested latches even if run() has not started yet; run() closes _listenFd on its own
    // thread once it sees this, so we don't close it here (that would race the poll()/accept()).
    _stopRequested = true;
    _running = false;

    // Shut down active connections so workers blocked in recv() return and their loops exit.
    // Each worker still closes its own fd.
    std::lock_guard<std::mutex> lock(_activeConnectionFdsMutex);
    for (int fd : _activeConnectionFds)
    {
        ::shutdown(fd, SHUT_RDWR);
    }
}

void NodeConnection::joinConnections()
{
    // Called after the run() thread is joined, so no new workers can be spawned here.
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(_connectionThreadsMutex);
        threads.swap(_connectionThreads);
        _finishedThreadIds.clear();
    }
    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
    {
        std::lock_guard<std::mutex> lock(_activeConnectionFdsMutex);
        _activeConnectionFds.clear();
    }
}

void NodeConnection::deregisterConnectionFd(int fd)
{
    std::lock_guard<std::mutex> lock(_activeConnectionFdsMutex);
    auto it = std::find(_activeConnectionFds.begin(), _activeConnectionFds.end(), fd);
    if (it != _activeConnectionFds.end())
    {
        _activeConnectionFds.erase(it);
    }
}

void NodeConnection::cleanupFinishedThreads()
{
    std::vector<std::thread> toJoin;
    {
        std::lock_guard<std::mutex> lock(_connectionThreadsMutex);
        auto it = _connectionThreads.begin();
        while (it != _connectionThreads.end())
        {
            auto finished = _finishedThreadIds.find(it->get_id());
            if (finished != _finishedThreadIds.end())
            {
                _finishedThreadIds.erase(finished);
                toJoin.push_back(std::move(*it));
                it = _connectionThreads.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    for (auto& t : toJoin)
    {
        if (t.joinable())
        {
            t.join();
        }
    }
}

} // namespace node
