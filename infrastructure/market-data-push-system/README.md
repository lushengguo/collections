# Market Data Push System

## Goal

This module will become the exchange market-data fanout and replay service. The implementation target is a broadcaster that can aggregate trades, orderbook deltas, and candles into subscription-oriented streams with strong latency and bandwidth control.

## Planned Design

- Standard candle aggregation for one-minute through daily intervals
- WebSocket broadcasting for trades, depth, and ticker updates
- Incremental snapshot strategy to reduce full-depth bandwidth costs
- Historical replay path for gap recovery and client resubscription
- Subscription fanout management and connection health tracking
- Benchmark coverage for fanout latency and broadcast throughput

## Implemented Layout

- module root: stream, depth, candle, broadcaster, and data-model headers plus implementation, executable, test, and benchmark sources kept together for quick inspection
- focus files: market_data_push_system.hpp, broadcaster.hpp, candle_aggregator.hpp, depth_book.hpp, market_data_push_system_test.cpp, and market_data_push_system_benchmark.cpp

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

- concurrent WebSocket connection capacity
- broadcast latency at P99
- candle generation cost per interval
- replay throughput under backlog recovery
