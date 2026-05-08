# Exchange Pipeline

## Goal

This module closes the biggest gap in the repository: turning the previously isolated gateway, risk, matching, clearing, and market-data building blocks into one executable workflow.

## Implemented Capabilities

- Gateway-authenticated order ingress with route-event replay
- Payload parsing into a deterministic order workflow request
- Pre-trade validation before any hold reservation or matching attempt
- Quote and base hold reservation before matching submission
- Trade-to-account ownership mapping for clearing settlement
- Residual hold release after fully matched or cancelled orders
- Resting-order feedback into the risk engine for later self-trade prevention
- End-to-end benchmark and integration tests for the happy path and risk rejection path

## Implemented Layout

- module root: integration headers, orchestration implementation, executable entry point, test, and benchmark sources kept together for quick inspection
- focus files: exchange_pipeline.hpp, exchange_pipeline.cpp, exchange_pipeline_test.cpp, and exchange_pipeline_benchmark.cpp

## Measured Snapshot

- End-to-end rest-then-cross workflow: 25867 ns CPU time in Release
- Throughput from the benchmark harness: 77.3k workflows/s
- Result page: BENCHMARK_RESULTS.md

## Build

Debug build with sanitizers:

```bash
cmake -S . -B build/debug -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Release benchmark run:

```bash
cmake -S . -B build/release -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --target run_benchmarks
```
