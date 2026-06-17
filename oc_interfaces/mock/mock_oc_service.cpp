#include "mock/mock_oc_service.h"

#include <cstring>
#include <fstream>
#include <iostream>

namespace oc_interfaces::mock
{

MockOcService::MockOcService(std::string sinkPath) : _sinkPath(std::move(sinkPath))
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
    return true;
}

} // namespace oc_interfaces::mock
