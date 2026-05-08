#include <benchmark/benchmark.h>

#include "performance_toolkit/performance_toolkit.hpp"

static void BM_EnqueueMixedWorkload(benchmark::State &state)
{
    performance_toolkit::PerformanceToolkit toolkit(8192);
    for (auto _ : state)
    {
        const auto accepted = toolkit.enqueue_workload({
            .scenario_name = "mixed",
            .pattern = performance_toolkit::FlowPattern::kMixed,
            .request_count = static_cast<std::size_t>(state.range(0)),
            .burst_size = 4,
            .concurrency = 4,
            .base_timestamp_ms = 100,
        });
        benchmark::DoNotOptimize(&accepted);
        while (auto request = toolkit.poll_request())
        {
            benchmark::DoNotOptimize(&request);
        }
    }
}

static void BM_LatencySummary(benchmark::State &state)
{
    performance_toolkit::PerformanceToolkit toolkit;
    for (std::uint64_t index = 0; index < 4096; ++index)
    {
        toolkit.record_latency(1000 + index, index % 7 != 0);
    }

    for (auto _ : state)
    {
        auto summary = toolkit.summarize(0.01);
        benchmark::DoNotOptimize(&summary);
    }
}

BENCHMARK(BM_EnqueueMixedWorkload)->Arg(1 << 10);
BENCHMARK(BM_LatencySummary)->Arg(1 << 12);
BENCHMARK_MAIN();
