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
    // Client-visible order id under evaluation.
    std::string order_id;
    // User placing the order.
    std::string user_id;
    // Trading pair the order targets.
    std::string symbol;
    // Source IP used for per-user/per-IP throttling.
    std::string source_ip;
    // Buy or sell intent.
    Side side{Side::kBuy};
    // Limit or market style.
    OrderType type{OrderType::kLimit};
    // Candidate limit price.
    double price{0.0};
    // Requested base quantity.
    double quantity{0.0};
    // Event time used for rate limiting and audit history.
    std::int64_t timestamp_ms{0};
};

struct MarketRiskConfig
{
    // Trading pair this rule set applies to.
    std::string symbol;
    // Hard cap on per-order base quantity.
    double max_order_quantity{0.0};
    // Hard cap on per-order quote notional.
    double max_order_notional{0.0};
    // Max allowed relative deviation from current reference price.
    double max_price_deviation_ratio{0.0};
    // Max requests allowed inside one rate-limit window.
    std::size_t max_requests_per_window{0};
    // Window length for per-user/per-IP throttling.
    std::int64_t rate_limit_window_ms{0};
    // Whether crossing against own resting liquidity should be blocked.
    bool enable_self_trade_prevention{true};
};

struct AccountRiskState
{
    // Spendable quote balance available to support buy orders.
    double quote_balance{0.0};
    // Spendable base position available to support sell orders.
    double base_position{0.0};
};

struct RestingOrderView
{
    // Owner of the resting order.
    std::string user_id;
    // Side of the resting liquidity.
    Side side{Side::kBuy};
    // Price level of the resting order.
    double price{0.0};
    // Remaining quantity still resting.
    double quantity{0.0};
};

struct RiskDecision
{
    // Whether the order passed all risk checks.
    bool accepted{false};
    // Structured failure reason when accepted is false.
    RejectReason reject_reason{RejectReason::kNone};
    // Name of the rule that accepted or rejected the request.
    std::string rule_name;
    // Echoed order id for audit correlation.
    std::string order_id;
    // Echoed user id for audit correlation.
    std::string user_id;
    // Echoed symbol for audit correlation.
    std::string symbol;
    // Price reference used for notional and price-band checks.
    double reference_price{0.0};
    // Estimated quote notional evaluated by risk.
    double estimated_notional{0.0};
    // Decision timestamp.
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

        // Static risk rules configured for this symbol.
        MarketRiskConfig config;
        // Lightweight depth view used to derive reference prices.
        market_data_push_system::DepthBook book;
        // Cached top bid from the latest market update.
        std::optional<double> best_bid;
        // Cached top ask from the latest market update.
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
