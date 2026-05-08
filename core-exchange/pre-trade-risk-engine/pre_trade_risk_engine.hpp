#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "depth_book.hpp"
#include "spsc_ring_queue.hpp"

namespace pre_trade_risk_engine
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

enum class RejectReason
{
    kNone,
    kInvalidRequest,
    kUnknownSymbol,
    kPriceBandExceeded,
    kQuantityLimitExceeded,
    kNotionalLimitExceeded,
    kInsufficientBalance,
    kInsufficientPosition,
    kRateLimited,
    kSelfTradePrevented,
};

struct OrderRequest
{
    std::string order_id;
    std::string user_id;
    std::string symbol;
    std::string source_ip;
    Side side{Side::kBuy};
    OrderType type{OrderType::kLimit};
    double price{0.0};
    double quantity{0.0};
    std::int64_t timestamp_ms{0};
};

struct MarketRiskConfig
{
    std::string symbol;
    double max_order_quantity{0.0};
    double max_order_notional{0.0};
    double max_price_deviation_ratio{0.0};
    std::size_t max_requests_per_window{0};
    std::int64_t rate_limit_window_ms{0};
    bool enable_self_trade_prevention{true};
};

struct AccountRiskState
{
    double quote_balance{0.0};
    double base_position{0.0};
};

struct RestingOrderView
{
    std::string user_id;
    Side side{Side::kBuy};
    double price{0.0};
    double quantity{0.0};
};

struct RiskDecision
{
    bool accepted{false};
    RejectReason reject_reason{RejectReason::kNone};
    std::string rule_name;
    std::string order_id;
    std::string user_id;
    std::string symbol;
    double reference_price{0.0};
    double estimated_notional{0.0};
    std::int64_t timestamp_ms{0};
};

struct ModuleSummary
{
    std::string module_name;
    std::size_t risk_checks{0};
    std::size_t infrastructure_reuse_points{0};
};

class PreTradeRiskEngine
{
  public:
    explicit PreTradeRiskEngine(std::size_t audit_queue_capacity = 1024);

    void configure_market(MarketRiskConfig config);
    void update_top_of_book(std::string_view symbol, double best_bid, double best_ask);
    void set_account_state(std::string_view user_id, std::string_view symbol, AccountRiskState state);
    void track_resting_order(std::string_view symbol, RestingOrderView order);
    void clear_user_resting_orders(std::string_view symbol, std::string_view user_id);

    [[nodiscard]] RiskDecision evaluate(const OrderRequest &request);
    [[nodiscard]] std::vector<RiskDecision> audit_log() const;
    [[nodiscard]] std::size_t pending_audit_events() const noexcept;

  private:
    struct MarketState
    {
        explicit MarketState(MarketRiskConfig config_value) : config(std::move(config_value)), book(config.symbol)
        {
        }

        MarketRiskConfig config;
        market_data_push_system::DepthBook book;
        std::optional<double> best_bid;
        std::optional<double> best_ask;
    };

    [[nodiscard]] static std::string account_key(std::string_view user_id, std::string_view symbol);
    [[nodiscard]] double reference_price(const MarketState &market, const OrderRequest &request) const;
    [[nodiscard]] bool would_self_trade(const OrderRequest &request,
                                        const std::vector<RestingOrderView> &resting_orders) const;
    void flush_audit_queue();
    void enqueue_audit(RiskDecision decision);

    std::unordered_map<std::string, MarketState> markets_;
    std::unordered_map<std::string, AccountRiskState> accounts_;
    std::unordered_map<std::string, std::vector<RestingOrderView>> resting_orders_;
    std::unordered_map<std::string, std::vector<std::int64_t>> request_windows_;
    lock_free_structures::SpscRingQueue<RiskDecision> audit_queue_;
    std::vector<RiskDecision> audit_history_;
};

[[nodiscard]] ModuleSummary module_summary();

[[nodiscard]] std::string project_name();

} // namespace pre_trade_risk_engine
