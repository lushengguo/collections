# Unified Access Gateway

## Goal

This module provides a protocol-neutral ingress layer for exchange traffic. The implemented focus is on the highest-value front-door controls: request authentication, path-based routing, user-level rate limiting, backend circuit breaking, session liveness tracking, and replayable route-event history.

## Implemented Capabilities

- REST, WebSocket, and FIX request model support
- Deterministic signature validation against registered API credentials
- Path-prefix routing to risk, matching, clearing, and market-data backends
- User-scoped sliding-window rate limiting
- Backend health gating with circuit-open rejection
- SPSC-backed forwarded-request queue for downstream polling
- Session heartbeat tracking and stale session eviction
- Replayable route event history via broadcaster-backed publication

## Infrastructure Reuse

- lock-free-structures: SPSC queue for ingress buffering
- market-data-push-system: broadcaster for route-event replay

## Validation Coverage

- authenticated routing success path
- invalid-signature rejection
- user rate limiting
- circuit-open rejection
- replayable route events
- stale-session cleanup
- timeout-bounded benchmark smoke run

## Measured Benchmark Snapshot

- `BM_RouteAcceptedRequests/4096`: 508 ns CPU time
- `BM_ReplayRouteEvents/1024`: 883 ns CPU time
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
