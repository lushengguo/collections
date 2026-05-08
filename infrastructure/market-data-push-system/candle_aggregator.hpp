#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

#include "market_types.hpp"

namespace market_data_push_system
{

class CandleAggregator
{
  public:
    explicit CandleAggregator(std::uint64_t interval_ms) : interval_ms_(interval_ms)
    {
        if (interval_ms_ == 0)
        {
            throw std::invalid_argument("CandleAggregator interval must be greater than zero");
        }
    }

    Candle add_trade(const Trade &trade)
    {
        const auto bucket_time = bucket_open_time(trade.event_time_ms);
        const auto key = make_key(trade.symbol, bucket_time);
        auto iterator = candles_.find(key);

        if (iterator == candles_.end())
        {
            iterator = candles_
                           .emplace(key,
                                    Candle{
                                        .symbol = trade.symbol,
                                        .open_time_ms = bucket_time,
                                        .close_time_ms = bucket_time + interval_ms_ - 1,
                                        .open = trade.price,
                                        .high = trade.price,
                                        .low = trade.price,
                                        .close = trade.price,
                                        .volume = trade.quantity,
                                    })
                           .first;
            return iterator->second;
        }

        Candle &candle = iterator->second;
        candle.high = std::max(candle.high, trade.price);
        candle.low = std::min(candle.low, trade.price);
        candle.close = trade.price;
        candle.volume += trade.quantity;
        return candle;
    }

    [[nodiscard]] std::optional<Candle> latest(std::string_view symbol, std::uint64_t event_time_ms) const
    {
        const auto bucket_time = bucket_open_time(event_time_ms);
        const auto key = make_key(symbol, bucket_time);
        auto iterator = candles_.find(key);
        if (iterator == candles_.end())
        {
            return std::nullopt;
        }

        return iterator->second;
    }

  private:
    [[nodiscard]] std::uint64_t bucket_open_time(std::uint64_t event_time_ms) const noexcept
    {
        return (event_time_ms / interval_ms_) * interval_ms_;
    }

    [[nodiscard]] static std::string make_key(std::string_view symbol, std::uint64_t open_time_ms)
    {
        return std::string(symbol) + ':' + std::to_string(open_time_ms);
    }

    std::uint64_t interval_ms_;
    std::unordered_map<std::string, Candle> candles_;
};

} // namespace market_data_push_system
