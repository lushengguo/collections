cmake_minimum_required(VERSION 3.25)

project(exchange_pipeline VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(WARNING "This project is intended to be configured with clang++.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EXCHANGE_PIPELINE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(EXCHANGE_PIPELINE_ENABLE_CLANG_TIDY "Enable clang-tidy when available" ON)
option(EXCHANGE_PIPELINE_ENABLE_NATIVE_OPTIMIZATIONS "Enable native CPU tuning in Release" ON)

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
  message(FATAL_ERROR "exchange_pipeline requires shared googletest and googlebenchmark targets when added to the workspace")
endif()

add_library(exchange_pipeline_project_options INTERFACE)
target_compile_features(exchange_pipeline_project_options INTERFACE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(exchange_pipeline_project_options INTERFACE
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

  if(EXCHANGE_PIPELINE_WARNINGS_AS_ERRORS)
    target_compile_options(exchange_pipeline_project_options INTERFACE -Werror)
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(exchange_pipeline_project_options INTERFACE -O3 -fstrict-aliasing -fvectorize -fslp-vectorize)

    if(EXCHANGE_PIPELINE_ENABLE_NATIVE_OPTIMIZATIONS)
      target_compile_options(exchange_pipeline_project_options INTERFACE -march=native)
    endif()
  endif()

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(exchange_pipeline_project_options INTERFACE -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined)
    target_link_options(exchange_pipeline_project_options INTERFACE -fsanitize=address,undefined)
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

if(EXCHANGE_PIPELINE_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy)

  if(CLANG_TIDY_EXE)
    set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_EXE})
  endif()
endif()

add_library(exchange_pipeline_lib exchange_pipeline.cpp)

target_include_directories(exchange_pipeline_lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core-exchange/unified-access-gateway
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core-exchange/pre-trade-risk-engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core-exchange/orderbook-matching-engine
    ${CMAKE_CURRENT_SOURCE_DIR}/../../core-exchange/account-clearing-system
    ${CMAKE_CURRENT_SOURCE_DIR}/../../infrastructure/distributed-consistency
    ${CMAKE_CURRENT_SOURCE_DIR}/../../infrastructure/lock-free-structures
    ${CMAKE_CURRENT_SOURCE_DIR}/../../infrastructure/memory-pool
    ${CMAKE_CURRENT_SOURCE_DIR}/../../infrastructure/market-data-push-system
)
target_link_libraries(exchange_pipeline_lib
  PUBLIC
    exchange_pipeline_project_options
)

if(PROJECT_IS_TOP_LEVEL)
  target_sources(exchange_pipeline_lib PRIVATE
    ../../core-exchange/unified-access-gateway/unified_access_gateway.cpp
    ../../core-exchange/pre-trade-risk-engine/pre_trade_risk_engine.cpp
    ../../core-exchange/orderbook-matching-engine/orderbook_matching_engine.cpp
    ../../core-exchange/account-clearing-system/account_clearing_system.cpp
  )
else()
  target_link_libraries(exchange_pipeline_lib
    PUBLIC
      unified_access_gateway_lib
      pre_trade_risk_engine_lib
      orderbook_matching_engine_lib
      account_clearing_system_lib
  )
endif()

add_executable(exchange_pipeline_app main.cpp)
target_link_libraries(exchange_pipeline_app PRIVATE exchange_pipeline_lib)

enable_testing()

add_executable(exchange_pipeline_tests exchange_pipeline_test.cpp)
target_link_libraries(exchange_pipeline_tests PRIVATE exchange_pipeline_lib GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(exchange_pipeline_tests)

add_executable(exchange_pipeline_benchmarks exchange_pipeline_benchmark.cpp)
target_link_libraries(exchange_pipeline_benchmarks PRIVATE exchange_pipeline_lib benchmark::benchmark)

add_test(
  NAME exchange_pipeline_benchmark_smoke
  COMMAND exchange_pipeline_benchmarks --benchmark_filter=BM_EndToEndRestThenCross --benchmark_min_time=0.02s
)
set_tests_properties(exchange_pipeline_benchmark_smoke PROPERTIES TIMEOUT 20)

if(PROJECT_IS_TOP_LEVEL)
  add_custom_target(
    run_benchmark_smoke
    COMMAND ${CMAKE_CTEST_COMMAND} -R exchange_pipeline_benchmark_smoke --output-on-failure
    DEPENDS exchange_pipeline_benchmarks
    USES_TERMINAL
  )

  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_target(
      run_benchmarks
      COMMAND exchange_pipeline_benchmarks --benchmark_min_time=0.1s
      DEPENDS exchange_pipeline_benchmarks
      USES_TERMINAL
    )
  endif()
endif()
