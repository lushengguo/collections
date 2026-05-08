# Measured Benchmark Results

## Scope

This page replaces planning-only benchmark language with measured Release results captured from the current repository state on May 8, 2026.

Measured modules:
- core-exchange/unified-access-gateway
- core-exchange/pre-trade-risk-engine
- core-exchange/orderbook-matching-engine
- core-exchange/account-clearing-system
- integration/exchange-pipeline

## Environment

- Machine: Apple silicon macOS host
- Compiler: AppleClang 17.0.0.17000013
- Build mode: Release
- Benchmark library: Google Benchmark 1.9.1
- Command style: module-local benchmark executable with `--benchmark_min_time=0.05s`

Notes:
- Google Benchmark could not read `hw.cpufrequency` on this host, so the printed CPU-frequency metadata is not trustworthy.
- Google Benchmark also warned that thread affinity could not be pinned.
- The reported benchmark CPU times and relative comparisons are still useful; the unreliable part is the frequency metadata, not the measured timing rows.

## Results

| Module | Benchmark | Measured CPU time | Throughput / note |
| --- | --- | ---: | --- |
| unified-access-gateway | `BM_RouteAcceptedRequests/4096` | 508 ns | ingress routing fast path |
| unified-access-gateway | `BM_ReplayRouteEvents/1024` | 883 ns | broadcaster-backed route replay |
| pre-trade-risk-engine | `BM_RiskEvaluationAccepted/4096` | 474 ns | accepted validation fast path |
| pre-trade-risk-engine | `BM_RateLimitRejections/4096` | 5369 ns | slower because it exercises sliding-window rejection bookkeeping |
| orderbook-matching-engine | `BM_LimitInsertions/2000` | 1229035 ns | 1.63M inserts/s |
| orderbook-matching-engine | `BM_MixedMatchingFlow/200` | 387182 ns | 1.03M ops/s mixed matching flow |
| account-clearing-system | `BM_FreezeAndRelease/4096` | 128 ns | hold reservation and release hot path |
| account-clearing-system | `BM_SpotSettlement/1024` | 1760 ns | settlement plus journal/outbox side effects |
| exchange-pipeline | `BM_EndToEndRestThenCross/1` | 25867 ns | 77.3k workflows/s across gateway → risk → matching → clearing → market data |

## Design Trade-Offs

1. The repository favors deterministic in-process components over network realism.
This keeps the measurements focused on core state transitions and data-structure costs, not socket stacks, serialization, or kernel scheduling noise.

2. The end-to-end pipeline pays for correctness boundaries explicitly.
The 25.9 us workflow is much slower than any single module because it includes authentication, risk evaluation, hold reservation, matching submission, settlement, market-data publication, and replayable side effects in one path.

3. Rate-limit rejection is intentionally more expensive than the accept fast path.
The risk benchmark shows this clearly: accepted checks complete in 474 ns, while rejection through sliding-window maintenance takes 5369 ns. The design chooses clearer abuse-control accounting over minimizing the rejection path at all costs.

4. Clearing stays cheap by keeping ledger state in memory and journaling via pooled objects.
`BM_FreezeAndRelease` at 128 ns and `BM_SpotSettlement` at 1760 ns show that the current design is optimized for in-process accounting, not durable storage latency.

5. Matching throughput is respectable for a single-process interview-scale engine, but it is not a proof of production readiness.
The measured 1.63M inserts/s and 1.03M mixed-flow ops/s are useful evidence for data-structure and implementation quality. They are not a substitute for multi-symbol, persistence-aware, or multi-core scaling studies.

6. The pipeline benchmark should be read as orchestration cost, not exchange-wide capacity.
The 77.3k workflows/s result measures one synthetic symbol and one specific happy path. It is useful because it proves the modules now compose into a real workflow, but it does not yet characterize saturation behavior, contention cliffs, or recovery behavior.

## Reproduction

Example commands:

```bash
cmake -S core-exchange/orderbook-matching-engine -B build/core-exchange/orderbook-matching-engine/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build/core-exchange/orderbook-matching-engine/release --target orderbook_matching_engine_benchmarks -j4
build/core-exchange/orderbook-matching-engine/release/orderbook_matching_engine_benchmarks --benchmark_min_time=0.05s
```

```bash
cmake -S integration/exchange-pipeline -B build/integration/exchange-pipeline/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build/integration/exchange-pipeline/release --target exchange_pipeline_benchmarks -j4
build/integration/exchange-pipeline/release/exchange_pipeline_benchmarks --benchmark_min_time=0.05s
```
