# Qubic OC Machine

Reference implementation of an **OC (Outsourced Computation) machine** node for the
Qubic network.

An OC machine is the external counterpart to the on-chain OC protocol implemented in
Qubic Core. Where an **Oracle Machine (OM)** *brings external data into* Qubic, an OC
machine *carries authorized intent out of* Qubic and acts on an external system.

This repository mirrors the structure of
[`qubic/oracle-machine`](https://github.com/qubic/oracle-machine), with the data flow
**reversed**.

## How it fits together

```
  Smart Contract                Qubic Core                 OC Machine (this repo)
       │                            │                            │
   INVOKE_OC ──────────────►  record invocation                 │
                              451 computors sign                 │
                              status -> AUTHORIZED               │
                                    │                            │
                              OcMachineInvocation ──────────────►│  receive bundle
                              (request + 451 signer entries)     │  (optionally) verify
                              over private out-of-band channel   │  451 SchnorrQ sigs
                                                                 │  dispatch to handler
                                                                 │  act on external system
```

There is **no reply path**. The OC protocol provides no return channel; a contract that
needs to observe an effect does so via a subsequent Oracle query.

## Protocol summary

- The Core node opens an outgoing TCP connection to each configured OC machine and keeps
  it open. The OC machine whitelists the IPs of its configured Core nodes.
- On a Core node, once an invocation collects `QUORUM` (451 of 676) computor signatures,
  it sends a single `OcMachineInvocation` message:
  - fixed 16-byte header: `invocationId`, `epoch`, `interfaceIndex`, `requestSize`,
    `signatureCount` (== 451)
  - `requestSize` bytes of the pinned `OcRequest` payload
  - `signatureCount` × `SignerEntry` (`computorIndex` + 64-byte SchnorrQ signature)
  - maximum size ≈ 30,790 bytes
- The signatures authorize `K12("QUBIC_OC_AUTH" || epoch || interfaceIndex ||
  invocationId || paramsDigest)`. An external verifier validates them against the
  epoch's computor list (queried from the standard peer protocol).
- Because the Core node only sends the bundle after confirming 451 valid signatures, the
  OC machine MAY trust it without re-verifying. Re-verification is optional and is not yet
  implemented here (see Status below).

## Interface deduplication constraint

OC does not deduplicate at the protocol level: every Core node configured for an
interface forwards each authorized invocation, so the same invocation may arrive at
every running OC machine for that interface. External effects MUST be idempotent
(natural dedup: Bitcoin UTXO, EVM nonce, HTTP idempotency key). The `invocationId` is the
dedup key.

## Repository layout

```
node/            Node orchestrator, TCP server, connection + request handling
oc_interfaces/   Per-interface handlers (what to do with an authorized bundle)
  core/          BaseOcService abstract base
  mock/          Mock interface: writes the request value to a local sink (for testing)
libs/
  oc_common/     Shared config + the OC wire-message definitions (mirror of Qubic Core)
tools/           send_test_invocation: frames a synthetic bundle for end-to-end testing
submodules/      qubic_core submodule: shared upstream primitives (header, message types)
```

## Build

Requires CMake ≥ 3.21 and a C++20 compiler.

```bash
cmake -S . -B build
cmake --build build
# binaries land in build/bin/
```

### Docker

```bash
docker build -f docker/Dockerfile -t oc-machine:latest .

# Simple deployment (bridge network, port mapping); copy example_env to docker/.env first:
docker compose -f docker/docker-compose.yml up -d

# Co-located with a Core node on the same host: use host networking so the machine can
# bind a loopback alias matching the node's ocMachineIPs entry, e.g.
docker run -d --name oc-machine --network host --restart unless-stopped \
  -e OC_MACHINE_BIND=127.0.0.2 -e OC_MACHINE_WHITELIST=127.0.0.1 \
  -v "$PWD/data:/opt/qubic/oc-machine/data" \
  oc-machine:latest
# One image, N machines: give each container its own --name, OC_MACHINE_BIND,
# OC_MACHINE_ID, and data volume.
```

## Configuration

The node is configured via environment variables; see `example_env` for the full
annotated list. Key settings: listen port and bind address, the Core-node IP whitelist,
the served `interfaceIndex`, the signature-verification toggle, and the mock-service
forwarding target (`OC_MACHINE_MOCK_SERVICE_HOST` / `_PORT` / `OC_MACHINE_ID`).

## Status

Working Mock reference. The node listens, accepts whitelisted Core connections, and runs
a streaming receive loop that survives multiplexed Core traffic (it consumes every framed
message and acts only on `OcMachineInvocation`). It validates framing, message type,
signature count, and exact size, then dispatches to the interface handler. The Mock
handler writes the request value to a local sink, verified end-to-end via
`send_test_invocation`, and — when `OC_MACHINE_MOCK_SERVICE_HOST` is set — forwards the
raw bundle bytes verbatim to the mock interface service via HTTP `POST /ingest`
(best-effort, no retry; the service re-verifies the 451 signatures itself).

Not yet implemented:

- **Signature verification.** The 451 SchnorrQ signatures are not re-verified;
  `OC_MACHINE_VERIFY_SIGNATURES=1` currently only prints a startup warning. The Core
  node only sends a bundle after confirming quorum, so a trusted operator MAY skip this
  (the OM machine does the same). It is required only for interfaces that forward to an
  external verifier (e.g. an EVM contract).
- **Idempotency / dedup.** Handlers do not yet dedup by `invocationId`; the Mock handler
  simply appends. See "Interface deduplication constraint" above.
- **Real external interfaces.** Only the Mock handler exists; no Bitcoin/EVM interface.
- **Standalone service IPC.** The node links handlers in-process; the separate
  `mock_oc_service` binary is a placeholder.
- **Containerisation and an automated test target.** The end-to-end check is a manual
  `send_test_invocation` run.
