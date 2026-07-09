// Minimal blocking HTTP/1.1 client — just enough to POST a raw byte body to the mock
// interface service. Plain POSIX sockets, no external dependencies, one request per
// connection ("Connection: close"). Not a general-purpose HTTP client.

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

// POST `body` as application/octet-stream to http://<host>:<port><path>. `host` may be an
// IPv4 address or a hostname. `extraHeaders` are raw "Name: value" lines (no CRLF).
// Blocking, with send/receive timeouts so a hung server cannot stall the caller forever.
HttpPostResult httpPost(const std::string& host, std::uint16_t port, const std::string& path,
                        const std::uint8_t* body, std::size_t bodySize,
                        const std::vector<std::string>& extraHeaders = {});

} // namespace oc_common
