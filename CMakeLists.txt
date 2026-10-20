cmake_minimum_required(VERSION 3.28)

project(orderbook
    VERSION 2.0.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(nlib
    GIT_REPOSITORY https://github.com/Zhou-London/nlib.git
    GIT_TAG v0.6.0
    GIT_SHALLOW ON
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shared/nlib
)
FetchContent_Declare(nqcppsdk
    GIT_REPOSITORY https://github.com/Zhou-London/nqcppsdk.git
    GIT_TAG v0.1.2
    GIT_SHALLOW ON
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shared/nqcppsdk
)
FetchContent_Declare(cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.5.0
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(nlib nqcppsdk cli11)

find_package(Arrow REQUIRED CONFIG)
find_package(Parquet REQUIRED CONFIG)

add_library(nqbook_core STATIC
    src/Orderbook.cpp
    src/Config.cpp
    src/Feature/OrderbookView.cpp
    src/Feature/Sampler.cpp
    src/Feature/FeatureCatalog.cpp
    src/Feature/FeatureEngine.cpp
    src/PipelineTable.cpp
    src/Adapter/AdapterEngine.cpp
    src/Session.cpp
    src/Report.cpp
    src/Writer/CsvWriter.cpp
    src/Writer/ParquetWriter.cpp
    src/Writer/WriterEngine.cpp
)
target_include_directories(nqbook_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(nqbook_core PUBLIC nlib::nlib nqcppsdk::nqcppsdk Parquet::parquet_shared ${CMAKE_DL_LIBS})
target_compile_options(nqbook_core PRIVATE -Wall -Wextra -Wpedantic)

add_subdirectory(plugins)

add_executable(nqbook src/main.cpp)
target_link_libraries(nqbook PRIVATE nqbook_core CLI11::CLI11)
target_compile_options(nqbook PRIVATE -Wall -Wextra -Wpedantic)

option(ORDERBOOK_BUILD_TESTS "Build the orderbook tests" ${PROJECT_IS_TOP_LEVEL})
if(ORDERBOOK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
