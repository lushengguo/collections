#include <benchmark/benchmark.h>

#include "observability_stack.hpp"

static void BM_MetricPublishAndFlush(benchmark::State &state)
{
    observability_stack::ObservabilityStack stack(4096);
    for (auto _ : state)
    {
        const auto accepted = stack.publish_metric({
            .name = "gateway.requests",
            .value = static_cast<double>(state.iterations() % 1024),
            .timestamp = 1,
            .type = observability_stack::MetricType::kCounter,
            .trace_context = {.trace_id = "trace-1", .span_id = "span-1", .parent_span_id = "", .component = "gateway"},
        });
        benchmark::DoNotOptimize(&accepted);
        if (stack.pending_events() >= 512U)
        {
            benchmark::DoNotOptimize(stack.flush());
        }
    }
    benchmark::DoNotOptimize(stack.flush());
}

static void BM_AlertRuleEvaluation(benchmark::State &state)
{
    observability_stack::ObservabilityStack stack(4096);
    stack.register_rule({
        .rule_id = "latency-alert",
        .metric_name = "gateway.latency.p99",
        .threshold = 50.0,
        .consecutive_breaches = 3,
        .severity = observability_stack::AlertSeverity::kCritical,
    });

    std::uint64_t value = 0;
    for (auto _ : state)
    {
        const auto accepted = stack.publish_metric({
            .name = "gateway.latency.p99",
            .value = static_cast<double>((++value % 7U) * 20U),
            .timestamp = static_cast<std::int64_t>(value),
            .type = observability_stack::MetricType::kGauge,
            .trace_context = {.trace_id = "trace-2", .span_id = "span-2", .parent_span_id = "", .component = "gateway"},
        });
        benchmark::DoNotOptimize(&accepted);
        if (stack.pending_events() >= 512U)
        {
            benchmark::DoNotOptimize(stack.flush());
        }
    }
    benchmark::DoNotOptimize(stack.flush());
    benchmark::DoNotOptimize(stack.alerts().size());
}

BENCHMARK(BM_MetricPublishAndFlush)->Arg(1 << 12);
BENCHMARK(BM_AlertRuleEvaluation)->Arg(1 << 12);
BENCHMARK_MAIN();
