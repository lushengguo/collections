#include <gtest/gtest.h>

#include "orderbook_matching_engine/orderbook_matching_engine.hpp"

TEST(OrderbookMatchingEngineTest, ProjectNameIsStable)
{
    EXPECT_EQ(orderbook_matching_engine::project_name(), "orderbook_matching_engine");
}

TEST(OrderbookMatchingEngineTest, ModuleSummaryReportsFeatureAndReuseCounts)
{
    const auto summary = orderbook_matching_engine::module_summary();
    EXPECT_EQ(summary.module_name, "orderbook_matching_engine");
    EXPECT_EQ(summary.order_features, 5U);
    EXPECT_EQ(summary.infrastructure_reuse_points, 3U);
}

TEST(OrderbookMatchingEngineTest, AcceptsRestingLimitOrderAndBuildsSnapshot)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto result = engine.submit({
        .order_id = "buy-1",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 100.0,
        .quantity = 3.0,
        .timestamp = 1,
    });

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.final_status, orderbook_matching_engine::OrderStatus::kAccepted);
    const auto snapshot = engine.snapshot(5);
    ASSERT_EQ(snapshot.bids.size(), 1U);
    EXPECT_DOUBLE_EQ(snapshot.bids.front().price, 100.0);
    EXPECT_DOUBLE_EQ(snapshot.bids.front().quantity, 3.0);
}

TEST(OrderbookMatchingEngineTest, MatchesCrossingLimitOrdersWithPriceTimePriority)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto sell_one = engine.submit({
        .order_id = "sell-1",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 1.5,
        .timestamp = 1,
    });
    const auto sell_two = engine.submit({
        .order_id = "sell-2",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 2.0,
        .timestamp = 2,
    });
    ASSERT_TRUE(sell_one.accepted);
    ASSERT_TRUE(sell_two.accepted);

    const auto result = engine.submit({
        .order_id = "buy-1",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 2.0,
        .timestamp = 3,
    });

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.trades.size(), 2U);
    EXPECT_EQ(result.trades[0].maker_order_id, "sell-1");
    EXPECT_EQ(result.trades[1].maker_order_id, "sell-2");
    const auto snapshot = engine.snapshot(5);
    ASSERT_EQ(snapshot.asks.size(), 1U);
    EXPECT_DOUBLE_EQ(snapshot.asks.front().quantity, 1.5);
}

TEST(OrderbookMatchingEngineTest, MarketOrderConsumesAvailableLiquidityWithoutResting)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto sell_one = engine.submit({
        .order_id = "sell-1",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 1.0,
        .timestamp = 1,
    });
    ASSERT_TRUE(sell_one.accepted);

    const auto result = engine.submit({
        .order_id = "buy-mkt",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kMarket,
        .tif = orderbook_matching_engine::TimeInForce::kIoc,
        .price = 0.0,
        .quantity = 2.0,
        .timestamp = 2,
    });

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.final_status, orderbook_matching_engine::OrderStatus::kPartiallyFilled);
    EXPECT_DOUBLE_EQ(result.executed_quantity, 1.0);
    EXPECT_DOUBLE_EQ(result.remaining_quantity, 1.0);
    EXPECT_TRUE(engine.snapshot(5).asks.empty());
}

TEST(OrderbookMatchingEngineTest, FokRejectsWhenLiquidityIsInsufficient)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto sell_one = engine.submit({
        .order_id = "sell-1",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 1.0,
        .timestamp = 1,
    });
    ASSERT_TRUE(sell_one.accepted);

    const auto result = engine.submit({
        .order_id = "buy-fok",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kFok,
        .price = 102.0,
        .quantity = 2.0,
        .timestamp = 2,
    });

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, orderbook_matching_engine::RejectReason::kInsufficientLiquidity);
    EXPECT_EQ(engine.live_order_count(), 1U);
}

TEST(OrderbookMatchingEngineTest, FokFillsCompletelyWhenLiquidityIsSufficient)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto sell_one = engine.submit({
        .order_id = "sell-1",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 1.0,
        .timestamp = 1,
    });
    const auto sell_two = engine.submit({
        .order_id = "sell-2",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.5,
        .quantity = 1.0,
        .timestamp = 2,
    });
    ASSERT_TRUE(sell_one.accepted);
    ASSERT_TRUE(sell_two.accepted);

    const auto result = engine.submit({
        .order_id = "buy-fok",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kFok,
        .price = 102.0,
        .quantity = 2.0,
        .timestamp = 3,
    });

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.final_status, orderbook_matching_engine::OrderStatus::kFilled);
    EXPECT_DOUBLE_EQ(result.executed_quantity, 2.0);
    EXPECT_TRUE(engine.snapshot(5).asks.empty());
}

TEST(OrderbookMatchingEngineTest, GtxRejectsCrossingPostOnlyOrder)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto sell_one = engine.submit({
        .order_id = "sell-1",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 1.0,
        .timestamp = 1,
    });
    ASSERT_TRUE(sell_one.accepted);

    const auto result = engine.submit({
        .order_id = "buy-gtx",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtx,
        .price = 101.0,
        .quantity = 1.0,
        .timestamp = 2,
    });

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, orderbook_matching_engine::RejectReason::kWouldCrossPostOnly);
}

TEST(OrderbookMatchingEngineTest, IocDoesNotRestResidualLiquidity)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");

    const auto result = engine.submit({
        .order_id = "buy-ioc",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kIoc,
        .price = 100.0,
        .quantity = 2.0,
        .timestamp = 1,
    });

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.final_status, orderbook_matching_engine::OrderStatus::kCancelled);
    EXPECT_EQ(engine.live_order_count(), 0U);
    EXPECT_TRUE(engine.snapshot(5).bids.empty());
}

TEST(OrderbookMatchingEngineTest, DuplicateOrderIdIsRejected)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto first = engine.submit({
        .order_id = "dup-1",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 100.0,
        .quantity = 1.0,
        .timestamp = 1,
    });
    const auto second = engine.submit({
        .order_id = "dup-1",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 1.0,
        .timestamp = 2,
    });

    EXPECT_TRUE(first.accepted);
    EXPECT_FALSE(second.accepted);
    EXPECT_EQ(second.reject_reason, orderbook_matching_engine::RejectReason::kDuplicateOrderId);
    EXPECT_EQ(engine.live_order_count(), 1U);
}

TEST(OrderbookMatchingEngineTest, CancelRemovesRestingOrder)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto buy_one = engine.submit({
        .order_id = "buy-1",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 100.0,
        .quantity = 1.0,
        .timestamp = 1,
    });
    ASSERT_TRUE(buy_one.accepted);

    const auto result = engine.cancel("buy-1", 2);
    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(result.final_status, orderbook_matching_engine::OrderStatus::kCancelled);
    EXPECT_TRUE(engine.snapshot(5).bids.empty());
}

TEST(OrderbookMatchingEngineTest, CancelMissingOrderReturnsNotFound)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto result = engine.cancel("missing", 1);

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, orderbook_matching_engine::RejectReason::kOrderNotFound);
}

TEST(OrderbookMatchingEngineTest, MarketOrderRejectsWhenNoLiquidityAndWouldRest)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto result = engine.submit({
        .order_id = "buy-mkt",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kMarket,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 0.0,
        .quantity = 1.0,
        .timestamp = 1,
    });

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, orderbook_matching_engine::RejectReason::kMarketOrderWouldRest);
}

TEST(OrderbookMatchingEngineTest, RestingOrdersReturnsVisibleOpenOrders)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto buy = engine.submit({
        .order_id = "buy-1",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 100.0,
        .quantity = 1.0,
        .timestamp = 1,
    });
    const auto sell = engine.submit({
        .order_id = "sell-1",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 102.0,
        .quantity = 2.0,
        .timestamp = 2,
    });
    ASSERT_TRUE(buy.accepted);
    ASSERT_TRUE(sell.accepted);

    const auto resting = engine.resting_orders();
    ASSERT_EQ(resting.size(), 2U);
    EXPECT_EQ(resting[0].order_id, "buy-1");
    EXPECT_EQ(resting[1].order_id, "sell-1");
}

TEST(OrderbookMatchingEngineTest, ReplaysMarketDataAndExposesEventQueue)
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto sell_one = engine.submit({
        .order_id = "sell-1",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 1.0,
        .timestamp = 1,
    });
    const auto buy_one = engine.submit({
        .order_id = "buy-1",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 1.0,
        .timestamp = 2,
    });
    ASSERT_TRUE(sell_one.accepted);
    ASSERT_TRUE(buy_one.accepted);

    const auto replay = engine.replay_market_data("trades.BTCUSDT", 0);
    ASSERT_EQ(replay.size(), 1U);
    EXPECT_NE(replay.front().payload.find("maker=sell-1"), std::string::npos);

    bool saw_trade_event = false;
    while (const auto event = engine.poll_event())
    {
        if (event->topic == "trades")
        {
            saw_trade_event = true;
            break;
        }
    }
    EXPECT_TRUE(saw_trade_event);
}
