#include <gtest/gtest.h>

#include "unified_access_gateway/unified_access_gateway.hpp"

TEST(UnifiedAccessGatewayTest, ProjectNameIsStable)
{
    EXPECT_EQ(unified_access_gateway::project_name(), "unified_access_gateway");
}

TEST(UnifiedAccessGatewayTest, ModuleSummaryReportsControlsAndReuse)
{
    const auto summary = unified_access_gateway::module_summary();
    EXPECT_EQ(summary.module_name, "unified_access_gateway");
    EXPECT_EQ(summary.ingress_controls, 5U);
    EXPECT_EQ(summary.infrastructure_reuse_points, 2U);
}

TEST(UnifiedAccessGatewayTest, AuthenticatedRequestRoutesToConfiguredBackend)
{
    unified_access_gateway::UnifiedAccessGateway gateway;
    gateway.register_credential("key-1", "secret-1", "alice");
    gateway.configure_route("/v1/orders", unified_access_gateway::Backend::kMatching, "matching_http");

    const auto result = gateway.route({
        .request_id = "req-1",
        .user_id = "alice",
        .api_key = "key-1",
        .signature = unified_access_gateway::expected_signature("key-1", "secret-1", "req-1"),
        .path = "/v1/orders/place",
        .payload = "{}",
        .protocol = unified_access_gateway::Protocol::kRest,
        .timestamp_ms = 1,
    });

    ASSERT_TRUE(result.accepted);
    EXPECT_EQ(result.backend, unified_access_gateway::Backend::kMatching);
    EXPECT_EQ(result.route_name, "matching_http");

    const auto forwarded = gateway.poll_forwarded_request();
    ASSERT_TRUE(forwarded.has_value());
    EXPECT_EQ(forwarded->request_id, "req-1");
}

TEST(UnifiedAccessGatewayTest, RejectsInvalidSignature)
{
    unified_access_gateway::UnifiedAccessGateway gateway;
    gateway.register_credential("key-1", "secret-1", "alice");
    gateway.configure_route("/v1/orders", unified_access_gateway::Backend::kMatching, "matching_http");

    const auto result = gateway.route({
        .request_id = "req-2",
        .user_id = "alice",
        .api_key = "key-1",
        .signature = "bad-signature",
        .path = "/v1/orders/place",
        .payload = "{}",
        .protocol = unified_access_gateway::Protocol::kRest,
        .timestamp_ms = 1,
    });

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, unified_access_gateway::RejectReason::kAuthenticationFailed);
}

TEST(UnifiedAccessGatewayTest, RejectsWhenUserRateLimitIsExceeded)
{
    unified_access_gateway::UnifiedAccessGateway gateway;
    gateway.register_credential("key-1", "secret-1", "alice");
    gateway.configure_route("/v1/orders", unified_access_gateway::Backend::kMatching, "matching_http");
    gateway.set_user_rate_limit("alice", 2, 1000);

    const auto sign = [&](const std::string &request_id) {
        return unified_access_gateway::expected_signature("key-1", "secret-1", request_id);
    };

    EXPECT_TRUE(gateway
                    .route({.request_id = "req-3",
                            .user_id = "alice",
                            .api_key = "key-1",
                            .signature = sign("req-3"),
                            .path = "/v1/orders/place",
                            .payload = "{}",
                            .protocol = unified_access_gateway::Protocol::kRest,
                            .timestamp_ms = 1})
                    .accepted);
    EXPECT_TRUE(gateway
                    .route({.request_id = "req-4",
                            .user_id = "alice",
                            .api_key = "key-1",
                            .signature = sign("req-4"),
                            .path = "/v1/orders/place",
                            .payload = "{}",
                            .protocol = unified_access_gateway::Protocol::kRest,
                            .timestamp_ms = 2})
                    .accepted);
    const auto third = gateway.route({.request_id = "req-5",
                                      .user_id = "alice",
                                      .api_key = "key-1",
                                      .signature = sign("req-5"),
                                      .path = "/v1/orders/place",
                                      .payload = "{}",
                                      .protocol = unified_access_gateway::Protocol::kRest,
                                      .timestamp_ms = 3});

    EXPECT_FALSE(third.accepted);
    EXPECT_EQ(third.reject_reason, unified_access_gateway::RejectReason::kRateLimited);
}

TEST(UnifiedAccessGatewayTest, CircuitBreakerBlocksUnhealthyBackend)
{
    unified_access_gateway::UnifiedAccessGateway gateway;
    gateway.register_credential("key-1", "secret-1", "alice");
    gateway.configure_route("/v1/risk", unified_access_gateway::Backend::kRisk, "risk_http");
    gateway.set_backend_health(unified_access_gateway::Backend::kRisk, false);

    const auto result = gateway.route({
        .request_id = "req-6",
        .user_id = "alice",
        .api_key = "key-1",
        .signature = unified_access_gateway::expected_signature("key-1", "secret-1", "req-6"),
        .path = "/v1/risk/check",
        .payload = "{}",
        .protocol = unified_access_gateway::Protocol::kRest,
        .timestamp_ms = 1,
    });

    EXPECT_FALSE(result.accepted);
    EXPECT_EQ(result.reject_reason, unified_access_gateway::RejectReason::kCircuitOpen);
}

TEST(UnifiedAccessGatewayTest, RouteEventsCanBeReplayed)
{
    unified_access_gateway::UnifiedAccessGateway gateway;
    gateway.register_credential("key-1", "secret-1", "alice");
    gateway.configure_route("/ws/market-data", unified_access_gateway::Backend::kMarketData, "md_ws");

    const auto result = gateway.route({
        .request_id = "req-7",
        .user_id = "alice",
        .api_key = "key-1",
        .signature = unified_access_gateway::expected_signature("key-1", "secret-1", "req-7"),
        .path = "/ws/market-data/depth",
        .payload = "subscribe",
        .protocol = unified_access_gateway::Protocol::kWebSocket,
        .timestamp_ms = 1,
    });

    ASSERT_TRUE(result.accepted);
    const auto replay = gateway.replay_route_events(0);
    ASSERT_EQ(replay.size(), 1U);
    EXPECT_EQ(replay.front().topic, "gateway.routes");
}

TEST(UnifiedAccessGatewayTest, StaleSessionsCanBeClosed)
{
    unified_access_gateway::UnifiedAccessGateway gateway;
    gateway.open_session("session-1", unified_access_gateway::Protocol::kWebSocket, 1000, 0);
    gateway.open_session("session-2", unified_access_gateway::Protocol::kFix, 1000, 500);

    EXPECT_TRUE(gateway.record_heartbeat("session-2", 1200));
    EXPECT_EQ(gateway.close_stale_sessions(1501), 1U);
    EXPECT_EQ(gateway.live_session_count(), 1U);
}
