#include "node/node_connection.h"
#include "oc_common/oc_wire.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
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
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(_config.port);

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
    std::vector<std::uint8_t> buffer(8 + oc_common::MAX_OC_MACHINE_INVOCATION_BODY_SIZE);

    while (_running)
    {
        sockaddr_in peerAddr{};
        socklen_t peerLen = sizeof(peerAddr);
        const int connFd = ::accept(_listenFd, reinterpret_cast<sockaddr*>(&peerAddr), &peerLen);
        if (connFd < 0)
        {
            if (errno == EINTR)
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

        // TODO: full streaming read loop. A Core node keeps the connection open and may send
        // many OcMachineInvocation messages; each is framed by an 8-byte RequestResponseHeader
        // whose size() gives the total message length. The scaffold reads a single message.
        const ssize_t n = ::recv(connFd, buffer.data(), buffer.size(), 0);
        if (n > 0)
        {
            const HandleResult result =
                _handler.handleFramedMessage(buffer.data(), static_cast<std::uint32_t>(n));
            if (result != HandleResult::Ok)
            {
                std::cerr << "NodeConnection: message from " << ipStr
                          << " not handled (result " << static_cast<int>(result) << ")\n";
            }
        }

        ::close(connFd);
    }

    _running = false;
}

void NodeConnection::stop()
{
    _running = false;
    if (_listenFd >= 0)
    {
        ::close(_listenFd);
        _listenFd = -1;
    }
}

} // namespace node
