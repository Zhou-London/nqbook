nq_add_plugin(nq_test_plugin test_plugin.cpp)

add_executable(sdk_test sdk_test.cpp)
target_link_libraries(sdk_test PRIVATE nq::sdk ${CMAKE_DL_LIBS})
target_compile_options(sdk_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(sdk_test PRIVATE NQ_TEST_PLUGIN_PATH="$<TARGET_FILE:nq_test_plugin>")
add_dependencies(sdk_test nq_test_plugin)
add_test(NAME sdk_test COMMAND sdk_test)
