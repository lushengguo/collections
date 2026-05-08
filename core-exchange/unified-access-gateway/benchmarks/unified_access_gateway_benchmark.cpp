#include <benchmark/benchmark.h>

#include "unified_access_gateway/unified_access_gateway.hpp"

namespace
{

void configure_gateway(unified_access_gateway::UnifiedAccessGateway &gateway)
{
    gateway.register_credential("key-1", "secret-1", "alice");
    gateway.configure_route("/v1/orders", unified_access_gateway::Backend::kMatching, "matching_http");
    gateway.set_user_rate_limit("alice", 100000, 1000);
}

} // namespace

static void BM_RouteAcceptedRequests(benchmark::State &state)
{
    unified_access_gateway::UnifiedAccessGateway gateway(4096);
    configure_gateway(gateway);
    std::uint64_t sequence = 0;
    for (auto _ : state)
    {
        const auto request_id = "req-" + std::to_string(++sequence);
        auto result = gateway.route({
            .request_id = request_id,
            .user_id = "alice",
            .api_key = "key-1",
            .signature = unified_access_gateway::expected_signature("key-1", "secret-1", request_id),
            .path = "/v1/orders/place",
            .payload = "{}",
            .protocol = unified_access_gateway::Protocol::kRest,
            .timestamp_ms = static_cast<std::int64_t>(sequence),
        });
        benchmark::DoNotOptimize(&result);
        auto forwarded = gateway.poll_forwarded_request();
        benchmark::DoNotOptimize(&forwarded);
    }
}

static void BM_ReplayRouteEvents(benchmark::State &state)
{
    unified_access_gateway::UnifiedAccessGateway gateway(4096);
    configure_gateway(gateway);
    for (std::uint64_t index = 1; index <= 1024; ++index)
    {
        const auto request_id = "seed-" + std::to_string(index);
        (void)gateway.route({
            .request_id = request_id,
            .user_id = "alice",
            .api_key = "key-1",
            .signature = unified_access_gateway::expected_signature("key-1", "secret-1", request_id),
            .path = "/v1/orders/place",
            .payload = "{}",
            .protocol = unified_access_gateway::Protocol::kRest,
            .timestamp_ms = static_cast<std::int64_t>(index),
        });
        (void)gateway.poll_forwarded_request();
    }

    for (auto _ : state)
    {
        auto replay = gateway.replay_route_events(1000);
        benchmark::DoNotOptimize(&replay);
    }
}

BENCHMARK(BM_RouteAcceptedRequests)->Arg(1 << 12);
BENCHMARK(BM_ReplayRouteEvents)->Arg(1 << 10);
BENCHMARK_MAIN();
