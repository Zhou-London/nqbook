add_library(nqbook_tests INTERFACE)
target_include_directories(nqbook_tests INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nqbook_tests INTERFACE nqbook_core)
target_compile_options(nqbook_tests INTERFACE -Wall -Wextra -Wpedantic)
target_compile_definitions(nqbook_tests INTERFACE
    NQBOOK_PATH="$<TARGET_FILE:nqbook>"
    NQ_EXAMPLE_FEATURES_PATH="$<TARGET_FILE:example_features>"
    NQ_SYNTHETIC_ADAPTER_PATH="$<TARGET_FILE:synthetic>"
    NQ_REPLAY_ADAPTER_PATH="$<TARGET_FILE:replay>"
)

function(nqbook_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE nqbook_tests)
    add_dependencies(${name} nqbook example_features synthetic replay)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

nqbook_add_test(orderbook_test orderbook_test.cpp)
nqbook_add_test(engine_test engine_test.cpp)
nqbook_add_test(pipeline_table_test pipeline_table_test.cpp)
nqbook_add_test(adapter_engine_test adapter_engine_test.cpp)
nqbook_add_test(replay_test adapters/replay_test.cpp)
nqbook_add_test(writer_test writer_test.cpp)

add_subdirectory(cli)
