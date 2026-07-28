#include "oc_common/http_client.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>

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

// Drains OpenSSL's error queue into a readable string, so a TLS failure reports why
// (expired cert, hostname mismatch, ...) instead of a bare "handshake failed".
std::string opensslError()
{
    std::string out;
    unsigned long code;
    while ((code = ::ERR_get_error()) != 0)
    {
        char buf[256];
        ::ERR_error_string_n(code, buf, sizeof(buf));
        if (!out.empty())
        {
            out += "; ";
        }
        out += buf;
    }
    return out.empty() ? "unknown TLS error" : out;
}

// Owns the socket and, for https, the TLS objects layered on top of it. Closing is handled in
// the destructor so every early return below cannot leak a descriptor.
class Connection
{
public:
    explicit Connection(int fd) : _fd(fd) {}

    ~Connection()
    {
        if (_ssl != nullptr)
        {
            ::SSL_shutdown(_ssl);
            ::SSL_free(_ssl);
        }
        if (_ctx != nullptr)
        {
            ::SSL_CTX_free(_ctx);
        }
        if (_fd >= 0)
        {
            ::close(_fd);
        }
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Wrap the connected socket in TLS, verifying the peer certificate chain against the system
    // trust store and checking it actually belongs to `host`.
    bool startTls(const std::string& host, std::string& error)
    {
        _ctx = ::SSL_CTX_new(TLS_client_method());
        if (_ctx == nullptr)
        {
            error = "SSL_CTX_new failed: " + opensslError();
            return false;
        }

        // TLS 1.2 floor: 1.0/1.1 are deprecated and offer nothing we want here.
        ::SSL_CTX_set_min_proto_version(_ctx, TLS1_2_VERSION);
        ::SSL_CTX_set_verify(_ctx, SSL_VERIFY_PEER, nullptr);
        if (::SSL_CTX_set_default_verify_paths(_ctx) != 1)
        {
            error = "cannot load system CA store: " + opensslError();
            return false;
        }

        _ssl = ::SSL_new(_ctx);
        if (_ssl == nullptr)
        {
            error = "SSL_new failed: " + opensslError();
            return false;
        }

        // SNI, so name-based virtual hosts serve the right certificate.
        ::SSL_set_tlsext_host_name(_ssl, host.c_str());
        // Hostname verification: without this the chain would validate but any valid
        // certificate from any host would be accepted.
        if (::SSL_set1_host(_ssl, host.c_str()) != 1)
        {
            error = "cannot set expected certificate host: " + opensslError();
            return false;
        }
        ::SSL_set_fd(_ssl, _fd);

        if (::SSL_connect(_ssl) != 1)
        {
            const long verifyResult = ::SSL_get_verify_result(_ssl);
            error = "TLS handshake with " + host + " failed: " + opensslError();
            if (verifyResult != X509_V_OK)
            {
                error += " (certificate verification: ";
                error += ::X509_verify_cert_error_string(verifyResult);
                error += ")";
            }
            return false;
        }
        return true;
    }

    bool sendAll(const void* data, std::size_t len)
    {
        const auto* p = static_cast<const std::uint8_t*>(data);
        std::size_t sent = 0;
        while (sent < len)
        {
            const int chunk = static_cast<int>(std::min<std::size_t>(len - sent, INT_MAX));
            const ssize_t n = _ssl != nullptr
                                  ? ::SSL_write(_ssl, p + sent, chunk)
                                  : ::send(_fd, p + sent, len - sent, MSG_NOSIGNAL);
            if (n <= 0)
            {
                if (n < 0 && _ssl == nullptr && errno == EINTR)
                {
                    continue;
                }
                return false;
            }
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    ssize_t receive(void* buf, std::size_t len)
    {
        if (_ssl != nullptr)
        {
            return ::SSL_read(_ssl, buf, static_cast<int>(len));
        }
        return ::recv(_fd, buf, len, 0);
    }

    bool isTls() const { return _ssl != nullptr; }

private:
    int _fd = -1;
    SSL_CTX* _ctx = nullptr;
    SSL* _ssl = nullptr;
};

} // namespace

HttpPostResult httpPost(const std::string& host, std::uint16_t port, const std::string& path,
                        const std::uint8_t* body, std::size_t bodySize,
                        const std::vector<std::string>& extraHeaders, bool tls)
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

    Connection conn(fd);

    const timeval timeout{TIMEOUT_SECONDS, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    const int connected = ::connect(fd, result->ai_addr, result->ai_addrlen);
    ::freeaddrinfo(result);
    if (connected < 0)
    {
        return fail("connect() to " + host + ":" + std::to_string(port) +
                    " failed: " + std::strerror(errno));
    }

    if (tls)
    {
        std::string error;
        if (!conn.startTls(host, error))
        {
            return fail(std::move(error));
        }
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

    if (!conn.sendAll(request.data(), request.size()) || !conn.sendAll(body, bodySize))
    {
        return fail(conn.isTls() ? "TLS send failed: " + opensslError()
                                 : std::string("send failed: ") + std::strerror(errno));
    }

    // Only the status line matters to the caller; read until it is complete.
    std::string response;
    char buf[512];
    while (response.find("\r\n") == std::string::npos && response.size() < 4096)
    {
        const ssize_t n = conn.receive(buf, sizeof(buf));
        if (n <= 0)
        {
            if (n < 0 && !conn.isTls() && errno == EINTR)
            {
                continue;
            }
            break;
        }
        response.append(buf, static_cast<std::size_t>(n));
    }

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
