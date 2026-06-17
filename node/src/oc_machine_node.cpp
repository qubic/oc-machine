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

#include <csignal>
#include <iostream>
#include <memory>

namespace
{

node::NodeConnection* g_connection = nullptr;

void handleSignal(int)
{
    if (g_connection)
    {
        g_connection->stop();
    }
}

// Construct the handler matching the configured interface index. Returns nullptr if the index
// is not served by this build.
std::unique_ptr<oc_interfaces::BaseOcService> makeHandler(const oc_common::Config& config)
{
    switch (config.interfaceIndex)
    {
    case oc_interfaces::mock::MockOcService::kInterfaceIndex:
        return std::make_unique<oc_interfaces::mock::MockOcService>("mock_oc_sink.txt");
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

    auto handler = makeHandler(config);
    if (!handler)
    {
        std::cerr << "No handler for interfaceIndex " << config.interfaceIndex << "\n";
        return 1;
    }

    node::RequestHandler requestHandler(config, handler.get());
    node::NodeConnection connection(config, requestHandler);

    g_connection = &connection;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    if (!connection.start())
    {
        return 1;
    }

    connection.run();
    std::cout << "Qubic OC machine node stopped\n";
    return 0;
}
