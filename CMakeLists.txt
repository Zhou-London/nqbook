cmake_minimum_required(VERSION 3.28)

project(orderbook
    VERSION 2.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

add_subdirectory(shared/nlib)
add_subdirectory(sdk)

add_library(orderbook_core STATIC
    src/Orderbook.cpp
    src/Feature/FeatureEngine.cpp
)
target_include_directories(orderbook_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(orderbook_core PUBLIC nlib::nlib nq::sdk)
target_compile_options(orderbook_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(orderbook src/main.cpp)
target_link_libraries(orderbook PRIVATE orderbook_core)
target_compile_options(orderbook PRIVATE -Wall -Wextra -Wpedantic)

option(ORDERBOOK_BUILD_TESTS "Build the orderbook tests" ${PROJECT_IS_TOP_LEVEL})
if(ORDERBOOK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
