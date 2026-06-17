// Parses an incoming OcMachineInvocation message and dispatches it to the configured
// interface handler. This is the testable core of the node: given the raw bytes of a
// message body, it validates the framing and hands a decoded AuthorizedInvocation to the
// handler.

#pragma once

#include "core/base_oc_service.h"
#include "oc_common/config.h"
#include "oc_common/oc_wire.h"

#include <cstdint>

namespace node
{

enum class HandleResult
{
    Ok,
    TooShort,         // buffer smaller than the fixed header
    WrongType,        // not an OcMachineInvocation
    BadSignatureCount,// signatureCount != QUORUM
    SizeMismatch,     // declared sizes don't match the buffer length
    NoHandler,        // no handler for this interfaceIndex
    HandlerFailed,    // handler returned false
    SignatureInvalid, // signature verification failed (when enabled)
};

class RequestHandler
{
public:
    RequestHandler(const oc_common::Config& config, oc_interfaces::BaseOcService* handler);

    // Process one OcMachineInvocation message BODY (i.e. starting at the OcMachineInvocation
    // header, after the 8-byte RequestResponseHeader has been stripped).
    HandleResult handleInvocationBody(const std::uint8_t* body, std::uint32_t bodySize);

    // Process a full framed message (RequestResponseHeader + body). Checks the header type
    // and size, then delegates to handleInvocationBody.
    HandleResult handleFramedMessage(const std::uint8_t* message, std::uint32_t messageSize);

private:
    const oc_common::Config& _config;
    oc_interfaces::BaseOcService* _handler;
};

} // namespace node
