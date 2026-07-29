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
    std::uint16_t port = 21841;

    // Local address to bind the listener to. Defaults to all interfaces.
    std::string bindAddress = "0.0.0.0";

    // Core-node IPs allowed to connect (IP whitelist). Dotted-quad strings.
    std::vector<std::string> whitelist;

    // Re-verify the 451 SchnorrQ signatures in each bundle before acting.
    bool verifySignatures = true;

    // Which OC interface this machine serves (matches OC_INTERFACE_INDEX in Qubic Core).
    std::uint16_t interfaceIndex = 0;

    // Mock interface service to forward authorized bundles to, parsed from
    // OC_MACHINE_MOCK_SERVICE_URL ("http://host[:port]" or "https://host[:port]"). Empty host =
    // do not forward. The machine POSTs the raw OcMachineInvocation body bytes (exactly as
    // received from Core, no re-encoding) to <url>/ingest; the service re-verifies the 451
    // signatures itself. Port defaults to 80 (http) / 443 (https) when the URL omits it.
    std::string mockServiceHost;
    std::uint16_t mockServicePort = 80;
    bool mockServiceTls = false;

    // Identifier sent as X-OC-Machine-Id so the service can count distinct reporting machines
    // (its replication factor). Empty = the service falls back to the sender's IP.
    std::string machineId;

    // Load configuration from environment variables. Returns a Config with defaults
    // for any variable not set.
    static Config fromEnvironment();
};

} // namespace oc_common
