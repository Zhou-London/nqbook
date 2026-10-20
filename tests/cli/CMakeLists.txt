add_library(nq_fake_plugin MODULE fake_plugin.c)
target_include_directories(nq_fake_plugin PRIVATE ${nqcppsdk_SOURCE_DIR})
set_target_properties(nq_fake_plugin PROPERTIES PREFIX "" SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_compile_options(nq_fake_plugin PRIVATE -Wall -Wextra -Wpedantic)

nqbook_add_test(cli_test cli_test.cpp)
target_compile_definitions(cli_test PRIVATE NQ_FAKE_PLUGIN_PATH="$<TARGET_FILE:nq_fake_plugin>")
add_dependencies(cli_test nq_fake_plugin)
