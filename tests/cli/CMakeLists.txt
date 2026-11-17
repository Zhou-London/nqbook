add_library(nq_fake_plugin MODULE fake_plugin.c)
target_include_directories(nq_fake_plugin PRIVATE ${nqcppsdk_SOURCE_DIR})
set_target_properties(nq_fake_plugin PROPERTIES PREFIX "" SUFFIX ".so" C_VISIBILITY_PRESET hidden)
target_compile_options(nq_fake_plugin PRIVATE -Wall -Wextra -Wpedantic)

nqhpc_add_test(cli_test cli_test.cpp)
nqhpc_define_binary_path(cli_test PRIVATE NQ_FAKE_PLUGIN_PATH nq_fake_plugin)
