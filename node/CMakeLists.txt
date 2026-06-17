# The OC machine node: TCP server, connection handling, request dispatch, orchestrator.

add_executable(oc_machine_node
    src/oc_machine_node.cpp
    src/node_connection.cpp
    src/request_handler.cpp)

target_include_directories(oc_machine_node PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(oc_machine_node PRIVATE
    oc_common
    oc_interfaces)

target_compile_features(oc_machine_node PRIVATE cxx_std_20)
