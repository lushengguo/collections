#include "unified_access_gateway/unified_access_gateway.hpp"

#include <algorithm>
#include <utility>

namespace unified_access_gateway
{

UnifiedAccessGateway::UnifiedAccessGateway(std::size_t ingress_queue_capacity) : ingress_queue_(ingress_queue_capacity)
{
    backend_health_[Backend::kRisk] = true;
    backend_health_[Backend::kMatching] = true;
    backend_health_[Backend::kClearing] = true;
    backend_health_[Backend::kMarketData] = true;
}

void UnifiedAccessGateway::register_credential(std::string api_key, std::string secret, std::string user_id)
{
    credentials_[std::move(api_key)] = Credential{.secret = std::move(secret), .user_id = std::move(user_id)};
}

void UnifiedAccessGateway::configure_route(std::string path_prefix, Backend backend, std::string route_name)
{
    routes_.push_back(RouteConfig{
        .path_prefix = std::move(path_prefix),
        .backend = backend,
        .route_name = std::move(route_name),
    });
    std::sort(routes_.begin(), routes_.end(),
              [](const auto &left, const auto &right) { return left.path_prefix.size() > right.path_prefix.size(); });
}

void UnifiedAccessGateway::set_user_rate_limit(std::string user_id, std::size_t max_requests, std::int64_t window_ms)
{
    user_limits_[std::move(user_id)] = RateLimit{.max_requests = max_requests, .window_ms = window_ms};
}

void UnifiedAccessGateway::set_backend_health(Backend backend, bool healthy)
{
    backend_health_[backend] = healthy;
}

void UnifiedAccessGateway::open_session(std::string session_id, Protocol protocol, std::int64_t heartbeat_timeout_ms,
                                        std::int64_t timestamp_ms)
{
    sessions_[std::move(session_id)] = SessionState{
        .protocol = protocol,
        .heartbeat_timeout_ms = heartbeat_timeout_ms,
        .last_heartbeat_ms = timestamp_ms,
        .live = true,
    };
}

bool UnifiedAccessGateway::record_heartbeat(std::string_view session_id, std::int64_t timestamp_ms)
{
    auto session_it = sessions_.find(std::string(session_id));
    if (session_it == sessions_.end() || !session_it->second.live)
    {
        return false;
    }

    session_it->second.last_heartbeat_ms = timestamp_ms;
    return true;
}

std::size_t UnifiedAccessGateway::close_stale_sessions(std::int64_t now_ms)
{
    std::size_t closed = 0;
    for (auto &[session_id, state] : sessions_)
    {
        (void)session_id;
        if (state.live && now_ms - state.last_heartbeat_ms > state.heartbeat_timeout_ms)
        {
            state.live = false;
            ++closed;
        }
    }
    return closed;
}

RouteResult UnifiedAccessGateway::route(const GatewayRequest &request)
{
    if (request.request_id.empty() || request.user_id.empty() || request.api_key.empty() || request.path.empty() ||
        request.timestamp_ms <= 0)
    {
        return RouteResult{.accepted = false, .reject_reason = RejectReason::kInvalidRequest};
    }

    const auto credential_it = credentials_.find(request.api_key);
    if (credential_it == credentials_.end() || credential_it->second.user_id != request.user_id ||
        request.signature != expected_signature(request.api_key, credential_it->second.secret, request.request_id))
    {
        return RouteResult{.accepted = false, .reject_reason = RejectReason::kAuthenticationFailed};
    }

    const auto route = match_route(request.path);
    if (!route.has_value())
    {
        return RouteResult{.accepted = false, .reject_reason = RejectReason::kUnknownRoute};
    }

    const auto backend_health_it = backend_health_.find(route->backend);
    if (backend_health_it != backend_health_.end() && !backend_health_it->second)
    {
        return RouteResult{.accepted = false, .reject_reason = RejectReason::kCircuitOpen, .backend = route->backend};
    }

    const auto limit_it = user_limits_.find(request.user_id);
    if (limit_it != user_limits_.end())
    {
        auto &window = user_windows_[request.user_id];
        window.erase(std::remove_if(window.begin(), window.end(),
                                    [&](const auto timestamp) {
                                        return timestamp < request.timestamp_ms - limit_it->second.window_ms;
                                    }),
                     window.end());
        if (window.size() >= limit_it->second.max_requests)
        {
            return RouteResult{
                .accepted = false, .reject_reason = RejectReason::kRateLimited, .backend = route->backend};
        }
        window.push_back(request.timestamp_ms);
    }

    ForwardedRequest forwarded{
        .request_id = request.request_id,
        .user_id = request.user_id,
        .path = request.path,
        .payload = request.payload,
        .backend = route->backend,
        .protocol = request.protocol,
        .timestamp_ms = request.timestamp_ms,
    };
    if (!ingress_queue_.push(forwarded))
    {
        return RouteResult{.accepted = false, .reject_reason = RejectReason::kQueueFull, .backend = route->backend};
    }

    const auto message = broadcaster_.publish("gateway.routes", route_event_payload(forwarded, route->route_name));
    (void)message;
    return RouteResult{
        .accepted = true,
        .reject_reason = RejectReason::kNone,
        .backend = route->backend,
        .route_name = route->route_name,
        .queued_requests = ingress_queue_.size_approx(),
    };
}

std::optional<ForwardedRequest> UnifiedAccessGateway::poll_forwarded_request()
{
    return ingress_queue_.try_pop();
}

std::vector<market_data_push_system::BroadcastMessage> UnifiedAccessGateway::replay_route_events(
    std::uint64_t sequence_after) const
{
    return broadcaster_.replay("gateway.routes", sequence_after);
}

std::size_t UnifiedAccessGateway::live_session_count() const
{
    return static_cast<std::size_t>(
        std::count_if(sessions_.begin(), sessions_.end(), [](const auto &value) { return value.second.live; }));
}

std::string UnifiedAccessGateway::route_event_payload(const ForwardedRequest &request, std::string_view route_name)
{
    return request.request_id + ":" + std::string(route_name) + ":" + request.path;
}

std::optional<UnifiedAccessGateway::RouteConfig> UnifiedAccessGateway::match_route(std::string_view path) const
{
    for (const auto &route : routes_)
    {
        if (path.starts_with(route.path_prefix))
        {
            return route;
        }
    }

    return std::nullopt;
}

std::string expected_signature(std::string_view api_key, std::string_view secret, std::string_view request_id)
{
    return std::string(api_key) + ":" + std::string(secret) + ":" + std::string(request_id);
}

ModuleSummary module_summary()
{
    return {
        .module_name = project_name(),
        .ingress_controls = 5,
        .infrastructure_reuse_points = 2,
    };
}

std::string project_name()
{
    return "unified_access_gateway";
}

} // namespace unified_access_gateway
