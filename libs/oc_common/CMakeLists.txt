# Shared library: OC wire-message definitions + runtime config + minimal HTTP client.
#
# NOTE: signature verification (FourQ/SchnorrQ) is intentionally NOT compiled here. Mirroring
# the OM machine, the OC machine trusts the bundle: a Core node only sends an OcMachineInvocation
# after its engine has confirmed QUORUM (451) valid signatures. Re-verification is
# an optional feature for interfaces that forward the bundle to an external verifier; if added
# later it needs a GCC/Clang shim for Qubic Core's MSVC-only four_q.h (see git history / project notes).

add_library(oc_common STATIC
    src/config.cpp
    src/http_client.cpp)

target_include_directories(oc_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(oc_common PUBLIC cxx_std_20)
