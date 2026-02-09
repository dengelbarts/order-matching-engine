cmake_minimum_required(VERSION 3.14)

project(OrderMatchingEngine
        VERSION 0.1
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(EngineLib STATIC
    src/engine.cpp
)

target_include_directories(EngineLib PUBLIC "${PROJECT_SOURCE_DIR}/include")

add_executable(OME src/main.cpp)
target_link_libraries(OME PRIVATE EngineLib)

if(MSVC)
    target_compile_options(EngineLib PRIVATE /W4 /WX)
    target_compile_options(OME PRIVATE /W4 /WX)
else()
    target_compile_options(EngineLib PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_options(OME PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(unit_tests tests/test_engine.cpp)
target_link_libraries(unit_tests PRIVATE EngineLib gtest_main)

include(GoogleTest)
gtest_discover_tests(unit_tests)