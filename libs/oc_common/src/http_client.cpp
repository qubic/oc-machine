#include "oc_common/http_client.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace oc_common
{

namespace
{

constexpr long TIMEOUT_SECONDS = 10;

HttpPostResult fail(std::string error)
{
    HttpPostResult r;
    r.error = std::move(error);
    return r;
}

bool sendAll(int fd, const void* data, std::size_t len)
{
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::size_t sent = 0;
    while (sent < len)
    {
        const ssize_t n = ::send(fd, p + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace

HttpPostResult httpPost(const std::string& host, std::uint16_t port, const std::string& path,
                        const std::uint8_t* body, std::size_t bodySize,
                        const std::vector<std::string>& extraHeaders)
{
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (rc != 0 || result == nullptr)
    {
        return fail("cannot resolve " + host + ": " + ::gai_strerror(rc));
    }

    const int fd = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0)
    {
        ::freeaddrinfo(result);
        return fail(std::string("socket() failed: ") + std::strerror(errno));
    }

    const timeval timeout{TIMEOUT_SECONDS, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    const int connected = ::connect(fd, result->ai_addr, result->ai_addrlen);
    ::freeaddrinfo(result);
    if (connected < 0)
    {
        ::close(fd);
        return fail("connect() to " + host + ":" + std::to_string(port) +
                    " failed: " + std::strerror(errno));
    }

    std::string request = "POST " + path + " HTTP/1.1\r\n" +
                          "Host: " + host + "\r\n" +
                          "Content-Type: application/octet-stream\r\n" +
                          "Content-Length: " + std::to_string(bodySize) + "\r\n" +
                          "Connection: close\r\n";
    for (const auto& header : extraHeaders)
    {
        request += header + "\r\n";
    }
    request += "\r\n";

    if (!sendAll(fd, request.data(), request.size()) || !sendAll(fd, body, bodySize))
    {
        ::close(fd);
        return fail(std::string("send failed: ") + std::strerror(errno));
    }

    // Only the status line matters to the caller; read until it is complete.
    std::string response;
    char buf[512];
    while (response.find("\r\n") == std::string::npos && response.size() < 4096)
    {
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            break;
        }
        response.append(buf, static_cast<std::size_t>(n));
    }
    ::close(fd);

    // Status line: "HTTP/1.1 200 OK"
    const auto space = response.find(' ');
    if (response.rfind("HTTP/", 0) != 0 || space == std::string::npos ||
        response.size() < space + 4)
    {
        return fail("no valid HTTP status line in response");
    }

    HttpPostResult res;
    res.statusCode = std::atoi(response.c_str() + space + 1);
    res.ok = res.statusCode > 0;
    if (!res.ok)
    {
        res.error = "unparsable status line";
    }
    return res;
}

} // namespace oc_common
