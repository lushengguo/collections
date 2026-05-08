cmake_minimum_required(VERSION 3.25)

project(collections_superbuild VERSION 0.1.0 LANGUAGES NONE)

include(ExternalProject)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COLLECTIONS_DEFAULT_BUILD_TYPE "Debug" CACHE STRING "Build type passed to child projects")

if(CMAKE_BUILD_TYPE)
  set(COLLECTIONS_DEFAULT_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif()

set(collections_enable_reactor_default ON)
if(APPLE)
  set(collections_enable_reactor_default OFF)
endif()

option(COLLECTIONS_ENABLE_REACTOR "Include the reactor submodule in the superbuild" ${collections_enable_reactor_default})
option(COLLECTIONS_ENABLE_REGEX "Include the regex submodule in the superbuild" ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

function(add_workspace_project target_name source_rel_path)
  set(source_dir "${CMAKE_SOURCE_DIR}/${source_rel_path}")
  set(binary_dir "${CMAKE_BINARY_DIR}/${source_rel_path}")

  if(NOT EXISTS "${source_dir}/CMakeLists.txt")
    message(STATUS "Skipping ${target_name}: ${source_dir}/CMakeLists.txt not found")
    return()
  endif()

  set(project_cmake_args
    -DCMAKE_BUILD_TYPE=${COLLECTIONS_DEFAULT_BUILD_TYPE}
  )

  if(CMAKE_CXX_COMPILER)
    list(APPEND project_cmake_args -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
  endif()

  ExternalProject_Add(${target_name}
    SOURCE_DIR ${source_dir}
    BINARY_DIR ${binary_dir}
    CMAKE_ARGS ${project_cmake_args}
    INSTALL_COMMAND ""
    TEST_COMMAND ""
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
  )

  add_custom_target(${target_name}_test
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${binary_dir} --output-on-failure
    DEPENDS ${target_name}
    USES_TERMINAL
  )

  set_property(TARGET ${target_name} PROPERTY FOLDER workspace)
  set_property(TARGET ${target_name}_test PROPERTY FOLDER workspace)

  set_property(GLOBAL APPEND PROPERTY COLLECTIONS_BUILD_TARGETS ${target_name})
  set_property(GLOBAL APPEND PROPERTY COLLECTIONS_TEST_TARGETS ${target_name}_test)
endfunction()

add_workspace_project(core_exchange_orderbook_matching_engine core-exchange/orderbook-matching-engine)
add_workspace_project(core_exchange_pre_trade_risk_engine core-exchange/pre-trade-risk-engine)
add_workspace_project(core_exchange_account_clearing_system core-exchange/account-clearing-system)
add_workspace_project(core_exchange_unified_access_gateway core-exchange/unified-access-gateway)

add_workspace_project(infrastructure_lock_free_structures infrastructure/lock-free-structures)
add_workspace_project(infrastructure_memory_pool infrastructure/memory-pool)
add_workspace_project(infrastructure_distributed_consistency infrastructure/distributed-consistency)
add_workspace_project(infrastructure_market_data_push_system infrastructure/market-data-push-system)

add_workspace_project(tooling_performance_toolkit tooling/performance-toolkit)
add_workspace_project(tooling_observability_stack tooling/observability-stack)

add_workspace_project(integration_exchange_pipeline integration/exchange-pipeline)

if(COLLECTIONS_ENABLE_REACTOR)
  add_workspace_project(reactor_project reactor)
elseif(APPLE)
  message(STATUS "Skipping reactor_project by default on Apple platforms because reactor currently depends on Linux epoll APIs.")
endif()

if(COLLECTIONS_ENABLE_REGEX)
  add_workspace_project(regex_project regex)
endif()

get_property(collections_build_targets GLOBAL PROPERTY COLLECTIONS_BUILD_TARGETS)
get_property(collections_test_targets GLOBAL PROPERTY COLLECTIONS_TEST_TARGETS)

if(collections_build_targets)
  add_custom_target(collections_all DEPENDS ${collections_build_targets})
  set_property(TARGET collections_all PROPERTY FOLDER workspace)
endif()

if(collections_test_targets)
  add_custom_target(collections_test_all DEPENDS ${collections_test_targets})
  set_property(TARGET collections_test_all PROPERTY FOLDER workspace)
endif()
