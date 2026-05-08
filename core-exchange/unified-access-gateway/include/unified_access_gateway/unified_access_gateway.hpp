#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "lock_free_structures/spsc_ring_queue.hpp"
#include "market_data_push_system/broadcaster.hpp"

namespace unified_access_gateway
{

enum class Protocol
{
    kRest,
    kWebSocket,
    kFix,
};

enum class Backend
{
    kRisk,
    kMatching,
    kClearing,
    kMarketData,
};

enum class RejectReason
{
    kNone,
    kInvalidRequest,
    kAuthenticationFailed,
    kRateLimited,
    kUnknownRoute,
    kCircuitOpen,
    kQueueFull,
};

struct GatewayRequest
{
    std::string request_id;
    std::string user_id;
    std::string api_key;
    std::string signature;
    std::string path;
    std::string payload;
    Protocol protocol{Protocol::kRest};
    std::int64_t timestamp_ms{0};
};

struct ForwardedRequest
{
    std::string request_id;
    std::string user_id;
    std::string path;
    std::string payload;
    Backend backend{Backend::kRisk};
    Protocol protocol{Protocol::kRest};
    std::int64_t timestamp_ms{0};
};

struct RouteResult
{
    bool accepted{false};
    RejectReason reject_reason{RejectReason::kNone};
    Backend backend{Backend::kRisk};
    std::string route_name;
    std::size_t queued_requests{0};
};

struct ModuleSummary
{
    std::string module_name;
    std::size_t ingress_controls{0};
    std::size_t infrastructure_reuse_points{0};
};

class UnifiedAccessGateway
{
  public:
    explicit UnifiedAccessGateway(std::size_t ingress_queue_capacity = 1024);

    void register_credential(std::string api_key, std::string secret, std::string user_id);
    void configure_route(std::string path_prefix, Backend backend, std::string route_name);
    void set_user_rate_limit(std::string user_id, std::size_t max_requests, std::int64_t window_ms);
    void set_backend_health(Backend backend, bool healthy);
    void open_session(std::string session_id, Protocol protocol, std::int64_t heartbeat_timeout_ms,
                      std::int64_t timestamp_ms);
    [[nodiscard]] bool record_heartbeat(std::string_view session_id, std::int64_t timestamp_ms);
    [[nodiscard]] std::size_t close_stale_sessions(std::int64_t now_ms);

    [[nodiscard]] RouteResult route(const GatewayRequest &request);
    [[nodiscard]] std::optional<ForwardedRequest> poll_forwarded_request();
    [[nodiscard]] std::vector<market_data_push_system::BroadcastMessage> replay_route_events(
        std::uint64_t sequence_after) const;
    [[nodiscard]] std::size_t live_session_count() const;

  private:
    struct Credential
    {
        std::string secret;
        std::string user_id;
    };

    struct RouteConfig
    {
        std::string path_prefix;
        Backend backend{Backend::kRisk};
        std::string route_name;
    };

    struct RateLimit
    {
        std::size_t max_requests{0};
        std::int64_t window_ms{0};
    };

    struct SessionState
    {
        Protocol protocol{Protocol::kRest};
        std::int64_t heartbeat_timeout_ms{0};
        std::int64_t last_heartbeat_ms{0};
        bool live{true};
    };

    [[nodiscard]] static std::string route_event_payload(const ForwardedRequest &request, std::string_view route_name);
    [[nodiscard]] std::optional<RouteConfig> match_route(std::string_view path) const;

    std::unordered_map<std::string, Credential> credentials_;
    std::vector<RouteConfig> routes_;
    std::unordered_map<std::string, RateLimit> user_limits_;
    std::unordered_map<std::string, std::vector<std::int64_t>> user_windows_;
    std::unordered_map<Backend, bool> backend_health_;
    std::unordered_map<std::string, SessionState> sessions_;
    lock_free_structures::SpscRingQueue<ForwardedRequest> ingress_queue_;
    market_data_push_system::MarketDataBroadcaster broadcaster_;
};

[[nodiscard]] std::string expected_signature(std::string_view api_key, std::string_view secret,
                                             std::string_view request_id);

[[nodiscard]] ModuleSummary module_summary();

[[nodiscard]] std::string project_name();

} // namespace unified_access_gateway
