# OC interface handlers: the abstract base plus per-interface implementations.

# Handler library (base + all built-in interface handlers). The node links this and selects
# a handler at runtime by interfaceIndex.
add_library(oc_interfaces STATIC
    mock/mock_oc_service.cpp)

target_include_directories(oc_interfaces PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(oc_interfaces PUBLIC oc_common)
target_compile_features(oc_interfaces PUBLIC cxx_std_20)

# Standalone Mock service binary (placeholder; see mock/main.cpp).
add_executable(mock_oc_service mock/main.cpp)
target_link_libraries(mock_oc_service PRIVATE oc_interfaces)
