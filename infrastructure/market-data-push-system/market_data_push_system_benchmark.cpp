#include <benchmark/benchmark.h>

#include "market_data_push_system.hpp"

static void BM_CandleAggregation(benchmark::State &state)
{
    market_data_push_system::CandleAggregator aggregator(60'000);

    for (auto _ : state)
    {
        for (int index = 0; index < state.range(0); ++index)
        {
            const auto candle = aggregator.add_trade({
                .symbol = "BTCUSDT",
                .price = 100.0 + static_cast<double>(index % 20),
                .quantity = 1.0,
                .event_time_ms = static_cast<std::uint64_t>(60'000 + index),
            });
            const auto *candle_address = &candle;
            benchmark::DoNotOptimize(candle_address);
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_BroadcastFanout(benchmark::State &state)
{
    for (auto _ : state)
    {
        market_data_push_system::MarketDataBroadcaster broadcaster;
        const auto subscriber_one = broadcaster.subscribe("trades.BTCUSDT");
        const auto subscriber_two = broadcaster.subscribe("trades.BTCUSDT");

        for (int index = 0; index < state.range(0); ++index)
        {
            const auto message = broadcaster.publish("trades.BTCUSDT", "trade-" + std::to_string(index));
            const auto *message_address = &message;
            benchmark::DoNotOptimize(message_address);
        }

        const auto first_batch = broadcaster.poll(subscriber_one, static_cast<std::size_t>(state.range(0)));
        const auto second_batch = broadcaster.poll(subscriber_two, static_cast<std::size_t>(state.range(0)));
        benchmark::DoNotOptimize(first_batch.data());
        benchmark::DoNotOptimize(second_batch.data());
    }

    state.SetItemsProcessed(state.iterations() * state.range(0) * 2);
}

BENCHMARK(BM_CandleAggregation)->Arg(10000);
BENCHMARK(BM_BroadcastFanout)->Arg(1000);
BENCHMARK_MAIN();
