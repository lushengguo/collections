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

- include/: public interfaces for order, level, event, and matching abstractions
- src/: executable entry point and implementation units
- tests/: GoogleTest-based unit and scenario coverage for matching behavior
- benchmarks/: Google Benchmark entry points plus timeout-bounded smoke execution

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

## Planned Benchmark Coverage

- single-symbol order insertion throughput
- cancel-path latency
- matching latency at P50, P99, and P999
- orderbook snapshot generation cost
- throughput under mixed limit and market order flows
