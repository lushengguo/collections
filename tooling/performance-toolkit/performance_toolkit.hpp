#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "object_pool.hpp"
#include "spsc_ring_queue.hpp"

namespace performance_toolkit
{

enum class FlowPattern
{
    kLimitOnly,
    kMarketBurst,
    kMixed,
};

struct WorkloadConfig
{
    std::string scenario_name;
    FlowPattern pattern{FlowPattern::kLimitOnly};
    std::size_t request_count{0};
    std::size_t burst_size{0};
    std::size_t concurrency{1};
    std::int64_t base_timestamp_ms{0};
};

struct SyntheticRequest
{
    std::string request_id;
    bool is_market{false};
    double price{0.0};
    double quantity{0.0};
    std::int64_t timestamp_ms{0};
};

struct LatencySummary
{
    std::size_t samples{0};
    std::uint64_t min_ns{0};
    std::uint64_t p50_ns{0};
    std::uint64_t p99_ns{0};
    std::uint64_t p999_ns{0};
    std::uint64_t max_ns{0};
    double throughput_per_second{0.0};
    double success_ratio{0.0};
};

struct ModuleSummary
{
    std::string module_name;
    std::size_t toolkit_capabilities{0};
    std::size_t infrastructure_reuse_points{0};
};

class PerformanceToolkit
{
  public:
    explicit PerformanceToolkit(std::size_t queue_capacity = 4096);
    ~PerformanceToolkit();

    PerformanceToolkit(const PerformanceToolkit &) = delete;
    PerformanceToolkit &operator=(const PerformanceToolkit &) = delete;

    [[nodiscard]] std::size_t enqueue_workload(const WorkloadConfig &config);
    [[nodiscard]] std::optional<SyntheticRequest> poll_request();
    void record_latency(std::uint64_t latency_ns, bool success);
    [[nodiscard]] LatencySummary summarize(double duration_seconds) const;
    [[nodiscard]] std::string render_report(const std::string &scenario_name, double duration_seconds) const;
    [[nodiscard]] std::size_t dropped_requests() const noexcept;
    [[nodiscard]] std::size_t pending_requests() const noexcept;

  private:
    struct RequestEnvelope
    {
        SyntheticRequest request;
    };

    struct LatencyRecord
    {
        std::uint64_t latency_ns{0};
        bool success{true};
    };

    [[nodiscard]] static SyntheticRequest make_request(const WorkloadConfig &config, std::size_t index);
    [[nodiscard]] static std::uint64_t percentile(const std::vector<std::uint64_t> &sorted_values, double rank);
    void drain_pending();

    lock_free_structures::SpscRingQueue<RequestEnvelope *> queue_;
    memory_pool::ObjectPool<RequestEnvelope> envelope_pool_;
    std::vector<LatencyRecord> latencies_;
    std::size_t dropped_requests_{0};
};

[[nodiscard]] ModuleSummary module_summary();

[[nodiscard]] std::string project_name();

} // namespace performance_toolkit
