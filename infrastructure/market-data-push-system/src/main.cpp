#include <iostream>

#include "market_data_push_system/market_data_push_system.hpp"

int main()
{
    market_data_push_system::CandleAggregator aggregator(60'000);
    market_data_push_system::DepthBook book("BTCUSDT");
    market_data_push_system::MarketDataBroadcaster broadcaster;

    const auto candle =
        aggregator.add_trade({.symbol = "BTCUSDT", .price = 100.0, .quantity = 0.5, .event_time_ms = 60'000});
    const auto delta = book.update(market_data_push_system::Side::kBid, 99.5, 3.0);
    const auto message = broadcaster.publish("trades.BTCUSDT", "price=100.0");

    const auto summary = market_data_push_system::module_summary();
    std::cout << summary.module_name << " market_data_components=" << summary.market_data_components
              << " close=" << candle.close << " delta_price=" << delta.price << " sequence=" << message.sequence
              << '\n';
    return 0;
}
