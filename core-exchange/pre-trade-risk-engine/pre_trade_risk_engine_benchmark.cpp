#include <benchmark/benchmark.h>

#include "pre_trade_risk_engine.hpp"

namespace
{

void configure_engine(pre_trade_risk_engine::PreTradeRiskEngine &engine)
{
    engine.configure_market({
        .symbol = "BTCUSDT",
        .max_order_quantity = 10.0,
        .max_order_notional = 1000000.0,
        .max_price_deviation_ratio = 0.05,
        .max_requests_per_window = 100000,
        .rate_limit_window_ms = 1000,
        .enable_self_trade_prevention = true,
    });
    engine.update_top_of_book("BTCUSDT", 9990.0, 10010.0);
    engine.set_account_state("alice", "BTCUSDT", {.quote_balance = 1000000.0, .base_position = 10.0});
}

} // namespace

static void BM_RiskEvaluationAccepted(benchmark::State &state)
{
    pre_trade_risk_engine::PreTradeRiskEngine engine(4096);
    configure_engine(engine);
    std::uint64_t sequence = 0;
    for (auto _ : state)
    {
        auto decision = engine.evaluate({
            .order_id = "ord-" + std::to_string(++sequence),
            .user_id = "alice",
            .symbol = "BTCUSDT",
            .source_ip = "10.0.0.1",
            .side = pre_trade_risk_engine::Side::kBuy,
            .type = pre_trade_risk_engine::OrderType::kLimit,
            .price = 10000.0,
            .quantity = 0.5,
            .timestamp_ms = static_cast<std::int64_t>(sequence),
        });
        benchmark::DoNotOptimize(&decision);
    }
}

static void BM_RateLimitRejections(benchmark::State &state)
{
    pre_trade_risk_engine::PreTradeRiskEngine engine(4096);
    engine.configure_market({
        .symbol = "BTCUSDT",
        .max_order_quantity = 10.0,
        .max_order_notional = 1000000.0,
        .max_price_deviation_ratio = 0.05,
        .max_requests_per_window = 1,
        .rate_limit_window_ms = 1000,
        .enable_self_trade_prevention = false,
    });
    engine.update_top_of_book("BTCUSDT", 9990.0, 10010.0);
    engine.set_account_state("alice", "BTCUSDT", {.quote_balance = 1000000.0, .base_position = 10.0});

    std::uint64_t sequence = 0;
    for (auto _ : state)
    {
        auto decision = engine.evaluate({
            .order_id = "rate-" + std::to_string(++sequence),
            .user_id = "alice",
            .symbol = "BTCUSDT",
            .source_ip = "10.0.0.1",
            .side = pre_trade_risk_engine::Side::kBuy,
            .type = pre_trade_risk_engine::OrderType::kLimit,
            .price = 10000.0,
            .quantity = 0.5,
            .timestamp_ms = 100,
        });
        benchmark::DoNotOptimize(&decision);
    }
}

BENCHMARK(BM_RiskEvaluationAccepted)->Arg(1 << 12);
BENCHMARK(BM_RateLimitRejections)->Arg(1 << 12);
BENCHMARK_MAIN();
