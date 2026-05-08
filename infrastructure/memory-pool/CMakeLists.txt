cmake_minimum_required(VERSION 3.25)

project(memory_pool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(WARNING "This project is intended to be configured with clang++.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MEMORY_POOL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(MEMORY_POOL_ENABLE_CLANG_TIDY "Enable clang-tidy when available" ON)
option(MEMORY_POOL_ENABLE_NATIVE_OPTIMIZATIONS "Enable native CPU tuning in Release" ON)

include(FetchContent)
include(CheckIPOSupported)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.16.0.tar.gz
)

FetchContent_Declare(
  googlebenchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.tar.gz
)

FetchContent_MakeAvailable(googletest googlebenchmark)

add_library(memory_pool_project_options INTERFACE)
target_compile_features(memory_pool_project_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(memory_pool_project_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wformat=2
    -Wimplicit-fallthrough
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-qual
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wextra-semi
    -fcolor-diagnostics
  )

  if(MEMORY_POOL_WARNINGS_AS_ERRORS)
    target_compile_options(memory_pool_project_options INTERFACE -Werror)
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(memory_pool_project_options INTERFACE -O3 -fstrict-aliasing -fvectorize -fslp-vectorize)

    if(MEMORY_POOL_ENABLE_NATIVE_OPTIMIZATIONS)
      target_compile_options(memory_pool_project_options INTERFACE -march=native)
    endif()
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(memory_pool_project_options INTERFACE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(memory_pool_project_options INTERFACE -fsanitize=address,undefined)
  endif()
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    message(STATUS "IPO is not available: ${ipo_error}")
  endif()
endif()

if(MEMORY_POOL_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)

  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
  endif()
endif()

add_library(memory_pool_lib
  memory_pool.cpp
)

target_include_directories(memory_pool_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(memory_pool_lib PUBLIC memory_pool_project_options)

add_executable(memory_pool_app main.cpp)
target_link_libraries(memory_pool_app PRIVATE memory_pool_lib)

enable_testing()

add_executable(memory_pool_tests memory_pool_test.cpp)
target_link_libraries(memory_pool_tests PRIVATE memory_pool_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(memory_pool_tests)

add_executable(memory_pool_benchmarks memory_pool_benchmark.cpp)
target_link_libraries(memory_pool_benchmarks PRIVATE memory_pool_lib benchmark::benchmark)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_custom_target(
    run_benchmarks
    COMMAND memory_pool_benchmarks --benchmark_min_time=0.1s
    DEPENDS memory_pool_benchmarks
    USES_TERMINAL
  )
endif()
