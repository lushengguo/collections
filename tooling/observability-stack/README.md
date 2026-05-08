# Observability Stack

## Goal

This module provides a lightweight in-process observability pipeline for exchange components. It focuses on low-overhead telemetry ingestion, bounded event buffering, trace propagation, alert evaluation, and replayable diagnostic history.

## Implemented Capabilities

- Trace context propagation with deterministic child span creation
- SPSC-backed telemetry ingestion path for metrics and spans
- Metric aggregation with sample count, sum, min, max, and last-value snapshots
- Threshold alert rules with consecutive-breach detection
- Replayable event history for metrics, spans, and generated alerts
- Object-pool-backed event envelopes to keep allocation churn bounded

## Infrastructure Reuse

- lock-free-structures: SPSC ring queue for the hot telemetry path
- memory-pool: object pool for queued telemetry envelopes

## Validation Coverage

- trace parent-child propagation
- metric snapshot aggregation
- consecutive-breach alert triggering
- queue overflow accounting
- span duration capture
- benchmark smoke path with timeout-bound execution

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

Smoke benchmark only:

```bash
cmake --build build/debug --target run_benchmark_smoke
```
