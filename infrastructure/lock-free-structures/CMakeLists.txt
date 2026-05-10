cmake_minimum_required(VERSION 3.25)

project(lock_free_structures VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(WARNING "This project is intended to be configured with clang++.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOCK_FREE_STRUCTURES_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(LOCK_FREE_STRUCTURES_ENABLE_CLANG_TIDY "Enable clang-tidy when available" ON)
option(LOCK_FREE_STRUCTURES_ENABLE_NATIVE_OPTIMIZATIONS "Enable native CPU tuning in Release" ON)

include(CheckIPOSupported)

if(PROJECT_IS_TOP_LEVEL)
  include(FetchContent)

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
elseif(NOT TARGET GTest::gtest_main OR NOT TARGET benchmark::benchmark)
  message(FATAL_ERROR "lock_free_structures requires shared googletest and googlebenchmark targets when added to the workspace")
endif()

add_library(lock_free_structures_project_options INTERFACE)
target_compile_features(lock_free_structures_project_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(lock_free_structures_project_options INTERFACE
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
    -Wthread-safety
    -fcolor-diagnostics
  )

  if(LOCK_FREE_STRUCTURES_WARNINGS_AS_ERRORS)
    target_compile_options(lock_free_structures_project_options INTERFACE -Werror)
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(lock_free_structures_project_options INTERFACE -O3 -fstrict-aliasing -fvectorize -fslp-vectorize)

    if(LOCK_FREE_STRUCTURES_ENABLE_NATIVE_OPTIMIZATIONS)
      target_compile_options(lock_free_structures_project_options INTERFACE -march=native)
    endif()
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(lock_free_structures_project_options INTERFACE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(lock_free_structures_project_options INTERFACE -fsanitize=address,undefined)
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

if(LOCK_FREE_STRUCTURES_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)

  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
  endif()
endif()

add_library(lock_free_structures_lib
  lock_free_structures.cpp
)

target_include_directories(lock_free_structures_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lock_free_structures_lib PUBLIC lock_free_structures_project_options)

add_executable(lock_free_structures_app main.cpp)
target_link_libraries(lock_free_structures_app PRIVATE lock_free_structures_lib)

enable_testing()

add_executable(lock_free_structures_tests lock_free_structures_test.cpp)
target_link_libraries(lock_free_structures_tests PRIVATE lock_free_structures_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(lock_free_structures_tests)

add_executable(lock_free_structures_benchmarks lock_free_structures_benchmark.cpp)
target_link_libraries(lock_free_structures_benchmarks PRIVATE lock_free_structures_lib benchmark::benchmark)

if(PROJECT_IS_TOP_LEVEL AND CMAKE_BUILD_TYPE STREQUAL "Release")
  add_custom_target(
    run_benchmarks
    COMMAND lock_free_structures_benchmarks --benchmark_min_time=0.1s
    DEPENDS lock_free_structures_benchmarks
    USES_TERMINAL
  )
endif()
