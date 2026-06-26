// Mock OC interface handler.
//
// Counterpart to Qubic Core src/oc_interfaces/Mock.h. The Mock request is a single 64-bit value;
// the handler writes that value to a local sink (a file) so tests can confirm an authorized
// invocation reached the external side end-to-end. No real external system is touched.

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

    explicit MockOcService(std::string sinkPath);

    std::uint16_t interfaceIndex() const override
    {
        return kInterfaceIndex;
    }

    bool handle(const AuthorizedInvocation& invocation) override;

private:
    std::string _sinkPath;
};

} // namespace oc_interfaces::mock
