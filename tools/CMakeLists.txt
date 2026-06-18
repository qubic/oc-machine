# Developer tools for exercising the OC machine node.

add_executable(send_test_invocation send_test_invocation.cpp)
target_link_libraries(send_test_invocation PRIVATE oc_common)
target_compile_features(send_test_invocation PRIVATE cxx_std_20)
