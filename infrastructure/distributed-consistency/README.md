# Distributed Consistency

## Goal

This module will become the consistency and transaction coordination toolkit for cross-service exchange workflows. The implementation target is a pragmatic foundation for saga orchestration, TCC fund operations, reliable messaging, and compensating action handling.

## Planned Design

- Saga step orchestration for order and settlement workflows
- TCC primitives for fund reservation, confirmation, and rollback
- Local outbox table abstractions and reliable event dispatch pipeline
- Idempotent retry and compensation policies
- Failure injection support for rollback-path testing
- Benchmark harnesses for transaction latency and recovery behavior

## Implemented Layout

- include/: saga, TCC wallet, and outbox interfaces
- src/: executable entry point and implementation units
- tests/: compensation, reservation, and outbox validation
- benchmarks/: transaction and recovery throughput coverage

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

- transaction success rate under concurrency
- rollback and compensation latency
- reliable delivery overhead
- consistency preservation under partial failure injection
