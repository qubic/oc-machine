// OC wire-message definitions — the format an OC machine receives from a Qubic Core node
// over the private out-of-band channel.
//
// SOURCE-OF-TRUTH SPLIT:
//   - Shared protocol primitives (RequestResponseHeader, NetworkMessageType, common_def
//     constants, FourQ/K12 for signature verification) live UPSTREAM in the
//     submodules/qubic_core git submodule (github.com/qubic/core). Prefer those when
//     wiring real functionality (e.g. signature verification).
//   - The OC-specific structs below (OcMachineInvocation, SignerEntry, the type value 192)
//     are NOT yet merged upstream, so they are vendored here, mirroring
//     Qubic Core's src/oc_core/core_oc_network_messages.h byte-for-byte.
//
// REMOVE-WHEN-UPSTREAM: once the OC mechanism is part of qubic/core, these structs exist in
// the submodule. Delete the vendored copies below and include the submodule header instead so
// they are defined in exactly one place (config below stays — it is OC-machine specific).
//
// This header is intentionally kept self-contained (cstdint only) so the scaffold builds
// without depending on the submodule's include paths. The static_asserts guard the sizes.

#pragma once

#include <cstdint>

namespace oc_common
{

// Network message type for an OcMachineInvocation (Qubic Core network_message_type.h).
inline constexpr std::uint8_t OC_MACHINE_INVOCATION_TYPE = 192;

// Protocol constants (Qubic Core network_messages/common_def.h).
inline constexpr std::uint32_t NUMBER_OF_COMPUTORS = 676;
inline constexpr std::uint32_t QUORUM = NUMBER_OF_COMPUTORS * 2 / 3 + 1; // 451
inline constexpr std::uint32_t SIGNATURE_SIZE = 64;
inline constexpr std::uint32_t MAX_OC_REQUEST_SIZE = 1024 - 16;          // mirrors MAX_INPUT_SIZE - 16 = 1008

#pragma pack(push, 1)

// The fixed 8-byte framing header that prefixes every message on the Qubic wire
// (Qubic Core src/network_messages/header.h). Size is a 24-bit little-endian field.
struct RequestResponseHeader
{
    std::uint8_t _size[3];
    std::uint8_t _type;
    std::uint32_t _dejavu;

    std::uint32_t size() const
    {
        return (std::uint32_t)_size[0] | ((std::uint32_t)_size[1] << 8) | ((std::uint32_t)_size[2] << 16);
    }

    std::uint8_t type() const
    {
        return _type;
    }

    std::uint32_t dejavu() const
    {
        return _dejavu;
    }
};

static_assert(sizeof(RequestResponseHeader) == 8, "RequestResponseHeader must be 8 bytes.");

// Single (computorIndex, signature) pair in an authorization bundle.
struct SignerEntry
{
    std::uint16_t computorIndex;            // index into the issuing epoch's computor list
    std::uint8_t signature[SIGNATURE_SIZE]; // SchnorrQ over the canonical auth message
};

static_assert(sizeof(SignerEntry) == 66, "SignerEntry must be exactly 66 bytes.");

// Fixed header of an OcMachineInvocation. Followed on the wire by:
//   std::uint8_t requestData[requestSize]
//   SignerEntry  signers[signatureCount]   (signatureCount == QUORUM)
struct OcMachineInvocation
{
    std::int64_t invocationId;       // 8 bytes
    std::uint16_t epoch;             // 2 bytes
    std::uint16_t interfaceIndex;    // 2 bytes
    std::uint16_t requestSize;       // 2 bytes
    std::uint16_t signatureCount;    // 2 bytes; MUST equal QUORUM
};

static_assert(sizeof(OcMachineInvocation) == 16, "OcMachineInvocation header must be exactly 16 bytes.");

#pragma pack(pop)

// Maximum bytes of an OcMachineInvocation message body (excludes the 8-byte framing header):
//   sizeof(OcMachineInvocation) + MAX_OC_REQUEST_SIZE + QUORUM * sizeof(SignerEntry)
inline constexpr std::uint32_t MAX_OC_MACHINE_INVOCATION_BODY_SIZE =
    sizeof(OcMachineInvocation) + MAX_OC_REQUEST_SIZE + QUORUM * sizeof(SignerEntry);

static_assert(MAX_OC_MACHINE_INVOCATION_BODY_SIZE == 16 + 1008 + 451 * 66,
              "Unexpected maximum OcMachineInvocation body size (≈ 30,790 bytes).");

} // namespace oc_common
