#include "exchange_pipeline.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace exchange_pipeline
{

namespace
{

constexpr double kEpsilon = 1e-9;

bool is_zero(double value)
{
    return std::fabs(value) <= kEpsilon;
}

std::unordered_map<std::string, std::string> parse_key_values(std::string_view payload)
{
    std::unordered_map<std::string, std::string> values;
    std::size_t start = 0;
    while (start < payload.size())
    {
        const auto end = payload.find(';', start);
        const auto token = payload.substr(start, end == std::string_view::npos ? payload.size() - start : end - start);
        const auto delimiter = token.find('=');
        if (delimiter != std::string_view::npos)
        {
            values.emplace(std::string(token.substr(0, delimiter)), std::string(token.substr(delimiter + 1)));
        }
        if (end == std::string_view::npos)
        {
            break;
        }
        start = end + 1;
    }
    return values;
}

orderbook_matching_engine::Side parse_matching_side(const std::string &value)
{
    if (value == "buy")
    {
        return orderbook_matching_engine::Side::kBuy;
    }
    if (value == "sell")
    {
        return orderbook_matching_engine::Side::kSell;
    }
    throw std::invalid_argument("unsupported side");
}

pre_trade_risk_engine::Side parse_risk_side(const std::string &value)
{
    if (value == "buy")
    {
        return pre_trade_risk_engine::Side::kBuy;
    }
    if (value == "sell")
    {
        return pre_trade_risk_engine::Side::kSell;
    }
    throw std::invalid_argument("unsupported side");
}

orderbook_matching_engine::OrderType parse_matching_type(const std::string &value)
{
    if (value == "limit")
    {
        return orderbook_matching_engine::OrderType::kLimit;
    }
    if (value == "market")
    {
        return orderbook_matching_engine::OrderType::kMarket;
    }
    throw std::invalid_argument("unsupported order type");
}

pre_trade_risk_engine::OrderType parse_risk_type(const std::string &value)
{
    if (value == "limit")
    {
        return pre_trade_risk_engine::OrderType::kLimit;
    }
    if (value == "market")
    {
        return pre_trade_risk_engine::OrderType::kMarket;
    }
    throw std::invalid_argument("unsupported order type");
}

orderbook_matching_engine::TimeInForce parse_tif(const std::string &value)
{
    if (value == "gtc")
    {
        return orderbook_matching_engine::TimeInForce::kGtc;
    }
    if (value == "ioc")
    {
        return orderbook_matching_engine::TimeInForce::kIoc;
    }
    if (value == "fok")
    {
        return orderbook_matching_engine::TimeInForce::kFok;
    }
    if (value == "gtx")
    {
        return orderbook_matching_engine::TimeInForce::kGtx;
    }
    throw std::invalid_argument("unsupported time in force");
}

} // namespace

ExchangePipeline::ExchangePipeline(std::string symbol)
    : symbol_(std::move(symbol)), gateway_(4096), risk_(4096), matching_(symbol_, 4096)
{
    if (symbol_.empty())
    {
        throw std::invalid_argument("symbol must not be empty");
    }
    gateway_.configure_route("/v1/orders", unified_access_gateway::Backend::kRisk, "risk_ingress");
}

void ExchangePipeline::configure_market(double best_bid, double best_ask,
                                        account_clearing_system::FeeSchedule fee_schedule,
                                        pre_trade_risk_engine::MarketRiskConfig risk_config)
{
    best_bid_ = best_bid;
    best_ask_ = best_ask;
    fee_schedule_ = fee_schedule;
    risk_.configure_market(std::move(risk_config));
    risk_.update_top_of_book(symbol_, best_bid_, best_ask_);
}

void ExchangePipeline::register_user(std::string api_key, std::string secret, std::string user_id,
                                     account_clearing_system::AccountSnapshot account_snapshot,
                                     std::size_t max_requests, std::int64_t window_ms)
{
    gateway_.register_credential(std::move(api_key), std::move(secret), user_id);
    gateway_.set_user_rate_limit(user_id, max_requests, window_ms);
    clearing_.upsert_account(user_id, account_snapshot);
    known_users_.insert(user_id);
    sync_account_state(user_id);
}

WorkflowResult ExchangePipeline::submit(const unified_access_gateway::GatewayRequest &request)
{
    WorkflowResult workflow;
    workflow.route_result = gateway_.route(request);
    workflow.route_events = gateway_.replay_route_events(0);
    if (!workflow.route_result.accepted)
    {
        return workflow;
    }

    const auto forwarded = gateway_.poll_forwarded_request();
    if (!forwarded.has_value())
    {
        return workflow;
    }

    const auto parsed = parse_order(*forwarded);
    if (!parsed.has_value())
    {
        workflow.risk_decision = risk_.evaluate({
            .order_id = "",
            .user_id = forwarded->user_id,
            .symbol = symbol_,
            .source_ip = "",
            .side = pre_trade_risk_engine::Side::kBuy,
            .type = pre_trade_risk_engine::OrderType::kLimit,
            .price = 0.0,
            .quantity = 0.0,
            .timestamp_ms = forwarded->timestamp_ms,
        });
        return workflow;
    }

    workflow.risk_decision = risk_.evaluate(to_risk_request(*parsed));
    if (!workflow.risk_decision.accepted)
    {
        return workflow;
    }

    const auto hold_id = hold_id_for(parsed->order_id);
    const auto reserved = reserve_amount(*parsed);
    const bool hold_created = parsed->matching_side == orderbook_matching_engine::Side::kBuy
                                  ? clearing_.freeze_quote(parsed->user_id, hold_id, reserved, parsed->timestamp_ms)
                                  : clearing_.freeze_base(parsed->user_id, hold_id, reserved, parsed->timestamp_ms);
    if (!hold_created)
    {
        workflow.risk_decision.accepted = false;
        workflow.risk_decision.reject_reason = parsed->matching_side == orderbook_matching_engine::Side::kBuy
                                                  ? pre_trade_risk_engine::RejectReason::kInsufficientBalance
                                                  : pre_trade_risk_engine::RejectReason::kInsufficientPosition;
        workflow.risk_decision.rule_name = "clearing_hold_reservation";
        return workflow;
    }

    sync_account_state(parsed->user_id);
    order_registry_[parsed->order_id] = ManagedOrder{
        .user_id = parsed->user_id,
        .symbol = parsed->symbol,
        .side = parsed->matching_side,
        .hold_id = hold_id,
    };

    workflow.match_result = matching_.submit(to_matching_request(*parsed));
    workflow.accepted = workflow.match_result.accepted;

    if (!workflow.match_result.accepted && workflow.match_result.trades.empty())
    {
        release_residual_hold(hold_id, parsed->timestamp_ms + 1);
        order_registry_.erase(parsed->order_id);
        sync_account_state(parsed->user_id);
        sync_resting_orders();
        workflow.trade_events = matching_.replay_market_data("trades." + symbol_, 0);
        workflow.depth_events = matching_.replay_market_data("depth." + symbol_, 0);
        return workflow;
    }

    for (const auto &trade : workflow.match_result.trades)
    {
        const auto maker_it = order_registry_.find(trade.maker_order_id);
        const auto taker_it = order_registry_.find(trade.taker_order_id);
        if (maker_it == order_registry_.end() || taker_it == order_registry_.end())
        {
            throw std::runtime_error("missing order ownership for matched trade");
        }

        const auto &maker = maker_it->second;
        const auto &taker = taker_it->second;
        const bool taker_is_buy = taker.side == orderbook_matching_engine::Side::kBuy;
        const std::string &buyer_account_id = taker_is_buy ? taker.user_id : maker.user_id;
        const std::string &seller_account_id = taker_is_buy ? maker.user_id : taker.user_id;
        const std::string &buyer_hold_id = taker_is_buy ? taker.hold_id : maker.hold_id;
        const std::string &seller_hold_id = taker_is_buy ? maker.hold_id : taker.hold_id;

        workflow.settlements.push_back(clearing_.settle_spot_trade(
            {
                .trade_id = trade.maker_order_id + "->" + trade.taker_order_id,
                .symbol = symbol_,
                .buyer_account_id = buyer_account_id,
                .seller_account_id = seller_account_id,
                .buyer_hold_id = buyer_hold_id,
                .seller_hold_id = seller_hold_id,
                .price = trade.price,
                .quantity = trade.quantity,
                .buyer_is_taker = taker_is_buy,
                .timestamp_ms = parsed->timestamp_ms,
            },
            fee_schedule_));
        sync_account_state(buyer_account_id);
        sync_account_state(seller_account_id);
    }

    const auto resting = matching_.resting_orders();
    std::unordered_set<std::string> live_order_ids;
    live_order_ids.reserve(resting.size());
    for (const auto &order : resting)
    {
        live_order_ids.insert(order.order_id);
    }

    const std::vector<std::string> tracked_order_ids = [&]() {
        std::vector<std::string> ids;
        ids.reserve(order_registry_.size());
        for (const auto &[order_id, managed] : order_registry_)
        {
            if (managed.symbol == symbol_)
            {
                ids.push_back(order_id);
            }
        }
        return ids;
    }();

    for (const auto &order_id : tracked_order_ids)
    {
        if (!live_order_ids.contains(order_id))
        {
            const auto registry_it = order_registry_.find(order_id);
            if (registry_it != order_registry_.end())
            {
                release_residual_hold(registry_it->second.hold_id, parsed->timestamp_ms + 2);
                sync_account_state(registry_it->second.user_id);
                order_registry_.erase(registry_it);
            }
        }
    }

    sync_resting_orders();
    refresh_market_reference();
    workflow.trade_events = matching_.replay_market_data("trades." + symbol_, 0);
    workflow.depth_events = matching_.replay_market_data("depth." + symbol_, 0);
    workflow.outbox_messages = clearing_.pending_outbox(32);
    return workflow;
}

std::optional<account_clearing_system::AccountSnapshot> ExchangePipeline::account(std::string_view user_id) const
{
    return clearing_.account(user_id);
}

std::vector<distributed_consistency::OutboxMessage> ExchangePipeline::pending_outbox(std::size_t max_items) const
{
    return clearing_.pending_outbox(max_items);
}

std::vector<pre_trade_risk_engine::RiskDecision> ExchangePipeline::audit_log() const
{
    return risk_.audit_log();
}

std::vector<market_data_push_system::BroadcastMessage> ExchangePipeline::replay_market_data(std::string_view topic,
                                                                                            std::uint64_t after) const
{
    return matching_.replay_market_data(std::string(topic), after);
}

std::vector<market_data_push_system::BroadcastMessage> ExchangePipeline::replay_route_events(std::uint64_t after) const
{
    return gateway_.replay_route_events(after);
}

std::optional<ExchangePipeline::ParsedOrder> ExchangePipeline::parse_order(
    const unified_access_gateway::ForwardedRequest &request) const
{
    try
    {
        const auto values = parse_key_values(request.payload);
        const auto &side = values.at("side");
        const auto &type = values.at("type");
        const auto &tif = values.at("tif");
        const auto &order_id = values.at("order_id");
        const auto &symbol = values.at("symbol");
        const auto &source_ip = values.at("source_ip");
        const double price = values.contains("price") ? std::stod(values.at("price")) : 0.0;
        const double quantity = std::stod(values.at("quantity"));

        return ParsedOrder{
            .order_id = order_id,
            .user_id = request.user_id,
            .symbol = symbol,
            .source_ip = source_ip,
            .matching_side = parse_matching_side(side),
            .risk_side = parse_risk_side(side),
            .matching_type = parse_matching_type(type),
            .risk_type = parse_risk_type(type),
            .tif = parse_tif(tif),
            .price = price,
            .quantity = quantity,
            .timestamp_ms = request.timestamp_ms,
        };
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}

double ExchangePipeline::reserve_amount(const ParsedOrder &order) const
{
    if (order.matching_side == orderbook_matching_engine::Side::kSell)
    {
        return order.quantity;
    }

    double reference_price = order.price;
    if (order.matching_type == orderbook_matching_engine::OrderType::kMarket || reference_price <= 0.0)
    {
        const auto snapshot = matching_.snapshot(1);
        if (!snapshot.asks.empty())
        {
            reference_price = snapshot.asks.front().price;
        }
        else
        {
            reference_price = best_ask_;
        }
    }

    return reference_price * order.quantity * (1.0 + fee_schedule_.taker_fee_bps / 10000.0);
}

std::string ExchangePipeline::hold_id_for(std::string_view order_id)
{
    return "hold-" + std::string(order_id);
}

orderbook_matching_engine::OrderRequest ExchangePipeline::to_matching_request(const ParsedOrder &order) const
{
    return {
        .order_id = order.order_id,
        .side = order.matching_side,
        .type = order.matching_type,
        .tif = order.tif,
        .price = order.price,
        .quantity = order.quantity,
        .timestamp = static_cast<std::uint64_t>(order.timestamp_ms),
    };
}

pre_trade_risk_engine::OrderRequest ExchangePipeline::to_risk_request(const ParsedOrder &order) const
{
    return {
        .order_id = order.order_id,
        .user_id = order.user_id,
        .symbol = order.symbol,
        .source_ip = order.source_ip,
        .side = order.risk_side,
        .type = order.risk_type,
        .price = order.price,
        .quantity = order.quantity,
        .timestamp_ms = order.timestamp_ms,
    };
}

void ExchangePipeline::sync_account_state(std::string_view user_id)
{
    const auto snapshot = clearing_.account(user_id);
    if (!snapshot.has_value())
    {
        return;
    }

    risk_.set_account_state(user_id, symbol_,
                            {
                                .quote_balance = snapshot->available_quote,
                                .base_position = snapshot->available_base,
                            });
}

void ExchangePipeline::sync_resting_orders()
{
    for (const auto &user_id : resting_users_)
    {
        risk_.clear_user_resting_orders(symbol_, user_id);
    }

    resting_users_.clear();
    for (const auto &resting : matching_.resting_orders())
    {
        const auto registry_it = order_registry_.find(resting.order_id);
        if (registry_it == order_registry_.end())
        {
            continue;
        }
        risk_.track_resting_order(symbol_,
                                  {
                                      .user_id = registry_it->second.user_id,
                                      .side = registry_it->second.side == orderbook_matching_engine::Side::kBuy
                                                  ? pre_trade_risk_engine::Side::kBuy
                                                  : pre_trade_risk_engine::Side::kSell,
                                      .price = resting.price,
                                      .quantity = resting.open_quantity,
                                  });
        resting_users_.insert(registry_it->second.user_id);
    }
}

void ExchangePipeline::refresh_market_reference()
{
    const auto snapshot = matching_.snapshot(1);
    if (!snapshot.bids.empty())
    {
        best_bid_ = snapshot.bids.front().price;
    }
    if (!snapshot.asks.empty())
    {
        best_ask_ = snapshot.asks.front().price;
    }
    risk_.update_top_of_book(symbol_, best_bid_, best_ask_);
}

void ExchangePipeline::release_residual_hold(std::string_view hold_id, std::int64_t timestamp_ms)
{
    const auto amount = clearing_.hold_amount(hold_id);
    if (!amount.has_value() || is_zero(*amount))
    {
        return;
    }
    (void)clearing_.release_hold(hold_id, timestamp_ms);
}

std::string project_name()
{
    return "exchange_pipeline";
}

} // namespace exchange_pipeline
