# Account Clearing System

## Goal

This module provides the core ledger and settlement flow for spot account balances. The implementation focuses on the highest-signal exchange accounting paths: fund holds, hold release, spot trade settlement, maker and taker fees, seller realized PnL, pooled journal storage, and reliable settlement outbox events.

## Implemented Capabilities

- Quote and base asset hold management for pre-settlement reservations
- Hold release for cancelled or partially reverted workflows
- Spot trade settlement with configurable maker and taker fee schedules
- Average-cost-based seller realized PnL computation
- Journal recording for every balance mutation
- Idempotent trade settlement handling by trade identifier
- Outbox storage for downstream settlement publication and dispatch tracking
- Basic reconciliation on non-negative balance and cost invariants

## Infrastructure Reuse

- distributed-consistency: outbox store for reliable settlement fanout
- memory-pool: pooled journal entry allocation to reduce ledger allocation churn

## Validation Coverage

- quote hold and release flows
- trade settlement balance and fee application
- duplicate trade idempotency
- account reconciliation invariants
- journal and outbox visibility
- timeout-bounded benchmark smoke run

## Measured Benchmark Snapshot

- `BM_FreezeAndRelease/4096`: 128 ns CPU time
- `BM_SpotSettlement/1024`: 1760 ns CPU time
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
