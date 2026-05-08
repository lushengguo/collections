#include "pre_trade_risk_engine/pre_trade_risk_engine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace pre_trade_risk_engine
{

namespace
{

market_data_push_system::Side bid_side()
{
    return market_data_push_system::Side::kBid;
}

market_data_push_system::Side ask_side()
{
    return market_data_push_system::Side::kAsk;
}

} // namespace

PreTradeRiskEngine::PreTradeRiskEngine(std::size_t audit_queue_capacity) : audit_queue_(audit_queue_capacity)
{
}

void PreTradeRiskEngine::configure_market(MarketRiskConfig config)
{
    if (config.symbol.empty())
    {
        throw std::invalid_argument("MarketRiskConfig symbol must not be empty");
    }
    if (config.max_order_quantity <= 0.0 || config.max_order_notional <= 0.0)
    {
        throw std::invalid_argument("MarketRiskConfig limits must be positive");
    }
    if (config.max_requests_per_window == 0 || config.rate_limit_window_ms <= 0)
    {
        throw std::invalid_argument("MarketRiskConfig rate limit must be positive");
    }

    const auto symbol = config.symbol;
    markets_.insert_or_assign(symbol, MarketState{std::move(config)});
}

void PreTradeRiskEngine::update_top_of_book(std::string_view symbol, double best_bid, double best_ask)
{
    auto market_it = markets_.find(std::string(symbol));
    if (market_it == markets_.end())
    {
        throw std::invalid_argument("Market must be configured before price updates");
    }

    auto &market = market_it->second;
    if (market.best_bid.has_value())
    {
        const auto removed = market.book.update(bid_side(), *market.best_bid, 0.0);
        (void)removed;
    }
    if (market.best_ask.has_value())
    {
        const auto removed = market.book.update(ask_side(), *market.best_ask, 0.0);
        (void)removed;
    }

    market.best_bid = best_bid;
    market.best_ask = best_ask;

    const auto bid_delta = market.book.update(bid_side(), best_bid, 1.0);
    const auto ask_delta = market.book.update(ask_side(), best_ask, 1.0);
    (void)bid_delta;
    (void)ask_delta;
}

void PreTradeRiskEngine::set_account_state(std::string_view user_id, std::string_view symbol, AccountRiskState state)
{
    accounts_[account_key(user_id, symbol)] = state;
}

void PreTradeRiskEngine::track_resting_order(std::string_view symbol, RestingOrderView order)
{
    resting_orders_[std::string(symbol)].push_back(std::move(order));
}

void PreTradeRiskEngine::clear_user_resting_orders(std::string_view symbol, std::string_view user_id)
{
    auto &orders = resting_orders_[std::string(symbol)];
    orders.erase(std::remove_if(orders.begin(), orders.end(),
                                [&](const auto &resting_order) { return resting_order.user_id == user_id; }),
                 orders.end());
}

RiskDecision PreTradeRiskEngine::evaluate(const OrderRequest &request)
{
    auto make_decision = [&](bool accepted, RejectReason reject_reason, std::string rule_name) {
        const auto market_it = markets_.find(request.symbol);
        const auto ref_price = market_it == markets_.end() ? 0.0 : reference_price(market_it->second, request);
        const auto notional = ref_price * request.quantity;
        return RiskDecision{
            .accepted = accepted,
            .reject_reason = reject_reason,
            .rule_name = std::move(rule_name),
            .order_id = request.order_id,
            .user_id = request.user_id,
            .symbol = request.symbol,
            .reference_price = ref_price,
            .estimated_notional = notional,
            .timestamp_ms = request.timestamp_ms,
        };
    };

    if (request.order_id.empty() || request.user_id.empty() || request.symbol.empty() || request.quantity <= 0.0 ||
        request.timestamp_ms <= 0 || request.source_ip.empty())
    {
        auto decision = make_decision(false, RejectReason::kInvalidRequest, "request_shape");
        enqueue_audit(decision);
        flush_audit_queue();
        return decision;
    }

    const auto market_it = markets_.find(request.symbol);
    if (market_it == markets_.end())
    {
        auto decision = make_decision(false, RejectReason::kUnknownSymbol, "symbol_configured");
        enqueue_audit(decision);
        flush_audit_queue();
        return decision;
    }

    const auto &market = market_it->second;
    const auto ref_price = reference_price(market, request);
    const auto notional = ref_price * request.quantity;
    const auto account_it = accounts_.find(account_key(request.user_id, request.symbol));
    const auto empty_account = AccountRiskState{};
    const auto &account = account_it == accounts_.end() ? empty_account : account_it->second;

    auto &request_window = request_windows_[request.user_id + "|" + request.source_ip];
    request_window.erase(std::remove_if(request_window.begin(), request_window.end(),
                                        [&](const auto window_timestamp) {
                                            return window_timestamp <
                                                   request.timestamp_ms - market.config.rate_limit_window_ms;
                                        }),
                         request_window.end());

    RiskDecision decision;
    if (request.quantity > market.config.max_order_quantity)
    {
        decision = make_decision(false, RejectReason::kQuantityLimitExceeded, "quantity_limit");
    }
    else if (notional > market.config.max_order_notional)
    {
        decision = make_decision(false, RejectReason::kNotionalLimitExceeded, "notional_limit");
    }
    else if (request.type == OrderType::kLimit && ref_price > 0.0 &&
             std::abs(request.price - ref_price) / ref_price > market.config.max_price_deviation_ratio)
    {
        decision = make_decision(false, RejectReason::kPriceBandExceeded, "price_band");
    }
    else if (request.side == Side::kBuy && account.quote_balance < notional)
    {
        decision = make_decision(false, RejectReason::kInsufficientBalance, "available_quote_balance");
    }
    else if (request.side == Side::kSell && account.base_position < request.quantity)
    {
        decision = make_decision(false, RejectReason::kInsufficientPosition, "available_base_position");
    }
    else if (request_window.size() >= market.config.max_requests_per_window)
    {
        decision = make_decision(false, RejectReason::kRateLimited, "user_ip_rate_limit");
    }
    else if (market.config.enable_self_trade_prevention && would_self_trade(request, resting_orders_[request.symbol]))
    {
        decision = make_decision(false, RejectReason::kSelfTradePrevented, "self_trade_prevention");
    }
    else
    {
        decision = make_decision(true, RejectReason::kNone, "accepted");
    }

    request_window.push_back(request.timestamp_ms);
    enqueue_audit(decision);
    flush_audit_queue();
    return decision;
}

std::vector<RiskDecision> PreTradeRiskEngine::audit_log() const
{
    return audit_history_;
}

std::size_t PreTradeRiskEngine::pending_audit_events() const noexcept
{
    return audit_queue_.size_approx();
}

std::string PreTradeRiskEngine::account_key(std::string_view user_id, std::string_view symbol)
{
    return std::string(user_id) + "|" + std::string(symbol);
}

double PreTradeRiskEngine::reference_price(const MarketState &market, const OrderRequest &request) const
{
    const auto snapshot = market.book.snapshot(1);
    if (!snapshot.bids.empty() && !snapshot.asks.empty())
    {
        return (snapshot.bids.front().price + snapshot.asks.front().price) / 2.0;
    }
    if (request.type == OrderType::kLimit && request.price > 0.0)
    {
        return request.price;
    }
    if (!snapshot.bids.empty())
    {
        return snapshot.bids.front().price;
    }
    if (!snapshot.asks.empty())
    {
        return snapshot.asks.front().price;
    }
    return 0.0;
}

bool PreTradeRiskEngine::would_self_trade(const OrderRequest &request,
                                          const std::vector<RestingOrderView> &resting_orders) const
{
    return std::any_of(resting_orders.begin(), resting_orders.end(), [&](const auto &resting_order) {
        if (resting_order.user_id != request.user_id || resting_order.quantity <= 0.0 ||
            resting_order.side == request.side)
        {
            return false;
        }

        if (request.type == OrderType::kMarket)
        {
            return true;
        }

        if (request.side == Side::kBuy)
        {
            return request.price >= resting_order.price;
        }

        return request.price <= resting_order.price;
    });
}

void PreTradeRiskEngine::flush_audit_queue()
{
    while (auto decision = audit_queue_.try_pop())
    {
        audit_history_.push_back(*decision);
    }
}

void PreTradeRiskEngine::enqueue_audit(RiskDecision decision)
{
    if (!audit_queue_.push(std::move(decision)))
    {
        flush_audit_queue();
        const auto accepted = audit_queue_.push(std::move(decision));
        if (!accepted)
        {
            throw std::runtime_error("audit queue could not accept decision after flush");
        }
    }
}

ModuleSummary module_summary()
{
    return {
        .module_name = project_name(),
        .risk_checks = 6,
        .infrastructure_reuse_points = 2,
    };
}

std::string project_name()
{
    return "pre_trade_risk_engine";
}

} // namespace pre_trade_risk_engine
