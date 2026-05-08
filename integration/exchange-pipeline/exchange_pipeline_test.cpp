#include <gtest/gtest.h>

#include <string_view>

#include "exchange_pipeline.hpp"

namespace
{

unified_access_gateway::GatewayRequest make_request(std::string_view request_id, std::string_view user_id,
                                                    std::string_view api_key, std::string_view secret,
                                                    std::string_view payload, std::int64_t timestamp_ms)
{
    const auto signature = unified_access_gateway::expected_signature(api_key, secret, request_id);
    return {
        .request_id = std::string(request_id),
        .user_id = std::string(user_id),
        .api_key = std::string(api_key),
        .signature = signature,
        .path = "/v1/orders/place",
        .payload = std::string(payload),
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
                                  .max_requests_per_window = 10000,
                                  .rate_limit_window_ms = 1000,
                                  .enable_self_trade_prevention = true,
                              });
    pipeline.register_user("seller-key", "seller-secret", "maker",
                           {.available_quote = 0.0, .available_base = 1.0, .base_cost = 80.0});
    pipeline.register_user("buyer-key", "buyer-secret", "taker", {.available_quote = 1000.0});
}

} // namespace

TEST(ExchangePipelineTest, GatewayRiskMatchingClearingAndMarketDataFormHappyPath)
{
    exchange_pipeline::ExchangePipeline pipeline("BTCUSDT");
    configure_pipeline(pipeline);

    const auto maker = pipeline.submit(make_request(
        "req-maker", "maker", "seller-key", "seller-secret",
        "order_id=ask-1;symbol=BTCUSDT;side=sell;type=limit;tif=gtc;price=100.0;quantity=1.0;source_ip=10.0.0.1", 1));
    EXPECT_EQ(maker.status, exchange_pipeline::WorkflowStatus::kCompleted);
    ASSERT_TRUE(maker.route_result.accepted);
    ASSERT_TRUE(maker.risk_decision.accepted);
    ASSERT_TRUE(maker.match_result.accepted);
    EXPECT_TRUE(maker.match_result.trades.empty());

    const auto taker = pipeline.submit(make_request(
        "req-taker", "taker", "buyer-key", "buyer-secret",
        "order_id=buy-1;symbol=BTCUSDT;side=buy;type=limit;tif=ioc;price=100.0;quantity=1.0;source_ip=10.0.0.2", 2));
    EXPECT_EQ(taker.status, exchange_pipeline::WorkflowStatus::kCompleted);
    ASSERT_TRUE(taker.route_result.accepted);
    ASSERT_TRUE(taker.risk_decision.accepted);
    ASSERT_TRUE(taker.match_result.accepted);
    ASSERT_EQ(taker.match_result.trades.size(), 1U);
    ASSERT_EQ(taker.settlements.size(), 1U);
    EXPECT_TRUE(taker.settlements.front().ok());

    const auto buyer = pipeline.account("taker");
    const auto seller = pipeline.account("maker");
    ASSERT_TRUE(buyer.has_value());
    ASSERT_TRUE(seller.has_value());
    EXPECT_DOUBLE_EQ(buyer->available_quote, 899.95);
    EXPECT_DOUBLE_EQ(buyer->frozen_quote, 0.0);
    EXPECT_DOUBLE_EQ(buyer->available_base, 1.0);
    EXPECT_DOUBLE_EQ(seller->available_quote, 99.98);
    EXPECT_DOUBLE_EQ(seller->available_base, 0.0);
    EXPECT_DOUBLE_EQ(seller->frozen_base, 0.0);
    EXPECT_DOUBLE_EQ(seller->realized_pnl, 19.98);

    const auto outbox = pipeline.pending_outbox(8);
    ASSERT_EQ(outbox.size(), 1U);
    EXPECT_EQ(outbox.front().topic, "clearing.settlement");

    const auto trades = pipeline.replay_market_data("trades.BTCUSDT", 0);
    const auto depth = pipeline.replay_market_data("depth.BTCUSDT", 0);
    const auto routes = pipeline.replay_route_events(0);
    ASSERT_EQ(trades.size(), 1U);
    EXPECT_EQ(trades.front().topic, "trades.BTCUSDT");
    EXPECT_FALSE(depth.empty());
    ASSERT_EQ(routes.size(), 2U);
    EXPECT_EQ(routes.front().topic, "gateway.routes");

    const auto audit = pipeline.audit_log();
    ASSERT_EQ(audit.size(), 2U);
    EXPECT_TRUE(audit.front().accepted);
    EXPECT_TRUE(audit.back().accepted);
}

TEST(ExchangePipelineTest, RiskRejectStopsBeforeFundsAreReservedOrMatched)
{
    exchange_pipeline::ExchangePipeline pipeline("BTCUSDT");
    configure_pipeline(pipeline);

    const auto rejected = pipeline.submit(make_request(
        "req-bad", "taker", "buyer-key", "buyer-secret",
        "order_id=buy-bad;symbol=BTCUSDT;side=buy;type=limit;tif=gtc;price=200.0;quantity=1.0;source_ip=10.0.0.2", 3));

    EXPECT_EQ(rejected.status, exchange_pipeline::WorkflowStatus::kRiskRejected);
    ASSERT_TRUE(rejected.route_result.accepted);
    EXPECT_FALSE(rejected.risk_decision.accepted);
    EXPECT_EQ(rejected.risk_decision.reject_reason, pre_trade_risk_engine::RejectReason::kPriceBandExceeded);
    EXPECT_TRUE(rejected.match_result.trades.empty());
    EXPECT_TRUE(rejected.outbox_messages.empty());

    const auto buyer = pipeline.account("taker");
    ASSERT_TRUE(buyer.has_value());
    EXPECT_DOUBLE_EQ(buyer->available_quote, 1000.0);
    EXPECT_DOUBLE_EQ(buyer->frozen_quote, 0.0);
    EXPECT_TRUE(pipeline.replay_market_data("trades.BTCUSDT", 0).empty());
}

TEST(ExchangePipelineTest, MalformedRequestBecomesTypedParseRejection)
{
    exchange_pipeline::ExchangePipeline pipeline("BTCUSDT");
    configure_pipeline(pipeline);

    const auto malformed = pipeline.submit(make_request(
        "req-bad-payload", "taker", "buyer-key", "buyer-secret",
        "order_id=buy-bad;symbol=BTCUSDT;side=buy;type=limit;tif=gtc;quantity=oops;source_ip=10.0.0.2", 4));

    EXPECT_EQ(malformed.status, exchange_pipeline::WorkflowStatus::kParseRejected);
    EXPECT_FALSE(malformed.risk_decision.accepted);
    EXPECT_EQ(malformed.risk_decision.reject_reason, pre_trade_risk_engine::RejectReason::kInvalidRequest);
    EXPECT_TRUE(malformed.match_result.trades.empty());
}
