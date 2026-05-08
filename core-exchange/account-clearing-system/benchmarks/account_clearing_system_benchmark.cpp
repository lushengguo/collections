#include <benchmark/benchmark.h>

#include "account_clearing_system/account_clearing_system.hpp"

static void BM_FreezeAndRelease(benchmark::State &state)
{
    account_clearing_system::AccountClearingSystem system;
    system.upsert_account("buyer", {.available_quote = 1000000.0});
    std::uint64_t sequence = 0;

    for (auto _ : state)
    {
        const auto hold_id = "hold-" + std::to_string(++sequence);
        bool frozen = system.freeze_quote("buyer", hold_id, 10.0, static_cast<std::int64_t>(sequence));
        benchmark::DoNotOptimize(&frozen);
        bool released = system.release_hold(hold_id, static_cast<std::int64_t>(sequence + 1));
        benchmark::DoNotOptimize(&released);
    }
}

static void BM_SpotSettlement(benchmark::State &state)
{
    std::uint64_t sequence = 0;
    for (auto _ : state)
    {
        account_clearing_system::AccountClearingSystem system;
        system.upsert_account("buyer", {.available_quote = 1200.0});
        system.upsert_account("seller", {.available_base = 1.0, .base_cost = 800.0});
        (void)system.freeze_quote("buyer", "hold-buy", 1005.0, 1);
        (void)system.freeze_base("seller", "hold-sell", 1.0, 1);
        auto result = system.settle_spot_trade(
            {
                .trade_id = "trade-" + std::to_string(++sequence),
                .symbol = "BTCUSDT",
                .buyer_account_id = "buyer",
                .seller_account_id = "seller",
                .price = 1000.0,
                .quantity = 1.0,
                .buyer_is_taker = true,
                .timestamp_ms = 2,
            },
            {.maker_fee_bps = 2.0, .taker_fee_bps = 5.0});
        benchmark::DoNotOptimize(&result);
    }
}

BENCHMARK(BM_FreezeAndRelease)->Arg(1 << 12);
BENCHMARK(BM_SpotSettlement)->Arg(1 << 10);
BENCHMARK_MAIN();
