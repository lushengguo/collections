#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "broadcaster.hpp"
#include "depth_book.hpp"
#include "object_pool.hpp"
#include "spsc_ring_queue.hpp"

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
    // Client-visible order id that must be unique among live orders.
    std::string order_id;
    // Buy or sell intent.
    Side side = Side::kBuy;
    // Limit or market execution style.
    OrderType type = OrderType::kLimit;
    // Resting policy such as GTC, IOC, FOK, or GTX.
    TimeInForce tif = TimeInForce::kGtc;
    // Limit price; ignored for market orders.
    double price = 0.0;
    // Requested base quantity.
    double quantity = 0.0;
    // Monotonic time used for price-time priority and event ordering.
    std::uint64_t timestamp = 0;
};

struct Trade
{
    // Resting order that provided liquidity.
    std::string maker_order_id;
    // Aggressive order that consumed liquidity.
    std::string taker_order_id;
    // Execution price chosen by the matching engine.
    double price = 0.0;
    // Executed base quantity.
    double quantity = 0.0;
};

struct MatchResult
{
    // Whether the order was accepted into the matching workflow at all.
    bool accepted = false;
    // Final lifecycle state after matching completes.
    OrderStatus final_status = OrderStatus::kRejected;
    // Structured rejection reason when accepted is false.
    RejectReason reject_reason = RejectReason::kNone;
    // Total quantity executed across all fills.
    double executed_quantity = 0.0;
    // Quantity left open after matching and TIF handling.
    double remaining_quantity = 0.0;
    // Individual trades generated while matching this order.
    std::vector<Trade> trades;
};

struct RestingOrderView
{
    // Order id still resting on the book.
    std::string order_id;
    // Side of the resting liquidity.
    Side side = Side::kBuy;
    // Price level where the order is queued.
    double price = 0.0;
    // Remaining open quantity still available for matching.
    double open_quantity = 0.0;
};

struct EngineEvent
{
    // Logical stream name such as orders, trades, or depth.
    std::string topic;
    // Serialized payload emitted for downstream consumers.
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
    [[nodiscard]] MatchResult cancel(std::string_view order_id, std::uint64_t timestamp);
    [[nodiscard]] market_data_push_system::DepthSnapshot snapshot(std::size_t depth) const;
    [[nodiscard]] std::vector<market_data_push_system::BroadcastMessage> replay_market_data(
        std::string_view topic, std::uint64_t sequence_after) const;
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
    [[nodiscard]] static MatchResult make_rejected_result(RejectReason reason, double remaining_quantity);
    [[nodiscard]] static MatchResult make_accepted_result(double requested_quantity);
    void match_request(const OrderRequest &request, double &remaining_quantity, MatchResult &result);
    [[nodiscard]] MatchResult finalize_limit_result(const OrderRequest &request, double remaining_quantity,
                                                    MatchResult result);
    [[nodiscard]] MatchResult finalize_market_result(const OrderRequest &request, double remaining_quantity,
                                                     MatchResult result);
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
    void emit_order_event(std::string_view order_id, OrderStatus status, RejectReason reason, double open_quantity);
    void emit_trade_event(const Trade &trade);
    void emit_depth_event();
    void publish_market_data(std::string_view topic, std::string payload);

    std::string symbol_;
    memory_pool::ObjectPool<RestingOrder> order_pool_;
    BidBook bids_;
    AskBook asks_;
    std::unordered_map<std::string, RestingOrder *> live_orders_;
    mutable market_data_push_system::DepthBook depth_book_;
    mutable market_data_push_system::MarketDataBroadcaster broadcaster_;
    lock_free_structures::SpscRingQueue<EngineEvent> event_queue_;
};

[[nodiscard]] std::string project_name();

[[nodiscard]] ModuleSummary module_summary();

} // namespace orderbook_matching_engine
