// TCP server that accepts incoming connections from whitelisted Qubic Core nodes and reads
// framed OcMachineInvocation messages off the wire.
//
// Mirrors the OM machine's NodeConnection. The connection is incoming from the OC machine's
// perspective: the Core node dials out and keeps the connection open; this server accepts and
// reads. There is no reply on this channel.
//
// Each accepted connection is served on its own thread, so several Core nodes can stay
// connected concurrently (the Core keeps its connection open persistently).

#pragma once

#include "node/request_handler.h"
#include "oc_common/config.h"

#include <atomic>
#include <mutex>
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

    // Blocking accept loop: accept whitelisted Core nodes and serve each on its own thread.
    // Returns when stopped or on fatal error.
    void run();

    // Request the run loop to stop and join all connection threads.
    void stop();

private:
    bool isWhitelisted(const std::string& ip) const;

    // Read framed messages from one connected Core node until it closes or errors.
    void serveConnection(int connFd, const char* peerIp);

    // Remove a connection fd from the active set (called by a worker as it exits).
    void deregisterConnectionFd(int fd);

    const oc_common::Config& _config;
    RequestHandler& _handler;
    int _listenFd = -1;
    std::atomic<bool> _running{false};

    // One worker thread per connected Core node.
    std::vector<std::thread> _connectionThreads;
    std::mutex _connectionThreadsMutex;

    // File descriptors of currently-served connections. stop() shuts these down so worker
    // threads blocked in recv() unblock and the join() below them completes; each worker
    // deregisters its own fd when it finishes.
    std::vector<int> _activeConnectionFds;
    std::mutex _activeConnectionFdsMutex;

    // Serializes the external-effect dispatch so concurrent connections don't race in the
    // interface handler (e.g. interleaved writes to the same sink).
    std::mutex _dispatchMutex;
};

} // namespace node
