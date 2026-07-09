// Abstract base for an OC interface handler.
//
// An OC machine serves exactly one interface (selected by interfaceIndex in config). When a
// fully-authorized OcMachineInvocation arrives, the node hands the pinned request bytes and
// the signer bundle to the matching handler, which performs the external-system action.
//
// Mirrors the OM machine's BaseOracleService, but the direction is reversed: there is no
// reply to produce — the handler acts and returns success/failure for logging only.

#pragma once

#include "oc_common/oc_wire.h"

#include <cstdint>

namespace oc_interfaces
{

// The decoded, authorized invocation passed to a handler.
struct AuthorizedInvocation
{
    std::int64_t invocationId;
    std::uint16_t epoch;
    std::uint16_t interfaceIndex;

    // Pinned OcRequest bytes (interface-specific layout; length == requestSize).
    const std::uint8_t* requestData;
    std::uint16_t requestSize;

    // Authorization bundle: signatureCount (== QUORUM) signer entries.
    const oc_common::SignerEntry* signers;
    std::uint16_t signatureCount;

    // The complete raw message body exactly as received from Core (OcMachineInvocation header +
    // request bytes + signer entries; the 8-byte framing header is NOT included). Handlers that
    // forward the bundle to an external verifier MUST forward these bytes verbatim — any
    // re-encoding risks breaking the signatures.
    const std::uint8_t* rawBody;
    std::uint32_t rawBodySize;
};

class BaseOcService
{
public:
    virtual ~BaseOcService() = default;

    // Interface index this handler serves; must match the invocation's interfaceIndex.
    virtual std::uint16_t interfaceIndex() const = 0;

    // Act on an authorized invocation. Implementations MUST be idempotent in their external
    // effect, keyed by invocation.invocationId (OC does not deduplicate at the protocol level).
    // Returns true on success (for logging/metrics only — there is no on-chain reply path).
    virtual bool handle(const AuthorizedInvocation& invocation) = 0;
};

} // namespace oc_interfaces
