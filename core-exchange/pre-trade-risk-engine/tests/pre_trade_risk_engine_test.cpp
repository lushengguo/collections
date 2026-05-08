#include <gtest/gtest.h>

#include "pre_trade_risk_engine/pre_trade_risk_engine.hpp"

TEST(PreTradeRiskEngineTest, ProjectNameIsStable)
{
    EXPECT_EQ(pre_trade_risk_engine::project_name(), "pre_trade_risk_engine");
}

TEST(PreTradeRiskEngineTest, ModuleSummaryReportsChecksAndReuse)
{
    const auto summary = pre_trade_risk_engine::module_summary();
    EXPECT_EQ(summary.module_name, "pre_trade_risk_engine");
    EXPECT_EQ(summary.risk_checks, 6U);
    EXPECT_EQ(summary.infrastructure_reuse_points, 2U);
}

TEST(PreTradeRiskEngineTest, AcceptsOrderWithinConfiguredLimits)
{
    pre_trade_risk_engine::PreTradeRiskEngine engine;
    engine.configure_market({
        .symbol = "BTCUSDT",
        .max_order_quantity = 5.0,
        .max_order_notional = 500000.0,
        .max_price_deviation_ratio = 0.05,
        .max_requests_per_window = 10,
        .rate_limit_window_ms = 1000,
        .enable_self_trade_prevention = true,
    });
    engine.update_top_of_book("BTCUSDT", 9990.0, 10010.0);
    engine.set_account_state("alice", "BTCUSDT", {.quote_balance = 50000.0, .base_position = 2.0});

    const auto decision = engine.evaluate({
        .order_id = "ord-1",
        .user_id = "alice",
        .symbol = "BTCUSDT",
        .source_ip = "10.0.0.1",
        .side = pre_trade_risk_engine::Side::kBuy,
        .type = pre_trade_risk_engine::OrderType::kLimit,
        .price = 10005.0,
        .quantity = 1.0,
        .timestamp_ms = 100,
    });

    EXPECT_TRUE(decision.accepted);
    EXPECT_EQ(decision.rule_name, "accepted");
    EXPECT_DOUBLE_EQ(decision.reference_price, 10000.0);
}

TEST(PreTradeRiskEngineTest, RejectsOrderOutsidePriceBand)
{
    pre_trade_risk_engine::PreTradeRiskEngine engine;
    engine.configure_market({
        .symbol = "BTCUSDT",
        .max_order_quantity = 5.0,
        .max_order_notional = 500000.0,
        .max_price_deviation_ratio = 0.01,
        .max_requests_per_window = 10,
        .rate_limit_window_ms = 1000,
        .enable_self_trade_prevention = true,
    });
    engine.update_top_of_book("BTCUSDT", 10000.0, 10020.0);
    engine.set_account_state("alice", "BTCUSDT", {.quote_balance = 50000.0, .base_position = 2.0});

    const auto decision = engine.evaluate({
        .order_id = "ord-2",
        .user_id = "alice",
        .symbol = "BTCUSDT",
        .source_ip = "10.0.0.1",
        .side = pre_trade_risk_engine::Side::kBuy,
        .type = pre_trade_risk_engine::OrderType::kLimit,
        .price = 10350.0,
        .quantity = 1.0,
        .timestamp_ms = 100,
    });

    EXPECT_FALSE(decision.accepted);
    EXPECT_EQ(decision.reject_reason, pre_trade_risk_engine::RejectReason::kPriceBandExceeded);
}

TEST(PreTradeRiskEngineTest, RejectsBuyOrderWhenQuoteBalanceIsInsufficient)
{
    pre_trade_risk_engine::PreTradeRiskEngine engine;
    engine.configure_market({
        .symbol = "BTCUSDT",
        .max_order_quantity = 5.0,
        .max_order_notional = 500000.0,
        .max_price_deviation_ratio = 0.05,
        .max_requests_per_window = 10,
        .rate_limit_window_ms = 1000,
        .enable_self_trade_prevention = true,
    });
    engine.update_top_of_book("BTCUSDT", 10000.0, 10020.0);
    engine.set_account_state("alice", "BTCUSDT", {.quote_balance = 500.0, .base_position = 2.0});

    const auto decision = engine.evaluate({
        .order_id = "ord-3",
        .user_id = "alice",
        .symbol = "BTCUSDT",
        .source_ip = "10.0.0.1",
        .side = pre_trade_risk_engine::Side::kBuy,
        .type = pre_trade_risk_engine::OrderType::kLimit,
        .price = 10010.0,
        .quantity = 1.0,
        .timestamp_ms = 100,
    });

    EXPECT_FALSE(decision.accepted);
    EXPECT_EQ(decision.reject_reason, pre_trade_risk_engine::RejectReason::kInsufficientBalance);
}

TEST(PreTradeRiskEngineTest, RejectsAfterRateLimitIsExceeded)
{
    pre_trade_risk_engine::PreTradeRiskEngine engine;
    engine.configure_market({
        .symbol = "BTCUSDT",
        .max_order_quantity = 5.0,
        .max_order_notional = 500000.0,
        .max_price_deviation_ratio = 0.05,
        .max_requests_per_window = 2,
        .rate_limit_window_ms = 1000,
        .enable_self_trade_prevention = true,
    });
    engine.update_top_of_book("BTCUSDT", 9990.0, 10010.0);
    engine.set_account_state("alice", "BTCUSDT", {.quote_balance = 50000.0, .base_position = 2.0});

    const auto first = engine.evaluate({.order_id = "ord-4",
                                        .user_id = "alice",
                                        .symbol = "BTCUSDT",
                                        .source_ip = "10.0.0.1",
                                        .side = pre_trade_risk_engine::Side::kBuy,
                                        .type = pre_trade_risk_engine::OrderType::kLimit,
                                        .price = 10005.0,
                                        .quantity = 1.0,
                                        .timestamp_ms = 100});
    const auto second = engine.evaluate({.order_id = "ord-5",
                                         .user_id = "alice",
                                         .symbol = "BTCUSDT",
                                         .source_ip = "10.0.0.1",
                                         .side = pre_trade_risk_engine::Side::kBuy,
                                         .type = pre_trade_risk_engine::OrderType::kLimit,
                                         .price = 10005.0,
                                         .quantity = 1.0,
                                         .timestamp_ms = 200});
    const auto third = engine.evaluate({.order_id = "ord-6",
                                        .user_id = "alice",
                                        .symbol = "BTCUSDT",
                                        .source_ip = "10.0.0.1",
                                        .side = pre_trade_risk_engine::Side::kBuy,
                                        .type = pre_trade_risk_engine::OrderType::kLimit,
                                        .price = 10005.0,
                                        .quantity = 1.0,
                                        .timestamp_ms = 300});

    EXPECT_TRUE(first.accepted);
    EXPECT_TRUE(second.accepted);
    EXPECT_FALSE(third.accepted);
    EXPECT_EQ(third.reject_reason, pre_trade_risk_engine::RejectReason::kRateLimited);
}

TEST(PreTradeRiskEngineTest, SelfTradePreventionRejectsCrossingOwnLiquidity)
{
    pre_trade_risk_engine::PreTradeRiskEngine engine;
    engine.configure_market({
        .symbol = "BTCUSDT",
        .max_order_quantity = 5.0,
        .max_order_notional = 500000.0,
        .max_price_deviation_ratio = 0.05,
        .max_requests_per_window = 10,
        .rate_limit_window_ms = 1000,
        .enable_self_trade_prevention = true,
    });
    engine.update_top_of_book("BTCUSDT", 9990.0, 10010.0);
    engine.set_account_state("alice", "BTCUSDT", {.quote_balance = 50000.0, .base_position = 2.0});
    engine.track_resting_order(
        "BTCUSDT", {.user_id = "alice", .side = pre_trade_risk_engine::Side::kSell, .price = 10000.0, .quantity = 1.0});

    const auto decision = engine.evaluate({
        .order_id = "ord-7",
        .user_id = "alice",
        .symbol = "BTCUSDT",
        .source_ip = "10.0.0.1",
        .side = pre_trade_risk_engine::Side::kBuy,
        .type = pre_trade_risk_engine::OrderType::kLimit,
        .price = 10000.0,
        .quantity = 0.5,
        .timestamp_ms = 100,
    });

    EXPECT_FALSE(decision.accepted);
    EXPECT_EQ(decision.reject_reason, pre_trade_risk_engine::RejectReason::kSelfTradePrevented);
}

TEST(PreTradeRiskEngineTest, AuditLogCapturesEveryDecision)
{
    pre_trade_risk_engine::PreTradeRiskEngine engine;
    engine.configure_market({
        .symbol = "BTCUSDT",
        .max_order_quantity = 5.0,
        .max_order_notional = 500000.0,
        .max_price_deviation_ratio = 0.05,
        .max_requests_per_window = 10,
        .rate_limit_window_ms = 1000,
        .enable_self_trade_prevention = false,
    });
    engine.update_top_of_book("BTCUSDT", 9990.0, 10010.0);
    engine.set_account_state("alice", "BTCUSDT", {.quote_balance = 50000.0, .base_position = 2.0});

    (void)engine.evaluate({.order_id = "ord-8",
                           .user_id = "alice",
                           .symbol = "BTCUSDT",
                           .source_ip = "10.0.0.1",
                           .side = pre_trade_risk_engine::Side::kBuy,
                           .type = pre_trade_risk_engine::OrderType::kLimit,
                           .price = 10000.0,
                           .quantity = 1.0,
                           .timestamp_ms = 100});
    (void)engine.evaluate({.order_id = "ord-9",
                           .user_id = "alice",
                           .symbol = "BTCUSDT",
                           .source_ip = "10.0.0.1",
                           .side = pre_trade_risk_engine::Side::kSell,
                           .type = pre_trade_risk_engine::OrderType::kLimit,
                           .price = 10000.0,
                           .quantity = 5.0,
                           .timestamp_ms = 200});

    const auto audit = engine.audit_log();
    ASSERT_EQ(audit.size(), 2U);
    EXPECT_EQ(audit.front().order_id, "ord-8");
    EXPECT_EQ(audit.back().order_id, "ord-9");
}
