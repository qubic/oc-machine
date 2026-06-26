// Entry point for the standalone Mock OC service binary.
//
// The Mock service is buildable on its own so it can be exercised in isolation, but the node
// currently links the handler directly for the in-process path, so this binary is optional.
//
// This main is a placeholder: it constructs the handler and reports readiness. Wiring it to
// receive invocations over an IPC/socket from the node is future work.

#include "mock/mock_oc_service.h"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    const std::string sinkPath = (argc > 1) ? argv[1] : "mock_oc_sink.txt";

    oc_interfaces::mock::MockOcService service(sinkPath);
    std::cout << "Mock OC service ready (interfaceIndex " << service.interfaceIndex()
              << ", sink " << sinkPath << ")\n";

    // TODO: receive AuthorizedInvocation messages from the node and dispatch to service.handle().
    return EXIT_SUCCESS;
}
