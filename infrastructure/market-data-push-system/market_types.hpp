#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace market_data_push_system
{

enum class Side
{
    kBid,
    kAsk,
};

struct Trade
{
    std::string symbol;
    double price = 0.0;
    double quantity = 0.0;
    std::uint64_t event_time_ms = 0;
};

struct Candle
{
    std::string symbol;
    std::uint64_t open_time_ms = 0;
    std::uint64_t close_time_ms = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
};

struct BookLevel
{
    double price = 0.0;
    double quantity = 0.0;
};

struct DepthSnapshot
{
    std::string symbol;
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
};

struct DepthDelta
{
    Side side = Side::kBid;
    double price = 0.0;
    double quantity = 0.0;
};

struct BroadcastMessage
{
    std::uint64_t sequence = 0;
    std::string topic;
    std::string payload;
};

} // namespace market_data_push_system
