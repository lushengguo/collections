# Pre-Trade Risk Engine

## Goal

This module provides a deterministic pre-trade validation chain that can reject bad orders before they reach matching. The implementation focuses on the most important exchange-side checks: order shape, price bands, quantity and notional limits, balance and position sufficiency, user rate limiting, and self-trade prevention.

## Implemented Capabilities

- Market configuration with per-symbol quantity, notional, and rate-limit settings
- Reference-price derivation from a DepthBook top-of-book snapshot
- Buy-side quote balance checks and sell-side base position checks
- Limit-order price band validation against the reference mid price
- User and IP rate limiting with sliding window accounting
- Self-trade prevention against tracked resting liquidity
- Lock-free audit buffering for accepted and rejected decisions

## Infrastructure Reuse

- market-data-push-system: DepthBook for reference price derivation
- lock-free-structures: SPSC queue for audit decision buffering

## Validation Coverage

- valid-order acceptance path
- price-band rejection
- insufficient-balance rejection
- rate-limit rejection
- self-trade prevention
- audit-log capture
- timeout-bounded benchmark smoke run

## Measured Benchmark Snapshot

- `BM_RiskEvaluationAccepted/4096`: 474 ns CPU time
- `BM_RateLimitRejections/4096`: 5369 ns CPU time
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
