# Shared library: OC wire-message definitions + runtime config.

add_library(oc_common STATIC
    src/config.cpp)

target_include_directories(oc_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(oc_common PUBLIC cxx_std_20)
