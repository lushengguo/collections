#include <benchmark/benchmark.h>

#include "distributed_consistency.hpp"

static void BM_SagaExecution(benchmark::State &state)
{
    distributed_consistency::SagaCoordinator coordinator;
    coordinator.add_step(
        "reserve",
        [](distributed_consistency::SagaContext &context) {
            context.attributes["reserve"] = "done";
            return distributed_consistency::StepResult::kSuccess;
        },
        [](distributed_consistency::SagaContext &context) { context.attributes["reserve"] = "compensated"; });
    coordinator.add_step(
        "publish",
        [](distributed_consistency::SagaContext &context) {
            context.attributes["publish"] = "done";
            return distributed_consistency::StepResult::kSuccess;
        },
        [](distributed_consistency::SagaContext &context) { context.attributes["publish"] = "compensated"; });

    for (auto _ : state)
    {
        distributed_consistency::SagaContext context;
        const auto report = coordinator.execute(context);
        const auto *report_address = &report;
        benchmark::DoNotOptimize(report_address);
        benchmark::ClobberMemory();
    }
}

static void BM_OutboxBatching(benchmark::State &state)
{
    for (auto _ : state)
    {
        distributed_consistency::OutboxStore outbox;
        for (int index = 0; index < state.range(0); ++index)
        {
            outbox.append({
                .id = "event-" + std::to_string(index),
                .topic = "ledger",
                .payload = "payload",
            });
        }

        auto batch = outbox.pending_batch(static_cast<std::size_t>(state.range(0)));
        benchmark::DoNotOptimize(batch.size());
    }
}

BENCHMARK(BM_SagaExecution);
BENCHMARK(BM_OutboxBatching)->Arg(1000);
BENCHMARK_MAIN();
