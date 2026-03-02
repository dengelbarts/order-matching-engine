cmake_minimum_required(VERSION 3.14)

project(OrderMatchingEngine 
    VERSION 0.1.0
    DESCRIPTION "High-performance limit order matching engine in C++"
    LANGUAGES CXX)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")

# Build type-specific flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# AddressSanitizer build option
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

# ThreadSanitizer build option
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(ENABLE_TSAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Fetch Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Fetch Google Benchmark
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

# Enable testing
enable_testing()

# Create a library for all source files (reusable across executables)
add_library(ome_lib
    src/price_level.cpp
    src/order_book.cpp
)
target_include_directories(ome_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)

# Main executable
add_executable(ome_main src/main.cpp)
target_link_libraries(ome_main ome_lib)

# Single test executable with all tests
add_executable(ome_tests
    test/test_main.cpp
    test/test_price.cpp
    test/test_order.cpp
    test/test_price_level.cpp
    test/test_order_book.cpp
    test/test_matching.cpp
    test/test_events.cpp
    test/test_market_orders.cpp
    test/test_ioc_orders.cpp
    test/test_fok_orders.cpp
    test/test_amend_orders.cpp
    test/test_integration.cpp
    test/test_object_pool.cpp
)
target_link_libraries(ome_tests ome_lib GTest::gtest_main)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(ome_tests)

# Print build configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")

# Throughput benchmark (Google Benchmark)
add_executable(ome_bench_throughput bench/bench_throughput.cpp)
target_link_libraries(ome_bench_throughput ome_lib benchmark::benchmark_main)

# Latency benchmark (standalone)
add_executable(ome_bench_latency bench/bench_latency.cpp)
target_link_libraries(ome_bench_latency ome_lib)

add_custom_target(bench
    COMMAND $<TARGET_FILE:ome_bench_throughput>
        --benchmark_format=console
        --benchmark_out_format=json
        --benchmark_out=${PROJECT_SOURCE_DIR}/bench/throughput_results.json
    COMMAND $<TARGET_FILE:ome_bench_latency>
    DEPENDS ome_bench_throughput ome_bench_latency
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Running full OME benchmark suite..."
    VERBATIM
)