#include "performance_toolkit.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace performance_toolkit
{

PerformanceToolkit::PerformanceToolkit(std::size_t queue_capacity) : queue_(queue_capacity), envelope_pool_(512, 128)
{
}

PerformanceToolkit::~PerformanceToolkit()
{
    drain_pending();
}

std::size_t PerformanceToolkit::enqueue_workload(const WorkloadConfig &config)
{
    if (config.scenario_name.empty() || config.request_count == 0 || config.concurrency == 0)
    {
        throw std::invalid_argument("WorkloadConfig must specify scenario_name, request_count, and concurrency");
    }

    std::size_t accepted = 0;
    for (std::size_t index = 0; index < config.request_count; ++index)
    {
        auto *envelope = envelope_pool_.create(RequestEnvelope{.request = make_request(config, index)});
        if (!queue_.push(envelope))
        {
            envelope_pool_.destroy(envelope);
            ++dropped_requests_;
            continue;
        }
        ++accepted;
    }
    return accepted;
}

std::optional<SyntheticRequest> PerformanceToolkit::poll_request()
{
    const auto envelope = queue_.try_pop();
    if (!envelope.has_value())
    {
        return std::nullopt;
    }

    auto *const value = *envelope;
    SyntheticRequest request = std::move(value->request);
    envelope_pool_.destroy(value);
    return request;
}

void PerformanceToolkit::record_latency(std::uint64_t latency_ns, bool success)
{
    latencies_.push_back(LatencyRecord{.latency_ns = latency_ns, .success = success});
}

LatencySummary PerformanceToolkit::summarize(double duration_seconds) const
{
    if (latencies_.empty())
    {
        return {};
    }

    std::vector<std::uint64_t> sorted_latencies;
    sorted_latencies.reserve(latencies_.size());
    std::size_t successes = 0;
    for (const auto &record : latencies_)
    {
        sorted_latencies.push_back(record.latency_ns);
        if (record.success)
        {
            ++successes;
        }
    }
    std::sort(sorted_latencies.begin(), sorted_latencies.end());

    return LatencySummary{
        .samples = sorted_latencies.size(),
        .min_ns = sorted_latencies.front(),
        .p50_ns = percentile(sorted_latencies, 0.50),
        .p99_ns = percentile(sorted_latencies, 0.99),
        .p999_ns = percentile(sorted_latencies, 0.999),
        .max_ns = sorted_latencies.back(),
        .throughput_per_second =
            duration_seconds > 0.0 ? static_cast<double>(sorted_latencies.size()) / duration_seconds : 0.0,
        .success_ratio = static_cast<double>(successes) / static_cast<double>(sorted_latencies.size()),
    };
}

std::string PerformanceToolkit::render_report(std::string_view scenario_name, double duration_seconds) const
{
    const auto summary = summarize(duration_seconds);
    std::ostringstream report;
    report << "scenario=" << scenario_name << '\n';
    report << "samples=" << summary.samples << " throughput=" << summary.throughput_per_second << '\n';
    report << "p50_ns=" << summary.p50_ns << " p99_ns=" << summary.p99_ns << " p999_ns=" << summary.p999_ns << '\n';
    report << "success_ratio=" << summary.success_ratio << " dropped_requests=" << dropped_requests_ << '\n';
    if (summary.p99_ns > 5'000'000)
    {
        report << "bottleneck_hint=tail_latency_hot_path\n";
    }
    else if (dropped_requests_ > 0)
    {
        report << "bottleneck_hint=ingress_queue_pressure\n";
    }
    else
    {
        report << "bottleneck_hint=stable\n";
    }
    return report.str();
}

std::size_t PerformanceToolkit::dropped_requests() const noexcept
{
    return dropped_requests_;
}

std::size_t PerformanceToolkit::pending_requests() const noexcept
{
    return queue_.size_approx();
}

SyntheticRequest PerformanceToolkit::make_request(const WorkloadConfig &config, std::size_t index)
{
    const bool is_market = config.pattern == FlowPattern::kMarketBurst
                               ? (index % std::max<std::size_t>(config.burst_size, 1U) == 0)
                               : (config.pattern == FlowPattern::kMixed ? index % 3 == 0 : false);
    const auto spread = static_cast<double>(static_cast<int>(index % 11) - 5);
    return SyntheticRequest{
        .request_id = config.scenario_name + "-" + std::to_string(index + 1),
        .is_market = is_market,
        .price = is_market ? 0.0 : 10000.0 + spread,
        .quantity = 0.1 + static_cast<double>(index % 5) * 0.05,
        .timestamp_ms = config.base_timestamp_ms + static_cast<std::int64_t>(index / config.concurrency),
    };
}

std::uint64_t PerformanceToolkit::percentile(const std::vector<std::uint64_t> &sorted_values, double rank)
{
    if (sorted_values.empty())
    {
        return 0;
    }

    const auto index = static_cast<std::size_t>(std::ceil(rank * static_cast<double>(sorted_values.size())));
    return sorted_values[std::min(index == 0 ? 0U : index - 1U, sorted_values.size() - 1U)];
}

void PerformanceToolkit::drain_pending()
{
    while (auto envelope = queue_.try_pop())
    {
        envelope_pool_.destroy(*envelope);
    }
}

ModuleSummary module_summary()
{
    return {
        .module_name = project_name(),
        .toolkit_capabilities = 4,
        .infrastructure_reuse_points = 2,
    };
}

std::string project_name()
{
    return "performance_toolkit";
}

} // namespace performance_toolkit
