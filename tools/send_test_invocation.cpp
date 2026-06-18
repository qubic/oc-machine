// Test tool: frame a synthetic OcMachineInvocation (Mock interface) and send it to a running
// oc_machine_node over TCP. Used to exercise the receive loop + parse + dispatch + Mock handler
// end-to-end without a real Qubic Core node.
//
// Usage: send_test_invocation <host> <port> <value>
//
// Builds a message with QUORUM signer entries (zeroed signatures — the reference node does not
// verify them, per OC spec §8). Verifies nothing itself; check the node's Mock sink file.

#include "oc_common/oc_wire.h"

#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

using namespace oc_common;

namespace
{

// Mirrors qcore OCI::Mock::OcRequest.
#pragma pack(push, 1)
struct MockOcRequest
{
    std::uint64_t value;
};
#pragma pack(pop)

} // namespace

int main(int argc, char** argv)
{
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    const std::uint16_t port = static_cast<std::uint16_t>((argc > 2) ? std::atoi(argv[2]) : 31841);
    const std::uint64_t value = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 42;

    // Build the message body: OcMachineInvocation header + MockOcRequest + QUORUM SignerEntries.
    const std::uint16_t requestSize = sizeof(MockOcRequest);
    const std::uint32_t bodySize =
        sizeof(OcMachineInvocation) + requestSize + QUORUM * sizeof(SignerEntry);
    const std::uint32_t totalSize = sizeof(RequestResponseHeader) + bodySize;

    std::vector<std::uint8_t> msg(totalSize, 0);

    // Framing header.
    auto* header = reinterpret_cast<RequestResponseHeader*>(msg.data());
    header->_size[0] = static_cast<std::uint8_t>(totalSize);
    header->_size[1] = static_cast<std::uint8_t>(totalSize >> 8);
    header->_size[2] = static_cast<std::uint8_t>(totalSize >> 16);
    header->_type = OC_MACHINE_INVOCATION_TYPE;
    header->_dejavu = 0;

    // Invocation header.
    auto* inv = reinterpret_cast<OcMachineInvocation*>(msg.data() + sizeof(RequestResponseHeader));
    inv->invocationId = 12345;
    inv->epoch = 100;
    inv->interfaceIndex = 0; // Mock
    inv->requestSize = requestSize;
    inv->signatureCount = static_cast<std::uint16_t>(QUORUM);

    // Request payload.
    auto* req = reinterpret_cast<MockOcRequest*>(msg.data() + sizeof(RequestResponseHeader) +
                                                 sizeof(OcMachineInvocation));
    req->value = value;

    // Signer entries: computorIndex 0..QUORUM-1, zeroed signatures (not verified).
    auto* signers = reinterpret_cast<SignerEntry*>(msg.data() + sizeof(RequestResponseHeader) +
                                                   sizeof(OcMachineInvocation) + requestSize);
    for (std::uint32_t i = 0; i < QUORUM; ++i)
    {
        signers[i].computorIndex = static_cast<std::uint16_t>(i);
    }

    // Connect and send.
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        std::cerr << "socket() failed\n";
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host, &addr.sin_addr) != 1)
    {
        std::cerr << "bad host " << host << "\n";
        ::close(fd);
        return 1;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "connect() to " << host << ":" << port << " failed\n";
        ::close(fd);
        return 1;
    }

    std::size_t sent = 0;
    while (sent < msg.size())
    {
        const ssize_t n = ::send(fd, msg.data() + sent, msg.size() - sent, 0);
        if (n <= 0)
        {
            std::cerr << "send() failed\n";
            ::close(fd);
            return 1;
        }
        sent += static_cast<std::size_t>(n);
    }

    std::cout << "Sent OcMachineInvocation (" << totalSize << " bytes, value " << value
              << ") to " << host << ":" << port << "\n";
    ::close(fd);
    return 0;
}
