# Lock-Free Structures

## Goal

This module will host low-latency concurrent data structures required by exchange hot paths. The implementation target is a set of queue, hash-table, and versioned skip-list primitives built with atomics and designed to reduce lock contention under bursty workloads.

## Planned Design

- SPSC and MPSC ring buffers for thread-to-thread handoff
- Lock-free hash table primitives for active order and account lookup scenarios
- Versioned skip-list experiments for concurrent orderbook access patterns
- CAS and fetch-add based coordination utilities
- Backoff strategies and cache-line-aware layout decisions
- Benchmark harnesses that compare lock-free and lock-based baselines

## Implemented Layout

- module root: concurrent queue headers, implementation units, executable entry point, tests, and benchmark sources kept together for quick inspection
- focus files: lock_free_structures.hpp, spsc_ring_queue.hpp, mpmc_linked_queue.hpp, lock_free_structures_test.cpp, and lock_free_structures_benchmark.cpp

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

- throughput under multiple producer and consumer patterns
- latency jitter under contention
- CPU utilization against mutex-based equivalents
- cache miss and scalability observations
