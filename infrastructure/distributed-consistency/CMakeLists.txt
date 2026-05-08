cmake_minimum_required(VERSION 3.25)

project(distributed_consistency VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(WARNING "This project is intended to be configured with clang++.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(DISTRIBUTED_CONSISTENCY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(DISTRIBUTED_CONSISTENCY_ENABLE_CLANG_TIDY "Enable clang-tidy when available" ON)
option(DISTRIBUTED_CONSISTENCY_ENABLE_NATIVE_OPTIMIZATIONS "Enable native CPU tuning in Release" ON)

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

add_library(distributed_consistency_project_options INTERFACE)
target_compile_features(distributed_consistency_project_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(distributed_consistency_project_options INTERFACE
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

  if(DISTRIBUTED_CONSISTENCY_WARNINGS_AS_ERRORS)
    target_compile_options(distributed_consistency_project_options INTERFACE -Werror)
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(distributed_consistency_project_options INTERFACE -O3 -fstrict-aliasing -fvectorize -fslp-vectorize)

    if(DISTRIBUTED_CONSISTENCY_ENABLE_NATIVE_OPTIMIZATIONS)
      target_compile_options(distributed_consistency_project_options INTERFACE -march=native)
    endif()
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(distributed_consistency_project_options INTERFACE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(distributed_consistency_project_options INTERFACE -fsanitize=address,undefined)
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

if(DISTRIBUTED_CONSISTENCY_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)

  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
  endif()
endif()

add_library(distributed_consistency_lib
  src/distributed_consistency.cpp
)

target_include_directories(distributed_consistency_lib PUBLIC include)
target_link_libraries(distributed_consistency_lib PUBLIC distributed_consistency_project_options)

add_executable(distributed_consistency_app src/main.cpp)
target_link_libraries(distributed_consistency_app PRIVATE distributed_consistency_lib)

enable_testing()

add_executable(distributed_consistency_tests tests/distributed_consistency_test.cpp)
target_link_libraries(distributed_consistency_tests PRIVATE distributed_consistency_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(distributed_consistency_tests)

add_executable(distributed_consistency_benchmarks benchmarks/distributed_consistency_benchmark.cpp)
target_link_libraries(distributed_consistency_benchmarks PRIVATE distributed_consistency_lib benchmark::benchmark)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_custom_target(
    run_benchmarks
    COMMAND distributed_consistency_benchmarks --benchmark_min_time=0.1s
    DEPENDS distributed_consistency_benchmarks
    USES_TERMINAL
  )
endif()
