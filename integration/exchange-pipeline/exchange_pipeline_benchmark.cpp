#include <benchmark/benchmark.h>

#include "exchange_pipeline.hpp"

namespace
{

unified_access_gateway::GatewayRequest make_request(std::string request_id, std::string user_id, std::string api_key,
                                                    std::string secret, std::string payload, std::int64_t timestamp_ms)
{
    const auto signature = unified_access_gateway::expected_signature(api_key, secret, request_id);
    return {
        .request_id = std::move(request_id),
        .user_id = std::move(user_id),
        .api_key = std::move(api_key),
        .signature = signature,
        .path = "/v1/orders/place",
        .payload = std::move(payload),
        .protocol = unified_access_gateway::Protocol::kRest,
        .timestamp_ms = timestamp_ms,
    };
}

void configure_pipeline(exchange_pipeline::ExchangePipeline &pipeline)
{
    pipeline.configure_market(99.0, 101.0, {.maker_fee_bps = 2.0, .taker_fee_bps = 5.0},
                              {
                                  .symbol = "BTCUSDT",
                                  .max_order_quantity = 10.0,
                                  .max_order_notional = 1000000.0,
                                  .max_price_deviation_ratio = 0.05,
                                  .max_requests_per_window = 100000,
                                  .rate_limit_window_ms = 1000,
                                  .enable_self_trade_prevention = true,
                              });
    pipeline.register_user("seller-key", "seller-secret", "maker",
                           {.available_quote = 0.0, .available_base = 1.0, .base_cost = 80.0});
    pipeline.register_user("buyer-key", "buyer-secret", "taker", {.available_quote = 1000.0});
}

} // namespace

static void BM_EndToEndRestThenCross(benchmark::State &state)
{
    std::uint64_t sequence = 0;
    for (auto _ : state)
    {
        exchange_pipeline::ExchangePipeline pipeline("BTCUSDT");
        configure_pipeline(pipeline);
        const auto maker_sequence = sequence;
        ++sequence;
        const auto maker = pipeline.submit(
            make_request("req-maker-" + std::to_string(maker_sequence), "maker", "seller-key", "seller-secret",
                         "order_id=ask-" + std::to_string(maker_sequence) +
                             ";symbol=BTCUSDT;side=sell;type=limit;tif=gtc;price=100.0;quantity=1.0;source_ip=10.0.0.1",
                         static_cast<std::int64_t>(sequence)));
        benchmark::DoNotOptimize(&maker);

        const auto taker_sequence = sequence;
        ++sequence;
        const auto taker = pipeline.submit(
            make_request("req-taker-" + std::to_string(taker_sequence), "taker", "buyer-key", "buyer-secret",
                         "order_id=buy-" + std::to_string(taker_sequence) +
                             ";symbol=BTCUSDT;side=buy;type=limit;tif=ioc;price=100.0;quantity=1.0;source_ip=10.0.0.2",
                         static_cast<std::int64_t>(sequence)));
        benchmark::DoNotOptimize(&taker);
    }

    state.SetItemsProcessed(state.iterations() * 2);
}

BENCHMARK(BM_EndToEndRestThenCross)->Arg(1);
BENCHMARK_MAIN();
