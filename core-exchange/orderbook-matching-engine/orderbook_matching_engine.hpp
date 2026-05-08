#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "spsc_ring_queue.hpp"
#include "broadcaster.hpp"
#include "depth_book.hpp"
#include "object_pool.hpp"

namespace orderbook_matching_engine
{

enum class Side
{
    kBuy,
    kSell,
};

enum class OrderType
{
    kLimit,
    kMarket,
};

enum class TimeInForce
{
    kGtc,
    kIoc,
    kFok,
    kGtx,
};

enum class OrderStatus
{
    kAccepted,
    kPartiallyFilled,
    kFilled,
    kCancelled,
    kRejected,
};

enum class RejectReason
{
    kNone,
    kDuplicateOrderId,
    kWouldCrossPostOnly,
    kInsufficientLiquidity,
    kMarketOrderWouldRest,
    kOrderNotFound,
};

struct OrderRequest
{
    std::string order_id;
    Side side = Side::kBuy;
    OrderType type = OrderType::kLimit;
    TimeInForce tif = TimeInForce::kGtc;
    double price = 0.0;
    double quantity = 0.0;
    std::uint64_t timestamp = 0;
};

struct Trade
{
    std::string maker_order_id;
    std::string taker_order_id;
    double price = 0.0;
    double quantity = 0.0;
};

struct MatchResult
{
    bool accepted = false;
    OrderStatus final_status = OrderStatus::kRejected;
    RejectReason reject_reason = RejectReason::kNone;
    double executed_quantity = 0.0;
    double remaining_quantity = 0.0;
    std::vector<Trade> trades;
};

struct RestingOrderView
{
    std::string order_id;
    Side side = Side::kBuy;
    double price = 0.0;
    double open_quantity = 0.0;
};

struct EngineEvent
{
    std::string topic;
    std::string payload;
};

struct ModuleSummary
{
    std::string module_name;
    std::uint32_t order_features;
    std::uint32_t infrastructure_reuse_points;
};

class OrderbookMatchingEngine
{
  public:
    explicit OrderbookMatchingEngine(std::string symbol, std::size_t event_queue_capacity = 4096);

    [[nodiscard]] MatchResult submit(const OrderRequest &request);
    [[nodiscard]] MatchResult cancel(const std::string &order_id, std::uint64_t timestamp);
    [[nodiscard]] market_data_push_system::DepthSnapshot snapshot(std::size_t depth) const;
    [[nodiscard]] std::vector<market_data_push_system::BroadcastMessage> replay_market_data(
        const std::string &topic, std::uint64_t sequence_after) const;
    [[nodiscard]] std::optional<EngineEvent> poll_event();
    [[nodiscard]] std::vector<RestingOrderView> resting_orders() const;
    [[nodiscard]] std::size_t live_order_count() const;

  private:
    struct RestingOrder;
    using OrderQueue = std::deque<RestingOrder *>;
    using AskBook = std::map<double, OrderQueue, std::less<double>>;
    using BidBook = std::map<double, OrderQueue, std::greater<double>>;

    [[nodiscard]] MatchResult handle_limit_order(const OrderRequest &request);
    [[nodiscard]] MatchResult handle_market_order(const OrderRequest &request);
    [[nodiscard]] bool can_fill_fully(const OrderRequest &request) const;
    [[nodiscard]] bool would_cross(const OrderRequest &request) const;
    [[nodiscard]] double best_bid() const;
    [[nodiscard]] double best_ask() const;
    [[nodiscard]] bool has_best_bid() const;
    [[nodiscard]] bool has_best_ask() const;
    void add_resting_order(const OrderRequest &request, double remaining_quantity);
    void remove_resting_order(RestingOrder *order, bool cancel_state);
    void match_against_asks(const OrderRequest &request, double &remaining_quantity, MatchResult &result);
    void match_against_bids(const OrderRequest &request, double &remaining_quantity, MatchResult &result);
    void emit_order_event(const std::string &order_id, OrderStatus status, RejectReason reason, double open_quantity);
    void emit_trade_event(const Trade &trade);
    void emit_depth_event();
    void publish_market_data(const std::string &topic, std::string payload);

    std::string symbol_;
    memory_pool::ObjectPool<RestingOrder> order_pool_;
    BidBook bids_;
    AskBook asks_;
    std::unordered_map<std::string, RestingOrder *> live_orders_;
    mutable market_data_push_system::DepthBook depth_book_;
    mutable market_data_push_system::MarketDataBroadcaster broadcaster_;
    lock_free_structures::SpscRingQueue<EngineEvent> event_queue_;
};

std::string project_name();

ModuleSummary module_summary();

} // namespace orderbook_matching_engine
