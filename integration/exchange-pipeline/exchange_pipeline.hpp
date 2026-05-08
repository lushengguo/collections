#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "account_clearing_system.hpp"
#include "market_types.hpp"
#include "orderbook_matching_engine.hpp"
#include "outbox.hpp"
#include "pre_trade_risk_engine.hpp"
#include "unified_access_gateway.hpp"

namespace exchange_pipeline
{

enum class WorkflowStatus
{
    kCompleted,
    kRouteRejected,
    kParseRejected,
    kRiskRejected,
    kClearingRejected,
    kMatchingRejected,
    kSettlementRejected,
    kInvariantViolation,
};

struct WorkflowResult
{
    WorkflowStatus status{WorkflowStatus::kCompleted};
    std::string error_detail;
    bool accepted{false};
    unified_access_gateway::RouteResult route_result;
    pre_trade_risk_engine::RiskDecision risk_decision;
    orderbook_matching_engine::MatchResult match_result;
    std::vector<account_clearing_system::SettlementResult> settlements;
    std::vector<market_data_push_system::BroadcastMessage> route_events;
    std::vector<market_data_push_system::BroadcastMessage> trade_events;
    std::vector<market_data_push_system::BroadcastMessage> depth_events;
    std::vector<distributed_consistency::OutboxMessage> outbox_messages;

    [[nodiscard]] bool ok() const noexcept
    {
        return status == WorkflowStatus::kCompleted;
    }
};

class ExchangePipeline
{
  public:
    explicit ExchangePipeline(std::string symbol);

    void configure_market(double best_bid, double best_ask, account_clearing_system::FeeSchedule fee_schedule,
                          pre_trade_risk_engine::MarketRiskConfig risk_config);
    void register_user(std::string_view api_key, std::string_view secret, std::string_view user_id,
                       const account_clearing_system::AccountSnapshot &account_snapshot,
                       std::size_t max_requests = 100000, std::int64_t window_ms = 1000);

    [[nodiscard]] WorkflowResult submit(const unified_access_gateway::GatewayRequest &request);
    [[nodiscard]] std::optional<account_clearing_system::AccountSnapshot> account(std::string_view user_id) const;
    [[nodiscard]] std::vector<distributed_consistency::OutboxMessage> pending_outbox(std::size_t max_items) const;
    [[nodiscard]] std::vector<pre_trade_risk_engine::RiskDecision> audit_log() const;
    [[nodiscard]] std::vector<market_data_push_system::BroadcastMessage> replay_market_data(std::string_view topic,
                                                                                            std::uint64_t after) const;
    [[nodiscard]] std::vector<market_data_push_system::BroadcastMessage> replay_route_events(std::uint64_t after) const;

  private:
    struct ParsedOrder
    {
        std::string order_id;
        std::string user_id;
        std::string symbol;
        std::string source_ip;
        orderbook_matching_engine::Side matching_side{orderbook_matching_engine::Side::kBuy};
        pre_trade_risk_engine::Side risk_side{pre_trade_risk_engine::Side::kBuy};
        orderbook_matching_engine::OrderType matching_type{orderbook_matching_engine::OrderType::kLimit};
        pre_trade_risk_engine::OrderType risk_type{pre_trade_risk_engine::OrderType::kLimit};
        orderbook_matching_engine::TimeInForce tif{orderbook_matching_engine::TimeInForce::kGtc};
        double price{0.0};
        double quantity{0.0};
        std::int64_t timestamp_ms{0};
    };

    struct ManagedOrder
    {
        std::string user_id;
        std::string symbol;
        orderbook_matching_engine::Side side{orderbook_matching_engine::Side::kBuy};
        std::string hold_id;
    };

    [[nodiscard]] std::optional<ParsedOrder> parse_order(const unified_access_gateway::ForwardedRequest &request) const;
    [[nodiscard]] double reserve_amount(const ParsedOrder &order) const;
    [[nodiscard]] static std::string hold_id_for(std::string_view order_id);
    [[nodiscard]] orderbook_matching_engine::OrderRequest to_matching_request(const ParsedOrder &order) const;
    [[nodiscard]] pre_trade_risk_engine::OrderRequest to_risk_request(const ParsedOrder &order) const;
    void sync_account_state(std::string_view user_id);
    void sync_resting_orders();
    void refresh_market_reference();
    [[nodiscard]] account_clearing_system::ClearingOperationResult release_residual_hold(std::string_view hold_id,
                                                                                         std::int64_t timestamp_ms);

    std::string symbol_;
    double best_bid_{0.0};
    double best_ask_{0.0};
    account_clearing_system::FeeSchedule fee_schedule_;
    unified_access_gateway::UnifiedAccessGateway gateway_;
    pre_trade_risk_engine::PreTradeRiskEngine risk_;
    orderbook_matching_engine::OrderbookMatchingEngine matching_;
    account_clearing_system::AccountClearingSystem clearing_;
    std::unordered_map<std::string, ManagedOrder> order_registry_;
    std::unordered_set<std::string> known_users_;
    std::unordered_set<std::string> resting_users_;
};

[[nodiscard]] std::string project_name();

} // namespace exchange_pipeline
