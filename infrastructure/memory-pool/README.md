# Memory Pool

## Goal

This module will become a high-performance allocator and object reuse layer for exchange hot objects. The implementation target is a thread-aware memory subsystem that minimizes allocator contention, reduces fragmentation, and exposes measurable improvements over general-purpose allocation.

## Planned Design

- Thread-local fixed-size block allocators
- Reuse pools for hot exchange objects such as orders and market-data messages
- Fragmentation tracking and leak-detection support
- Size-class management for predictable allocation behavior
- Debug instrumentation for ownership and lifetime validation
- Benchmark coverage against system allocator baselines

## Implemented Layout

- include/: fixed-block allocator and object-pool interfaces
- src/: executable entry point and implementation units
- tests/: correctness, reuse, and lifecycle validation
- benchmarks/: allocation throughput coverage

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

- allocation and release latency under concurrency
- allocator contention reduction against malloc
- fragmentation rate after bursty workloads
- hot-object reuse efficiency
