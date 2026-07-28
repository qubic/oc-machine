// Mock OC interface handler.
//
// Counterpart to Qubic Core src/oc_interfaces/Mock.h. The Mock request is a single 64-bit value;
// the handler writes that value to a local sink (a file) so tests can confirm an authorized
// invocation reached the external side end-to-end.
//
// Optionally it also forwards the RAW bundle bytes to the public mock interface service
// (POST http(s)://<host>:<port>/ingest), which re-verifies the 451 signatures and displays the
// invocation. Forwarding the exact bytes received from Core keeps the signatures verifiable —
// no re-encoding in the trust path.

#pragma once

#include "core/base_oc_service.h"

#include <cstdint>
#include <string>

namespace oc_interfaces::mock
{

// Mirrors Qubic Core OCI::Mock::OcRequest.
#pragma pack(push, 1)
struct MockOcRequest
{
    std::uint64_t value;
};
#pragma pack(pop)

static_assert(sizeof(MockOcRequest) == 8, "MockOcRequest must match Qubic Core OCI::Mock::OcRequest (8 bytes).");

class MockOcService : public BaseOcService
{
public:
    static constexpr std::uint16_t kInterfaceIndex = 0;

    // forwardHost empty = local sink only. forwardTls selects https (certificate verified against
    // the system trust store). machineId is sent as X-OC-Machine-Id so the service can count
    // distinct reporting machines; empty = the service falls back to the sender's IP.
    explicit MockOcService(std::string sinkPath, std::string forwardHost = {},
                           std::uint16_t forwardPort = 80, bool forwardTls = false,
                           std::string machineId = {});

    std::uint16_t interfaceIndex() const override
    {
        return kInterfaceIndex;
    }

    bool handle(const AuthorizedInvocation& invocation) override;

private:
    bool forwardToMockService(const AuthorizedInvocation& invocation);

    std::string _sinkPath;
    std::string _forwardHost;
    std::uint16_t _forwardPort;
    bool _forwardTls;
    std::string _machineId;
};

} // namespace oc_interfaces::mock
