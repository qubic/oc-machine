// Runtime configuration for an OC machine node, populated from environment variables
// (see example_env). Mirrors the OM machine's default_config.h in spirit.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace oc_common
{

struct Config
{
    // TCP port to listen on for incoming Core-node connections.
    std::uint16_t port = 31841;

    // Local address to bind the listener to. Defaults to all interfaces. For co-locating with a
    // Core node on one host, bind a distinct loopback IP (e.g. 127.0.0.2) so this listener does
    // not collide with the Core node's own listener on the same port.
    std::string bindAddress = "0.0.0.0";

    // Core-node IPs allowed to connect (IP whitelist). Dotted-quad strings.
    std::vector<std::string> whitelist;

    // Re-verify the 451 SchnorrQ signatures in each bundle before acting.
    bool verifySignatures = true;

    // Which OC interface this machine serves (matches OC_INTERFACE_INDEX in Qubic Core).
    std::uint16_t interfaceIndex = 0;

    // Mock interface service to forward authorized bundles to (IP or hostname). Empty = do not
    // forward. The machine POSTs the raw OcMachineInvocation body bytes (exactly as received
    // from Core, no re-encoding) to http://<host>:<port>/ingest; the service re-verifies the
    // 451 signatures itself.
    std::string mockServiceHost;
    std::uint16_t mockServicePort = 8000;

    // Identifier sent as X-OC-Machine-Id so the service can count distinct reporting machines
    // (its replication factor). Empty = the service falls back to the sender's IP.
    std::string machineId;

    // Load configuration from environment variables. Returns a Config with defaults
    // for any variable not set.
    static Config fromEnvironment();
};

} // namespace oc_common
