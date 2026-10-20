nq_add_plugin(sdk_test_plugin test_plugin.cpp)
nq_add_plugin(sdk_test_adapter test_adapter_plugin.cpp)

add_executable(feature_test feature_test.cpp)
target_link_libraries(feature_test PRIVATE nqcppsdk::nqcppsdk ${CMAKE_DL_LIBS})
target_compile_options(feature_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(feature_test PRIVATE NQ_TEST_PLUGIN_PATH="$<TARGET_FILE:sdk_test_plugin>")
add_dependencies(feature_test sdk_test_plugin)
add_test(NAME feature_test COMMAND feature_test)

add_executable(adapter_test adapter_test.cpp)
target_link_libraries(adapter_test PRIVATE nqcppsdk::nqcppsdk ${CMAKE_DL_LIBS})
target_compile_options(adapter_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(adapter_test PRIVATE
    NQ_TEST_PLUGIN_PATH="$<TARGET_FILE:sdk_test_plugin>"
    NQ_TEST_ADAPTER_PATH="$<TARGET_FILE:sdk_test_adapter>"
)
add_dependencies(adapter_test sdk_test_plugin sdk_test_adapter)
add_test(NAME adapter_test COMMAND adapter_test)
