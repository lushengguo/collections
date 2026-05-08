#include <benchmark/benchmark.h>

#include "orderbook_matching_engine.hpp"

static void BM_LimitInsertions(benchmark::State &state)
{
    for (auto _ : state)
    {
        orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
        for (int index = 0; index < state.range(0); ++index)
        {
            const auto result = engine.submit({
                .order_id = "bid-" + std::to_string(index),
                .side = orderbook_matching_engine::Side::kBuy,
                .type = orderbook_matching_engine::OrderType::kLimit,
                .tif = orderbook_matching_engine::TimeInForce::kGtc,
                .price = 100.0 - static_cast<double>(index % 25) * 0.01,
                .quantity = 1.0,
                .timestamp = static_cast<std::uint64_t>(index),
            });
            const auto *result_address = &result;
            benchmark::DoNotOptimize(result_address);
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_MixedMatchingFlow(benchmark::State &state)
{
    for (auto _ : state)
    {
        orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
        for (int index = 0; index < state.range(0); ++index)
        {
            const auto seeded = engine.submit({
                .order_id = "ask-" + std::to_string(index),
                .side = orderbook_matching_engine::Side::kSell,
                .type = orderbook_matching_engine::OrderType::kLimit,
                .tif = orderbook_matching_engine::TimeInForce::kGtc,
                .price = 100.0 + static_cast<double>(index % 10) * 0.01,
                .quantity = 1.0,
                .timestamp = static_cast<std::uint64_t>(index),
            });
            const auto *seeded_address = &seeded;
            benchmark::DoNotOptimize(seeded_address);
        }

        for (int index = 0; index < state.range(0); ++index)
        {
            const auto result = engine.submit({
                .order_id = "buy-" + std::to_string(index),
                .side = orderbook_matching_engine::Side::kBuy,
                .type = orderbook_matching_engine::OrderType::kMarket,
                .tif = orderbook_matching_engine::TimeInForce::kIoc,
                .price = 0.0,
                .quantity = 1.0,
                .timestamp = static_cast<std::uint64_t>(state.range(0) + index),
            });
            const auto *result_address = &result;
            benchmark::DoNotOptimize(result_address);
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range(0) * 2);
}

BENCHMARK(BM_LimitInsertions)->Arg(2000);
BENCHMARK(BM_MixedMatchingFlow)->Arg(200);
BENCHMARK_MAIN();
