# Orderbook Matching Engine

## Goal

This module will evolve into a single-symbol continuous auction engine for a centralized exchange. The implementation target is a production-oriented matching core with deterministic state transitions, efficient price-level indexing, incremental market data generation, and measurable latency characteristics.

## Planned Design

- A bid-side and ask-side orderbook based on skip-list price levels
- Price-time priority matching for limit, market, IOC, FOK, and GTX orders
- Conditional order activation for stop-profit and stop-loss workflows
- Order state transitions covering accepted, queued, partially filled, filled, canceled, rejected, and expired states
- Trade event generation, market depth snapshots, and incremental book updates
- Persistence hooks for append-only matching logs and replay support

## Implemented Layout

- module root: public headers, matching implementation, executable entry point, tests, and benchmark sources kept together for quick inspection
- focus files: orderbook_matching_engine.hpp, orderbook_matching_engine.cpp, orderbook_matching_engine_test.cpp, and orderbook_matching_engine_benchmark.cpp

## Build

Debug build with AddressSanitizer:

```bash
cmake -S . -B build/debug -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Release build for benchmarks:

```bash
cmake -S . -B build/release -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target run_benchmarks
```

## Measured Benchmark Snapshot

- `BM_LimitInsertions/2000`: 1229035 ns CPU time, 1.63M inserts/s
- `BM_MixedMatchingFlow/200`: 387182 ns CPU time, 1.03M ops/s
- Result page: BENCHMARK_RESULTS.md
