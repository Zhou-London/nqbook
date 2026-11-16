add_library(nqhpc_tests INTERFACE)
target_include_directories(nqhpc_tests INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nqhpc_tests INTERFACE nqhpc_core)
target_compile_options(nqhpc_tests INTERFACE -Wall -Wextra -Wpedantic)
target_compile_definitions(nqhpc_tests INTERFACE
    NQHPC_PATH="$<TARGET_FILE:nqhpc>"
    NQ_EXAMPLE_FEATURES_PATH="$<TARGET_FILE:example_features>"
    NQ_SYNTHETIC_ADAPTER_PATH="$<TARGET_FILE:synthetic>"
    NQ_EXAMPLE_OHLCV_FEATURES_PATH="$<TARGET_FILE:example_ohlcv_features>"
    NQ_SYNTHETIC_OHLCV_ADAPTER_PATH="$<TARGET_FILE:synthetic_ohlcv>"
)

function(nqhpc_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE nqhpc_tests)
    add_dependencies(${name} nqhpc example_features synthetic example_ohlcv_features synthetic_ohlcv)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nqhpc_add_test(orderbook_test orderbook_test.cpp)
nqhpc_add_test(sampling_test sampling_test.cpp)
nqhpc_add_test(engine_test engine_test.cpp)
nq_add_plugin(ignore_reset_features ignore_reset_features.cpp)
target_compile_definitions(engine_test PRIVATE NQ_IGNORE_RESET_FEATURES_PATH="$<TARGET_FILE:ignore_reset_features>")
add_dependencies(engine_test ignore_reset_features)
nqhpc_add_test(pipeline_table_test pipeline_table_test.cpp)
nqhpc_add_test(ohlcv_test ohlcv_test.cpp)
nqhpc_add_test(adapter_engine_test adapter_engine_test.cpp)
nqhpc_add_test(writer_test writer_test.cpp)

add_subdirectory(cli)
