add_executable(orderbook_test orderbook_test.cpp)
target_link_libraries(orderbook_test PRIVATE orderbook_core)
target_compile_options(orderbook_test PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME orderbook_test COMMAND orderbook_test)

add_subdirectory(nqcppsdk)
