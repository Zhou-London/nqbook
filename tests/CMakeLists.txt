add_executable(orderbook_test orderbook_test.cpp)
target_link_libraries(orderbook_test PRIVATE nqbook_core)
target_compile_options(orderbook_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME orderbook_test COMMAND orderbook_test)

add_subdirectory(nqcppsdk)
add_subdirectory(cli)
add_subdirectory(adapters)

add_executable(engine_test engine_test.cpp)
target_link_libraries(engine_test PRIVATE nqbook_core)
target_compile_options(engine_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(engine_test PRIVATE NQ_TEST_PLUGIN_PATH="$<TARGET_FILE:nq_test_plugin>")
add_dependencies(engine_test nq_test_plugin)
add_test(NAME engine_test COMMAND engine_test)

add_executable(pipeline_table_test pipeline_table_test.cpp)
target_link_libraries(pipeline_table_test PRIVATE nqbook_core)
target_compile_options(pipeline_table_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(pipeline_table_test PRIVATE
    NQ_TEST_PLUGIN_PATH="$<TARGET_FILE:nq_test_plugin>"
    NQ_TEST_ADAPTER_PATH="$<TARGET_FILE:nq_test_adapter>"
)
add_dependencies(pipeline_table_test nq_test_plugin nq_test_adapter)
add_test(NAME pipeline_table_test COMMAND pipeline_table_test)
