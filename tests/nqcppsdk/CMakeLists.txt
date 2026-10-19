nq_add_plugin(nq_test_plugin test_plugin.cpp)
nq_add_plugin(nq_test_adapter test_adapter_plugin.cpp)

add_executable(nqcppsdk_test nqcppsdk_test.cpp)
target_link_libraries(nqcppsdk_test PRIVATE nqcppsdk::nqcppsdk ${CMAKE_DL_LIBS})
target_compile_options(nqcppsdk_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(nqcppsdk_test PRIVATE NQ_TEST_PLUGIN_PATH="$<TARGET_FILE:nq_test_plugin>")
add_dependencies(nqcppsdk_test nq_test_plugin)
add_test(NAME nqcppsdk_test COMMAND nqcppsdk_test)

add_executable(adapter_test adapter_test.cpp)
target_link_libraries(adapter_test PRIVATE nqcppsdk::nqcppsdk ${CMAKE_DL_LIBS})
target_compile_options(adapter_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(adapter_test PRIVATE
    NQ_TEST_PLUGIN_PATH="$<TARGET_FILE:nq_test_plugin>"
    NQ_TEST_ADAPTER_PATH="$<TARGET_FILE:nq_test_adapter>"
)
add_dependencies(adapter_test nq_test_plugin nq_test_adapter)
add_test(NAME adapter_test COMMAND adapter_test)
