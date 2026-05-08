# Performance Toolkit

## Goal

This module provides an in-process synthetic load and reporting toolkit for exchange components. The implementation focuses on generating deterministic request streams, buffering generated work with low overhead, collecting latency samples, computing tail percentiles, and rendering a compact bottleneck-oriented report.

## Implemented Capabilities

- Deterministic limit-only, market-burst, and mixed workload generation
- Queue-backed request staging for downstream consumers
- Latency recording with success and failure accounting
- P50, P99, and P999 latency summary computation
- Simple throughput and success-ratio reporting
- Automatic bottleneck hints for tail latency and queue pressure

## Infrastructure Reuse

- lock-free-structures: SPSC queue for generated request staging
- memory-pool: object pool for queued request envelopes

## Validation Coverage

- deterministic workload generation
- market-burst request shaping
- percentile summary computation
- report rendering
- queue overflow accounting
- timeout-bounded benchmark smoke run

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
