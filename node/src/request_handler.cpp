#include "node/request_handler.h"

#include <cstring>
#include <iostream>

namespace node
{

using oc_common::OcMachineInvocation;
using oc_common::QUORUM;
using oc_common::RequestResponseHeader;
using oc_common::SignerEntry;

RequestHandler::RequestHandler(const oc_common::Config& config, oc_interfaces::BaseOcService* handler)
    : _config(config), _handler(handler)
{
}

HandleResult RequestHandler::handleFramedMessage(const std::uint8_t* message, std::uint32_t messageSize)
{
    if (messageSize < sizeof(RequestResponseHeader))
    {
        return HandleResult::TooShort;
    }

    const auto* header = reinterpret_cast<const RequestResponseHeader*>(message);
    if (header->type() != oc_common::OC_MACHINE_INVOCATION_TYPE)
    {
        return HandleResult::WrongType;
    }
    // The framing header's size() covers header + body; trust the smaller of declared/actual.
    const std::uint32_t declared = header->size();
    if (declared > messageSize || declared < sizeof(RequestResponseHeader))
    {
        return HandleResult::SizeMismatch;
    }

    const std::uint8_t* body = message + sizeof(RequestResponseHeader);
    const std::uint32_t bodySize = declared - sizeof(RequestResponseHeader);
    return handleInvocationBody(body, bodySize);
}

HandleResult RequestHandler::handleInvocationBody(const std::uint8_t* body, std::uint32_t bodySize)
{
    if (bodySize < sizeof(OcMachineInvocation))
    {
        return HandleResult::TooShort;
    }

    OcMachineInvocation hdr{};
    std::memcpy(&hdr, body, sizeof(hdr));

    if (hdr.signatureCount != QUORUM)
    {
        return HandleResult::BadSignatureCount;
    }

    // An OC request is at most MAX_OC_REQUEST_SIZE by protocol; reject oversized ones here so
    // handlers can trust requestSize without re-checking.
    if (hdr.requestSize > oc_common::MAX_OC_REQUEST_SIZE)
    {
        return HandleResult::SizeMismatch;
    }

    // Validate that the declared request + signer bytes exactly fill the body.
    const std::uint64_t expected =
        sizeof(OcMachineInvocation) +
        static_cast<std::uint64_t>(hdr.requestSize) +
        static_cast<std::uint64_t>(hdr.signatureCount) * sizeof(SignerEntry);
    if (expected != bodySize)
    {
        return HandleResult::SizeMismatch;
    }

    const std::uint8_t* requestData = body + sizeof(OcMachineInvocation);
    const auto* signers = reinterpret_cast<const SignerEntry*>(requestData + hdr.requestSize);

    if (_config.verifySignatures)
    {
        // TODO: recompute K12("QUBIC_OC_AUTH" || epoch || interfaceIndex || invocationId ||
        // paramsDigest) and verify each SchnorrQ signature against the epoch's computor list
        // (fetched via the standard peer protocol). Until implemented, verification is skipped.
    }

    if (_handler == nullptr || _handler->interfaceIndex() != hdr.interfaceIndex)
    {
        return HandleResult::NoHandler;
    }

    oc_interfaces::AuthorizedInvocation invocation{};
    invocation.invocationId = hdr.invocationId;
    invocation.epoch = hdr.epoch;
    invocation.interfaceIndex = hdr.interfaceIndex;
    invocation.requestData = requestData;
    invocation.requestSize = hdr.requestSize;
    invocation.signers = signers;
    invocation.signatureCount = hdr.signatureCount;

    return _handler->handle(invocation) ? HandleResult::Ok : HandleResult::HandlerFailed;
}

} // namespace node
