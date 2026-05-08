#include <iostream>

#include "orderbook_matching_engine/orderbook_matching_engine.hpp"

int main()
{
    orderbook_matching_engine::OrderbookMatchingEngine engine("BTCUSDT");
    const auto maker = engine.submit({
        .order_id = "maker-1",
        .side = orderbook_matching_engine::Side::kSell,
        .type = orderbook_matching_engine::OrderType::kLimit,
        .tif = orderbook_matching_engine::TimeInForce::kGtc,
        .price = 101.0,
        .quantity = 2.0,
        .timestamp = 1,
    });
    const auto taker = engine.submit({
        .order_id = "taker-1",
        .side = orderbook_matching_engine::Side::kBuy,
        .type = orderbook_matching_engine::OrderType::kMarket,
        .tif = orderbook_matching_engine::TimeInForce::kIoc,
        .price = 0.0,
        .quantity = 1.0,
        .timestamp = 2,
    });
    const auto summary = orderbook_matching_engine::module_summary();
    std::cout << summary.module_name << " features=" << summary.order_features << " maker_accepted=" << maker.accepted
              << " taker_trades=" << taker.trades.size() << '\n';
    return 0;
}
