cmake_minimum_required(VERSION 3.31)
project(STRATA VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_STRATA_TESTS "Build strata tests" ON)
option(BUILD_STRATA_BENCHMARKS "Build strata benchmarks" ON)

find_package(benchmark REQUIRED)
find_package(GTest CONFIG REQUIRED)
include(GoogleTest)
include(CTest)
enable_testing()

add_library(strata INTERFACE)

target_include_directories(strata INTERFACE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/Tests
)
target_compile_definitions(strata INTERFACE DEBUG)

if(BUILD_STRATA_TESTS)
    file(GLOB_RECURSE STRATA_TESTS_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp)
    add_executable(strata_tests ${STRATA_TESTS_SOURCE})
    target_link_libraries(strata_tests PUBLIC strata GTest::gtest_main)
    target_include_directories(strata_tests PUBLIC ${PROJECT_SOURCE_DIR})
    gtest_add_tests(TARGET strata_tests)
endif()

if(BUILD_STRATA_BENCHMARKS)
    file(GLOB_RECURSE STRATA_BENCHMARKS_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Benchmarks/*.cpp)
    add_executable(strata_benchmarks ${STRATA_BENCHMARKS_SOURCE})
    target_link_libraries(strata_benchmarks PUBLIC strata benchmark::benchmark_main)
    target_include_directories(strata_benchmarks PUBLIC ${PROJECT_SOURCE_DIR})
endif()
