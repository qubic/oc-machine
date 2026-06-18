// TCP server that accepts incoming connections from whitelisted Qubic Core nodes and reads
// framed OcMachineInvocation messages off the wire.
//
// Mirrors the OM machine's NodeConnection. The connection is incoming from the OC machine's
// perspective: the Core node dials out and keeps the connection open; this server accepts and
// reads. There is no reply on this channel.
//
// Scaffold: the accept/read loop is declared but not yet implemented (POSIX sockets).

#pragma once

#include "node/request_handler.h"
#include "oc_common/config.h"

#include <string>

namespace node
{

class NodeConnection
{
public:
    NodeConnection(const oc_common::Config& config, RequestHandler& handler);

    // Bind + listen on config.port. Returns false on failure.
    bool start();

    // Blocking accept/read loop: accept whitelisted Core nodes, read framed messages, hand
    // each to the RequestHandler. Returns when stopped or on fatal error.
    void run();

    // Request the run loop to stop.
    void stop();

private:
    bool isWhitelisted(const std::string& ip) const;

    // Read framed messages from one connected Core node until it closes or errors.
    void serveConnection(int connFd, const char* peerIp);

    const oc_common::Config& _config;
    RequestHandler& _handler;
    int _listenFd = -1;
    volatile bool _running = false;
};

} // namespace node
