#include "mock/mock_oc_service.h"

#include "oc_common/http_client.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

namespace oc_interfaces::mock
{

MockOcService::MockOcService(std::string sinkPath, std::string forwardHost,
                             std::uint16_t forwardPort, std::string machineId)
    : _sinkPath(std::move(sinkPath)),
      _forwardHost(std::move(forwardHost)),
      _forwardPort(forwardPort),
      _machineId(std::move(machineId))
{
}

bool MockOcService::handle(const AuthorizedInvocation& invocation)
{
    if (invocation.interfaceIndex != kInterfaceIndex)
    {
        std::cerr << "MockOcService: interfaceIndex mismatch\n";
        return false;
    }
    if (invocation.requestSize != sizeof(MockOcRequest) || invocation.requestData == nullptr)
    {
        std::cerr << "MockOcService: unexpected requestSize " << invocation.requestSize << "\n";
        return false;
    }

    MockOcRequest req{};
    std::memcpy(&req, invocation.requestData, sizeof(req));

    // Idempotency note: a production handler would dedup on invocation.invocationId before
    // applying any external effect. For the mock sink we simply append; tests key on the value.
    std::ofstream sink(_sinkPath, std::ios::app);
    if (!sink)
    {
        std::cerr << "MockOcService: cannot open sink " << _sinkPath << "\n";
        return false;
    }
    sink << invocation.invocationId << ' ' << req.value << '\n';

    std::cout << "MockOcService: wrote value " << req.value << " (invocationId "
              << invocation.invocationId << ") to " << _sinkPath << "\n";

    if (!_forwardHost.empty())
    {
        return forwardToMockService(invocation);
    }
    return true;
}

bool MockOcService::forwardToMockService(const AuthorizedInvocation& invocation)
{
    if (invocation.rawBody == nullptr || invocation.rawBodySize == 0)
    {
        std::cerr << "MockOcService: no raw body to forward\n";
        return false;
    }

    std::vector<std::string> headers;
    if (!_machineId.empty())
    {
        headers.push_back("X-OC-Machine-Id: " + _machineId);
    }

    // Forward the body bytes verbatim; the service re-verifies the 451 signatures itself.
    // Best-effort: no retry — every Core node pushes the same bundle to every OC machine,
    // so the service's replication across machines covers a lost delivery.
    const oc_common::HttpPostResult result = oc_common::httpPost(
        _forwardHost, _forwardPort, "/ingest", invocation.rawBody, invocation.rawBodySize, headers);

    if (!result.ok)
    {
        std::cerr << "MockOcService: forward of invocationId " << invocation.invocationId
                  << " to " << _forwardHost << ":" << _forwardPort << " failed: " << result.error
                  << "\n";
        return false;
    }
    if (result.statusCode < 200 || result.statusCode >= 300)
    {
        std::cerr << "MockOcService: mock service rejected invocationId " << invocation.invocationId
                  << " (HTTP " << result.statusCode << ")\n";
        return false;
    }

    std::cout << "MockOcService: forwarded invocationId " << invocation.invocationId << " to "
              << _forwardHost << ":" << _forwardPort << " (HTTP " << result.statusCode << ")\n";
    return true;
}

} // namespace oc_interfaces::mock
