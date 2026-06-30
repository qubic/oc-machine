// TCP server accepting connections from whitelisted Qubic Core nodes, reading framed
// OcMachineInvocation messages. Each connection is served on its own thread.

#pragma once

#include "node/request_handler.h"
#include "oc_common/config.h"

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace node
{

class NodeConnection
{
public:
    NodeConnection(const oc_common::Config& config, RequestHandler& handler);

    // Bind + listen on config.port. Returns false on failure.
    bool start();

    void run();
    void stop();

private:
    bool isWhitelisted(const std::string& ip) const;
    void serveConnection(int connFd, const char* peerIp);
    void deregisterConnectionFd(int fd);

    // Join finished worker threads so the thread vector does not grow over a long run.
    void cleanupFinishedThreads();

    const oc_common::Config& _config;
    RequestHandler& _handler;
    int _listenFd = -1;
    std::atomic<bool> _running{false};

    // One worker thread per connection; _finishedThreadIds marks workers ready to be reaped.
    std::vector<std::thread> _connectionThreads;
    std::set<std::thread::id> _finishedThreadIds;
    std::mutex _connectionThreadsMutex;

    // Active connection fds, so stop() can shut them down to unblock workers stuck in recv().
    std::vector<int> _activeConnectionFds;
    std::mutex _activeConnectionFdsMutex;

    // Serializes handler dispatch so concurrent connections don't race in the external effect.
    std::mutex _dispatchMutex;
};

} // namespace node
