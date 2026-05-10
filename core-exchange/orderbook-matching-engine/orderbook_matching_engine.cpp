#include "orderbook_matching_engine.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace orderbook_matching_engine
{

namespace
{

constexpr double kPriceEpsilon = 1e-9;

bool is_zero(double value)
{
    return std::fabs(value) <= kPriceEpsilon;
}

std::string to_string(OrderStatus status)
{
    switch (status)
    {
    case OrderStatus::kAccepted:
        return "accepted";
    case OrderStatus::kPartiallyFilled:
        return "partially_filled";
    case OrderStatus::kFilled:
        return "filled";
    case OrderStatus::kCancelled:
        return "cancelled";
    case OrderStatus::kRejected:
        return "rejected";
    }

    return "unknown";
}

std::string to_string(RejectReason reason)
{
    switch (reason)
    {
    case RejectReason::kNone:
        return "none";
    case RejectReason::kDuplicateOrderId:
        return "duplicate_order_id";
    case RejectReason::kWouldCrossPostOnly:
        return "would_cross_post_only";
    case RejectReason::kInsufficientLiquidity:
        return "insufficient_liquidity";
    case RejectReason::kMarketOrderWouldRest:
        return "market_order_would_rest";
    case RejectReason::kOrderNotFound:
        return "order_not_found";
    }

    return "unknown";
}

std::string format_trade_payload(const Trade &trade)
{
    std::ostringstream stream;
    stream << "maker=" << trade.maker_order_id << ",taker=" << trade.taker_order_id << ",price=" << trade.price
           << ",qty=" << trade.quantity;
    return stream.str();
}

void ignore_depth_delta(const market_data_push_system::DepthDelta &delta)
{
    const auto *delta_address = &delta;
    (void)delta_address;
}

} // namespace

struct OrderbookMatchingEngine::RestingOrder
{
    std::string order_id;
    Side side = Side::kBuy;
    double price = 0.0;
    double open_quantity = 0.0;
    std::uint64_t timestamp = 0;
};

OrderbookMatchingEngine::OrderbookMatchingEngine(std::string symbol, std::size_t event_queue_capacity)
    : symbol_(std::move(symbol)), order_pool_(1024, 128), depth_book_(symbol_), event_queue_(event_queue_capacity)
{
    if (symbol_.empty())
    {
        throw std::invalid_argument("symbol must not be empty");
    }
}

MatchResult OrderbookMatchingEngine::submit(const OrderRequest &request)
{
    if (request.order_id.empty() || request.quantity <= 0.0)
    {
        return MatchResult{
            .accepted = false,
            .final_status = OrderStatus::kRejected,
            .reject_reason = RejectReason::kInsufficientLiquidity,
            .executed_quantity = 0.0,
            .remaining_quantity = request.quantity,
        };
    }

    if (live_orders_.contains(request.order_id))
    {
        emit_order_event(request.order_id, OrderStatus::kRejected, RejectReason::kDuplicateOrderId, request.quantity);
        return MatchResult{
            .accepted = false,
            .final_status = OrderStatus::kRejected,
            .reject_reason = RejectReason::kDuplicateOrderId,
            .executed_quantity = 0.0,
            .remaining_quantity = request.quantity,
        };
    }

    return request.type == OrderType::kLimit ? handle_limit_order(request) : handle_market_order(request);
}

MatchResult OrderbookMatchingEngine::cancel(std::string_view order_id, std::uint64_t)
{
    const auto iterator = live_orders_.find(std::string(order_id));
    if (iterator == live_orders_.end())
    {
        return MatchResult{
            .accepted = false,
            .final_status = OrderStatus::kRejected,
            .reject_reason = RejectReason::kOrderNotFound,
            .executed_quantity = 0.0,
            .remaining_quantity = 0.0,
        };
    }

    auto *const order = iterator->second;
    const double open_quantity = order->open_quantity;
    remove_resting_order(order, true);
    emit_order_event(order_id, OrderStatus::kCancelled, RejectReason::kNone, 0.0);

    return MatchResult{
        .accepted = true,
        .final_status = OrderStatus::kCancelled,
        .reject_reason = RejectReason::kNone,
        .executed_quantity = 0.0,
        .remaining_quantity = open_quantity,
    };
}

market_data_push_system::DepthSnapshot OrderbookMatchingEngine::snapshot(std::size_t depth) const
{
    return depth_book_.snapshot(depth);
}

std::vector<market_data_push_system::BroadcastMessage> OrderbookMatchingEngine::replay_market_data(
    std::string_view topic, std::uint64_t sequence_after) const
{
    return broadcaster_.replay(topic, sequence_after);
}

std::optional<EngineEvent> OrderbookMatchingEngine::poll_event()
{
    return event_queue_.try_pop();
}

std::vector<RestingOrderView> OrderbookMatchingEngine::resting_orders() const
{
    std::vector<RestingOrderView> views;
    views.reserve(live_orders_.size());
    for (const auto &[order_id, order] : live_orders_)
    {
        views.push_back(RestingOrderView{
            .order_id = order_id,
            .side = order->side,
            .price = order->price,
            .open_quantity = order->open_quantity,
        });
    }

    std::sort(views.begin(), views.end(), [](const RestingOrderView &left, const RestingOrderView &right) {
        return left.order_id < right.order_id;
    });
    return views;
}

std::size_t OrderbookMatchingEngine::live_order_count() const
{
    return live_orders_.size();
}

MatchResult OrderbookMatchingEngine::make_rejected_result(RejectReason reason, double remaining_quantity)
{
    return MatchResult{
        .accepted = false,
        .final_status = OrderStatus::kRejected,
        .reject_reason = reason,
        .executed_quantity = 0.0,
        .remaining_quantity = remaining_quantity,
    };
}

MatchResult OrderbookMatchingEngine::make_accepted_result(double requested_quantity)
{
    return MatchResult{
        .accepted = true,
        .final_status = OrderStatus::kAccepted,
        .reject_reason = RejectReason::kNone,
        .executed_quantity = 0.0,
        .remaining_quantity = requested_quantity,
    };
}

void OrderbookMatchingEngine::match_request(const OrderRequest &request, double &remaining_quantity,
                                            MatchResult &result)
{
    if (request.side == Side::kBuy)
    {
        match_against_asks(request, remaining_quantity, result);
    }
    else
    {
        match_against_bids(request, remaining_quantity, result);
    }
}

MatchResult OrderbookMatchingEngine::finalize_limit_result(const OrderRequest &request, double remaining_quantity,
                                                           MatchResult result)
{
    result.executed_quantity = request.quantity - remaining_quantity;
    result.remaining_quantity = remaining_quantity;

    if (is_zero(remaining_quantity))
    {
        result.final_status = result.executed_quantity > 0.0 ? OrderStatus::kFilled : OrderStatus::kAccepted;
        emit_order_event(request.order_id, result.final_status, RejectReason::kNone, 0.0);
        return result;
    }

    if (request.tif == TimeInForce::kIoc)
    {
        result.final_status = result.executed_quantity > 0.0 ? OrderStatus::kPartiallyFilled : OrderStatus::kCancelled;
        emit_order_event(request.order_id, result.final_status, RejectReason::kNone, remaining_quantity);
        return result;
    }

    add_resting_order(request, remaining_quantity);
    result.final_status = result.executed_quantity > 0.0 ? OrderStatus::kPartiallyFilled : OrderStatus::kAccepted;
    emit_order_event(request.order_id, result.final_status, RejectReason::kNone, remaining_quantity);
    return result;
}

MatchResult OrderbookMatchingEngine::finalize_market_result(const OrderRequest &request, double remaining_quantity,
                                                            MatchResult result)
{
    result.executed_quantity = request.quantity - remaining_quantity;
    result.remaining_quantity = remaining_quantity;

    if (is_zero(remaining_quantity))
    {
        result.final_status = OrderStatus::kFilled;
        emit_order_event(request.order_id, OrderStatus::kFilled, RejectReason::kNone, 0.0);
        return result;
    }

    if (request.tif == TimeInForce::kIoc || result.executed_quantity > 0.0)
    {
        result.final_status = result.executed_quantity > 0.0 ? OrderStatus::kPartiallyFilled : OrderStatus::kCancelled;
        emit_order_event(request.order_id, result.final_status, RejectReason::kNone, remaining_quantity);
        return result;
    }

    emit_order_event(request.order_id, OrderStatus::kRejected, RejectReason::kMarketOrderWouldRest, remaining_quantity);
    result.accepted = false;
    result.final_status = OrderStatus::kRejected;
    result.reject_reason = RejectReason::kMarketOrderWouldRest;
    return result;
}

MatchResult OrderbookMatchingEngine::handle_limit_order(const OrderRequest &request)
{
    if (request.tif == TimeInForce::kGtx && would_cross(request))
    {
        emit_order_event(request.order_id, OrderStatus::kRejected, RejectReason::kWouldCrossPostOnly, request.quantity);
        return make_rejected_result(RejectReason::kWouldCrossPostOnly, request.quantity);
    }

    if (request.tif == TimeInForce::kFok && !can_fill_fully(request))
    {
        emit_order_event(request.order_id, OrderStatus::kRejected, RejectReason::kInsufficientLiquidity,
                         request.quantity);
        return make_rejected_result(RejectReason::kInsufficientLiquidity, request.quantity);
    }

    MatchResult result = make_accepted_result(request.quantity);

    double remaining_quantity = request.quantity;
    match_request(request, remaining_quantity, result);
    return finalize_limit_result(request, remaining_quantity, std::move(result));
}

MatchResult OrderbookMatchingEngine::handle_market_order(const OrderRequest &request)
{
    if (request.tif == TimeInForce::kFok && !can_fill_fully(request))
    {
        emit_order_event(request.order_id, OrderStatus::kRejected, RejectReason::kInsufficientLiquidity,
                         request.quantity);
        return make_rejected_result(RejectReason::kInsufficientLiquidity, request.quantity);
    }

    MatchResult result = make_accepted_result(request.quantity);

    double remaining_quantity = request.quantity;
    match_request(request, remaining_quantity, result);
    return finalize_market_result(request, remaining_quantity, std::move(result));
}

bool OrderbookMatchingEngine::can_fill_fully(const OrderRequest &request) const
{
    double remaining = request.quantity;

    if (request.side == Side::kBuy)
    {
        for (const auto &[price, orders] : asks_)
        {
            if (request.type == OrderType::kLimit && price - request.price > kPriceEpsilon)
            {
                break;
            }

            for (const RestingOrder *order : orders)
            {
                remaining -= order->open_quantity;
                if (remaining <= kPriceEpsilon)
                {
                    return true;
                }
            }
        }
    }
    else
    {
        for (const auto &[price, orders] : bids_)
        {
            if (request.type == OrderType::kLimit && request.price - price > kPriceEpsilon)
            {
                break;
            }

            for (const RestingOrder *order : orders)
            {
                remaining -= order->open_quantity;
                if (remaining <= kPriceEpsilon)
                {
                    return true;
                }
            }
        }
    }

    return remaining <= kPriceEpsilon;
}

bool OrderbookMatchingEngine::would_cross(const OrderRequest &request) const
{
    if (request.side == Side::kBuy)
    {
        return has_best_ask() && request.price + kPriceEpsilon >= best_ask();
    }

    return has_best_bid() && request.price <= best_bid() + kPriceEpsilon;
}

double OrderbookMatchingEngine::best_bid() const
{
    return bids_.empty() ? 0.0 : bids_.begin()->first;
}

double OrderbookMatchingEngine::best_ask() const
{
    return asks_.empty() ? 0.0 : asks_.begin()->first;
}

bool OrderbookMatchingEngine::has_best_bid() const
{
    return !bids_.empty();
}

bool OrderbookMatchingEngine::has_best_ask() const
{
    return !asks_.empty();
}

void OrderbookMatchingEngine::add_resting_order(const OrderRequest &request, double remaining_quantity)
{
    RestingOrder *order = order_pool_.create();
    order->order_id = request.order_id;
    order->side = request.side;
    order->price = request.price;
    order->open_quantity = remaining_quantity;
    order->timestamp = request.timestamp;

    live_orders_.emplace(order->order_id, order);

    if (order->side == Side::kBuy)
    {
        bids_[order->price].push_back(order);
    }
    else
    {
        asks_[order->price].push_back(order);
    }

    const auto side =
        order->side == Side::kBuy ? market_data_push_system::Side::kBid : market_data_push_system::Side::kAsk;
    ignore_depth_delta(depth_book_.update(side, order->price, order->open_quantity));
    emit_depth_event();
}

void OrderbookMatchingEngine::remove_resting_order(RestingOrder *order, bool)
{
    auto remove_from_book = [&](auto &book, market_data_push_system::Side depth_side) {
        auto level_iterator = book.find(order->price);
        if (level_iterator == book.end())
        {
            return;
        }

        auto &queue = level_iterator->second;
        auto order_iterator = std::find(queue.begin(), queue.end(), order);
        if (order_iterator != queue.end())
        {
            queue.erase(order_iterator);
        }

        double aggregate_quantity = 0.0;
        for (const RestingOrder *queued : queue)
        {
            aggregate_quantity += queued->open_quantity;
        }

        ignore_depth_delta(depth_book_.update(depth_side, order->price, aggregate_quantity));

        if (queue.empty())
        {
            book.erase(level_iterator);
        }
    };

    if (order->side == Side::kBuy)
    {
        remove_from_book(bids_, market_data_push_system::Side::kBid);
    }
    else
    {
        remove_from_book(asks_, market_data_push_system::Side::kAsk);
    }

    live_orders_.erase(order->order_id);
    order_pool_.destroy(order);
    emit_depth_event();
}

void OrderbookMatchingEngine::match_against_asks(const OrderRequest &request, double &remaining_quantity,
                                                 MatchResult &result)
{
    while (remaining_quantity > kPriceEpsilon && !asks_.empty())
    {
        const auto best_level = asks_.begin();
        if (request.type == OrderType::kLimit && best_level->first - request.price > kPriceEpsilon)
        {
            break;
        }

        const auto &queue = best_level->second;
        auto *const maker = queue.front();
        const double fill_quantity = std::min(remaining_quantity, maker->open_quantity);
        maker->open_quantity -= fill_quantity;
        remaining_quantity -= fill_quantity;

        Trade trade{
            .maker_order_id = maker->order_id,
            .taker_order_id = request.order_id,
            .price = maker->price,
            .quantity = fill_quantity,
        };
        result.trades.push_back(trade);
        emit_trade_event(trade);

        if (maker->open_quantity <= kPriceEpsilon)
        {
            emit_order_event(maker->order_id, OrderStatus::kFilled, RejectReason::kNone, 0.0);
            remove_resting_order(maker, false);
        }
        else
        {
            double aggregate_quantity = 0.0;
            for (const RestingOrder *queued : queue)
            {
                aggregate_quantity += queued->open_quantity;
            }
            ignore_depth_delta(
                depth_book_.update(market_data_push_system::Side::kAsk, maker->price, aggregate_quantity));
            emit_order_event(maker->order_id, OrderStatus::kPartiallyFilled, RejectReason::kNone, maker->open_quantity);
            emit_depth_event();
        }
    }
}

void OrderbookMatchingEngine::match_against_bids(const OrderRequest &request, double &remaining_quantity,
                                                 MatchResult &result)
{
    while (remaining_quantity > kPriceEpsilon && !bids_.empty())
    {
        const auto best_level = bids_.begin();
        if (request.type == OrderType::kLimit && request.price - best_level->first > kPriceEpsilon)
        {
            break;
        }

        const auto &queue = best_level->second;
        auto *const maker = queue.front();
        const double fill_quantity = std::min(remaining_quantity, maker->open_quantity);
        maker->open_quantity -= fill_quantity;
        remaining_quantity -= fill_quantity;

        Trade trade{
            .maker_order_id = maker->order_id,
            .taker_order_id = request.order_id,
            .price = maker->price,
            .quantity = fill_quantity,
        };
        result.trades.push_back(trade);
        emit_trade_event(trade);

        if (maker->open_quantity <= kPriceEpsilon)
        {
            emit_order_event(maker->order_id, OrderStatus::kFilled, RejectReason::kNone, 0.0);
            remove_resting_order(maker, false);
        }
        else
        {
            double aggregate_quantity = 0.0;
            for (const RestingOrder *queued : queue)
            {
                aggregate_quantity += queued->open_quantity;
            }
            ignore_depth_delta(
                depth_book_.update(market_data_push_system::Side::kBid, maker->price, aggregate_quantity));
            emit_order_event(maker->order_id, OrderStatus::kPartiallyFilled, RejectReason::kNone, maker->open_quantity);
            emit_depth_event();
        }
    }
}

void OrderbookMatchingEngine::emit_order_event(std::string_view order_id, OrderStatus status, RejectReason reason,
                                               double open_quantity)
{
    std::ostringstream payload;
    payload << "order_id=" << order_id << ",status=" << to_string(status) << ",reason=" << to_string(reason)
            << ",open_qty=" << open_quantity;
    const std::string event_payload = payload.str();
    publish_market_data("orders." + symbol_, event_payload);
    event_queue_.push(EngineEvent{.topic = "orders", .payload = event_payload});
}

void OrderbookMatchingEngine::emit_trade_event(const Trade &trade)
{
    const std::string payload = format_trade_payload(trade);
    publish_market_data("trades." + symbol_, payload);
    event_queue_.push(EngineEvent{.topic = "trades", .payload = payload});
}

void OrderbookMatchingEngine::emit_depth_event()
{
    const auto top = depth_book_.snapshot(5);
    std::ostringstream payload;
    payload << "bids=" << top.bids.size() << ",asks=" << top.asks.size();
    const std::string event_payload = payload.str();
    publish_market_data("depth." + symbol_, event_payload);
    event_queue_.push(EngineEvent{.topic = "depth", .payload = event_payload});
}

void OrderbookMatchingEngine::publish_market_data(std::string_view topic, std::string payload)
{
    const auto message = broadcaster_.publish(std::string(topic), std::move(payload));
    const auto *message_address = &message;
    (void)message_address;
}

std::string project_name()
{
    return "orderbook_matching_engine";
}

ModuleSummary module_summary()
{
    return ModuleSummary{
        .module_name = project_name(),
        .order_features = 5,
        .infrastructure_reuse_points = 3,
    };
}

} // namespace orderbook_matching_engine
