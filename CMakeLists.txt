cmake_minimum_required(VERSION 3.28)

project(nqbook
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(include/nlib)

add_executable(nqbook
    src/main.cpp
)

target_include_directories(nqbook PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(nqbook PRIVATE nlib::nlib)
