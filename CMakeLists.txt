cmake_minimum_required(VERSION 3.25)

project(collections VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(collections_enable_reactor_default ON)
if(APPLE)
  set(collections_enable_reactor_default OFF)
endif()

option(COLLECTIONS_ENABLE_REACTOR "Include the reactor submodule in the workspace build" ${collections_enable_reactor_default})
option(COLLECTIONS_ENABLE_REGEX "Include the regex submodule in the workspace build" ON)

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

enable_testing()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

function(add_workspace_project source_rel_path)
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${source_rel_path}/CMakeLists.txt")
    message(STATUS "Skipping ${source_rel_path}: CMakeLists.txt not found")
    return()
  endif()

  add_subdirectory(${source_rel_path})
endfunction()

add_workspace_project(core-exchange/orderbook-matching-engine)
add_workspace_project(core-exchange/pre-trade-risk-engine)
add_workspace_project(core-exchange/account-clearing-system)
add_workspace_project(core-exchange/unified-access-gateway)

add_workspace_project(infrastructure/lock-free-structures)
add_workspace_project(infrastructure/memory-pool)
add_workspace_project(infrastructure/distributed-consistency)
add_workspace_project(infrastructure/market-data-push-system)

add_workspace_project(tooling/performance-toolkit)
add_workspace_project(tooling/observability-stack)

add_workspace_project(integration/exchange-pipeline)

if(COLLECTIONS_ENABLE_REACTOR)
  add_workspace_project(reactor)
elseif(APPLE)
  message(STATUS "Skipping reactor by default on Apple platforms because reactor currently depends on Linux epoll APIs.")
endif()

if(COLLECTIONS_ENABLE_REGEX)
  add_workspace_project(regex)
endif()
