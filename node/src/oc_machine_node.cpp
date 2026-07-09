// OC machine node orchestrator + entry point.
//
// Loads config, selects the interface handler for the configured interfaceIndex, wires it to
// the request handler and TCP server, and runs the accept loop.
//
// Mirrors the OM machine's oracle_machine_node.cpp.

#include "node/node_connection.h"
#include "node/request_handler.h"

#include "mock/mock_oc_service.h"
#include "oc_common/config.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

namespace
{

// Set by the signal handler only. Setting a lock-free atomic flag is async-signal-safe; the
// actual teardown (locks, joins, fd close) runs on the main thread after run() returns.
std::atomic<bool> g_stopRequested{false};

void handleSignal(int)
{
    g_stopRequested.store(true);
}

// Construct the handler matching the configured interface index. Returns nullptr if the index
// is not served by this build.
std::unique_ptr<oc_interfaces::BaseOcService> makeHandler(const oc_common::Config& config)
{
    switch (config.interfaceIndex)
    {
    case oc_interfaces::mock::MockOcService::kInterfaceIndex:
        return std::make_unique<oc_interfaces::mock::MockOcService>(
            "mock_oc_sink.txt", config.mockServiceHost, config.mockServicePort, config.machineId);
    default:
        return nullptr;
    }
}

} // namespace

int main()
{
    const oc_common::Config config = oc_common::Config::fromEnvironment();

    std::cout << "Qubic OC machine node starting (port " << config.port
              << ", interfaceIndex " << config.interfaceIndex
              << ", verifySignatures " << (config.verifySignatures ? "on" : "off") << ")\n";

    if (config.mockServiceHost.empty())
    {
        std::cout << "Mock service forwarding: disabled (set OC_MACHINE_MOCK_SERVICE_HOST to enable)\n";
    }
    else
    {
        std::cout << "Mock service forwarding: http://" << config.mockServiceHost << ":"
                  << config.mockServicePort << "/ingest"
                  << (config.machineId.empty() ? "" : " as \"" + config.machineId + "\"") << "\n";
    }

    if (config.verifySignatures)
    {
        std::cerr << "WARNING: signature verification is requested but NOT yet implemented; "
                     "bundles are accepted without re-verifying the 451 signatures.\n";
    }

    auto handler = makeHandler(config);
    if (!handler)
    {
        std::cerr << "No handler for interfaceIndex " << config.interfaceIndex << "\n";
        return 1;
    }

    node::RequestHandler requestHandler(config, handler.get());
    node::NodeConnection connection(config, requestHandler);

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    if (!connection.start())
    {
        return 1;
    }

    // Run the accept loop on its own thread so the main thread can wait for a shutdown signal
    // and then perform the (non-signal-safe) teardown itself.
    std::thread runThread([&connection]() { connection.run(); });

    while (!g_stopRequested.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    connection.stop();            // signal the accept loop to exit and unblock workers
    runThread.join();             // run() fully exits, so it can no longer spawn workers
    connection.joinConnections(); // now safe to join every worker thread

    std::cout << "Qubic OC machine node stopped\n";
    return 0;
}
