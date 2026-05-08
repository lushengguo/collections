# collections

This repository contains independent C++20 projects for core centralized exchange building blocks.

Every project is isolated in its own folder and includes:
- a self-contained CMake build
- implemented source, header, test, and benchmark files stored directly in the module root
- a GoogleTest target
- a Google Benchmark target
- AddressSanitizer enabled for Debug builds
- a module README with current capabilities and run instructions

## Project Groups

### Core Exchange

- core-exchange/orderbook-matching-engine
- core-exchange/pre-trade-risk-engine
- core-exchange/account-clearing-system
- core-exchange/unified-access-gateway

### Infrastructure

- infrastructure/lock-free-structures
- infrastructure/memory-pool
- infrastructure/distributed-consistency
- infrastructure/market-data-push-system

### Tooling

- tooling/performance-toolkit
- tooling/observability-stack

### Integration

- integration/exchange-pipeline

## Toolchain

- CMake 3.25+
- clang++ with C++20 support
- Ninja or Make

## Common Build Flow

Configure a module in Debug for development and sanitizers:

```bash
cmake -S core-exchange/orderbook-matching-engine -B build/core-exchange/orderbook-matching-engine/debug -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core-exchange/orderbook-matching-engine/debug
ctest --test-dir build/core-exchange/orderbook-matching-engine/debug --output-on-failure
```

Configure a module in Release for benchmark runs:

```bash
cmake -S core-exchange/orderbook-matching-engine -B build/core-exchange/orderbook-matching-engine/release -G "Unix Makefiles" -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build/core-exchange/orderbook-matching-engine/release --target run_benchmarks
```

Each module README explains the implemented scope, infrastructure reuse, validation coverage, and local build flow.

Measured benchmark results and design trade-offs are documented in BENCHMARK_RESULTS.md.

