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

## Protocol summary (see qcore `final_OC_spec/oc_protocol_spec.md`)

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
  OC machine MAY trust it without re-verifying. A reference implementation verifies anyway.

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
  oc_common/     Shared config + the OC wire-message definitions (mirror of qcore)
docker/          Container build/deploy (placeholder)
submodules/      Intended home for the qubic_core submodule (see libs/oc_common note)
```

## Build

Requires CMake ≥ 3.21 and a C++20 compiler.

```bash
cmake -S . -B build
cmake --build build
# binaries land in build/bin/
```

## Status

**Scaffold only.** TCP server, message parsing, and the Mock handler are skeletons.
Signature verification and the real external-system handlers are not yet implemented.
