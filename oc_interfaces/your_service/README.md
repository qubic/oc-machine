# Template: your OC interface handler

Copy this directory to add a new OC interface handler (e.g. `bitcoin/`, `evm_bridge/`).

A handler:

1. Subclasses `oc_interfaces::BaseOcService` (see `../core/base_oc_service.h`).
2. Defines its `OcRequest` struct, byte-for-byte matching the qcore interface header in
   `qcore: src/oc_interfaces/<Name>.h` (use `#pragma pack(push, 1)` and a `static_assert`
   on the size).
3. Returns its `interfaceIndex()` (matching `OC_INTERFACE_INDEX` in
   `qcore: src/oc_core/oc_interfaces_def.h`).
4. Implements `handle(const AuthorizedInvocation&)` to perform the external-system action.
   The effect MUST be idempotent keyed on `invocationId` — every Core node forwards every
   authorized invocation, so the same bundle can arrive multiple times.

See `../mock/` for a minimal working example.
