add_library(nq_fake_plugin MODULE fake_plugin.c)
target_include_directories(nq_fake_plugin PRIVATE ${CMAKE_SOURCE_DIR}/shared/nqcppsdk)
set_target_properties(nq_fake_plugin PROPERTIES PREFIX "" SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_compile_options(nq_fake_plugin PRIVATE -Wall -Wextra -Wpedantic)

add_executable(cli_test cli_test.cpp)
target_link_libraries(cli_test PRIVATE nqbook_core)
target_compile_options(cli_test PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(cli_test PRIVATE
    NQBOOK_PATH="$<TARGET_FILE:nqbook>"
    NQ_TEST_PLUGIN_PATH="$<TARGET_FILE:nq_test_plugin>"
    NQ_FAKE_PLUGIN_PATH="$<TARGET_FILE:nq_fake_plugin>"
    NQ_REPLAY_ADAPTER_PATH="$<TARGET_FILE:replay>"
)
add_dependencies(cli_test nqbook nq_test_plugin nq_fake_plugin replay)
add_test(NAME cli_test COMMAND cli_test)
