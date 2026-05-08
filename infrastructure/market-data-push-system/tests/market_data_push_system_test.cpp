#include <gtest/gtest.h>

#include "market_data_push_system/market_data_push_system.hpp"

TEST(MarketDataPushSystemTest, ProjectNameIsStable)
{
    EXPECT_EQ(market_data_push_system::project_name(), "market_data_push_system");
}

TEST(MarketDataPushSystemTest, CandleAggregatorBuildsOhlcvFromTrades)
{
    market_data_push_system::CandleAggregator aggregator(60'000);

    aggregator.add_trade({.symbol = "BTCUSDT", .price = 100.0, .quantity = 1.0, .event_time_ms = 60'010});
    aggregator.add_trade({.symbol = "BTCUSDT", .price = 105.0, .quantity = 2.0, .event_time_ms = 60'020});
    const auto candle =
        aggregator.add_trade({.symbol = "BTCUSDT", .price = 102.0, .quantity = 3.0, .event_time_ms = 60'030});

    EXPECT_DOUBLE_EQ(candle.open, 100.0);
    EXPECT_DOUBLE_EQ(candle.high, 105.0);
    EXPECT_DOUBLE_EQ(candle.low, 100.0);
    EXPECT_DOUBLE_EQ(candle.close, 102.0);
    EXPECT_DOUBLE_EQ(candle.volume, 6.0);
}

TEST(MarketDataPushSystemTest, DepthBookBuildsSnapshotAndDeltas)
{
    market_data_push_system::DepthBook book("BTCUSDT");
    const auto bid_delta = book.update(market_data_push_system::Side::kBid, 100.0, 3.5);
    const auto ask_delta = book.update(market_data_push_system::Side::kAsk, 101.0, 2.5);
    const auto snapshot = book.snapshot(5);

    EXPECT_EQ(bid_delta.price, 100.0);
    EXPECT_EQ(ask_delta.quantity, 2.5);
    ASSERT_EQ(snapshot.bids.size(), 1U);
    ASSERT_EQ(snapshot.asks.size(), 1U);
    EXPECT_DOUBLE_EQ(snapshot.bids.front().price, 100.0);
    EXPECT_DOUBLE_EQ(snapshot.asks.front().price, 101.0);
}

TEST(MarketDataPushSystemTest, BroadcasterSupportsPollingAndReplay)
{
    market_data_push_system::MarketDataBroadcaster broadcaster;
    const auto subscriber_id = broadcaster.subscribe("depth.BTCUSDT");

    const auto first = broadcaster.publish("depth.BTCUSDT", "delta-1");
    const auto second = broadcaster.publish("depth.BTCUSDT", "delta-2");

    const auto polled = broadcaster.poll(subscriber_id, 10);
    ASSERT_EQ(polled.size(), 2U);
    EXPECT_EQ(polled.front().payload, "delta-1");

    const auto replay = broadcaster.replay("depth.BTCUSDT", first.sequence);
    ASSERT_EQ(replay.size(), 1U);
    EXPECT_EQ(replay.front().sequence, second.sequence);
}

TEST(MarketDataPushSystemTest, ModuleSummaryReportsComponentCount)
{
    const auto summary = market_data_push_system::module_summary();
    EXPECT_EQ(summary.module_name, "market_data_push_system");
    EXPECT_EQ(summary.market_data_components, 3U);
}
