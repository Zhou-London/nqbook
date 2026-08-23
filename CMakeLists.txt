cmake_minimum_required(VERSION 3.28)

project(nqbook
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(third_party/nlib)

find_package(Arrow REQUIRED)
find_package(Parquet REQUIRED)
find_package(cppzmq REQUIRED)

add_executable(nqbook
    src/Orderbook.cpp
    src/Config.cpp
    src/ParquetWriter.cpp
    src/main.cpp
    src/Threads/FeedThread.cpp
    src/Threads/BookThread.cpp
    src/Threads/WriterThread.cpp
    src/Threads/MetricsThread.cpp
)

target_include_directories(nqbook PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(nqbook PRIVATE
    nlib::nlib
    Arrow::arrow_shared
    Parquet::parquet_shared
    cppzmq
)
