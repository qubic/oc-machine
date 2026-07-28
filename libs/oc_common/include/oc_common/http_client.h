// Minimal blocking HTTP/1.1 client — just enough to POST a raw byte body to the mock
// interface service. POSIX sockets, optionally wrapped in OpenSSL for https, one request per
// connection ("Connection: close"). Not a general-purpose HTTP client: no redirects, no
// chunked-response decoding, no connection reuse.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oc_common
{

struct HttpPostResult
{
    bool ok = false;        // request was sent and a status line came back
    int statusCode = 0;     // HTTP status code (e.g. 200); 0 when !ok
    std::string error;      // failure description when !ok
};

// POST `body` as application/octet-stream to http(s)://<host>:<port><path>. `host` may be an
// IPv4 address or a hostname. `extraHeaders` are raw "Name: value" lines (no CRLF).
// Blocking, with send/receive timeouts so a hung server cannot stall the caller forever.
//
// With `tls` set, the connection is wrapped in TLS: the server certificate is verified against
// the system trust store and matched against `host`, and a verification failure is an error —
// there is no opt-out. An IPv4-literal `host` therefore only works with a certificate that
// carries a matching IP SAN; use the hostname the certificate was issued for.
HttpPostResult httpPost(const std::string& host, std::uint16_t port, const std::string& path,
                        const std::uint8_t* body, std::size_t bodySize,
                        const std::vector<std::string>& extraHeaders = {}, bool tls = false);

} // namespace oc_common
