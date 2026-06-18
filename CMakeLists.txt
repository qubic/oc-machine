cmake_minimum_required(VERSION 3.21)

project(qubic_oc_machine
    VERSION 0.1.0
    DESCRIPTION "Qubic OC (Outsourced Computation) machine node"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# All binaries land in build/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Shared library: config + OC wire-message definitions
add_subdirectory(libs/oc_common)

# Interface handlers (BaseOcService + Mock)
add_subdirectory(oc_interfaces)

# The node: TCP server, connection handling, request dispatch
add_subdirectory(node)

# Developer tools (test message sender, etc.)
add_subdirectory(tools)
