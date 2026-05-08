#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "object_pool.hpp"
#include "spsc_ring_queue.hpp"

namespace observability_stack
{

enum class EventKind
{
    kMetric,
    kSpan,
    kAlert,
};

enum class MetricType
{
    kCounter,
    kGauge,
    kHistogram,
};

enum class AlertSeverity
{
    kInfo,
    kWarning,
    kCritical,
};

struct TraceContext
{
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::string component;
};

struct MetricPoint
{
    std::string name;
    double value{0.0};
    std::int64_t timestamp{0};
    MetricType type{MetricType::kCounter};
    TraceContext trace_context;
};

struct TraceSpan
{
    TraceContext context;
    std::string operation;
    std::int64_t start_timestamp{0};
    std::int64_t end_timestamp{0};
    bool success{true};
};

struct AlertRule
{
    std::string rule_id;
    std::string metric_name;
    double threshold{0.0};
    std::size_t consecutive_breaches{1};
    AlertSeverity severity{AlertSeverity::kWarning};
};

struct AlertNotification
{
    std::string rule_id;
    std::string metric_name;
    double observed_value{0.0};
    double threshold{0.0};
    std::size_t breach_count{0};
    std::int64_t timestamp{0};
    AlertSeverity severity{AlertSeverity::kWarning};
    TraceContext trace_context;
};

struct MetricSnapshot
{
    std::string name;
    MetricType type{MetricType::kCounter};
    std::uint64_t samples{0};
    double sum{0.0};
    double last{0.0};
    double min{0.0};
    double max{0.0};
};

struct TelemetryEvent
{
    EventKind kind{EventKind::kMetric};
    std::string name;
    std::string component;
    std::string trace_id;
    double value{0.0};
    std::int64_t timestamp{0};
    bool success{true};
    AlertSeverity severity{AlertSeverity::kInfo};
};

struct ModuleSummary
{
    std::string module_name;
    std::size_t core_capabilities{0};
    std::size_t infrastructure_reuse_points{0};
};

enum class PublishStatus
{
    kEnqueued,
    kQueueFull,
};

struct PublishResult
{
    PublishStatus status{PublishStatus::kEnqueued};

    [[nodiscard]] bool ok() const noexcept
    {
        return status == PublishStatus::kEnqueued;
    }
};

class ObservabilityStack
{
  public:
    explicit ObservabilityStack(std::size_t queue_capacity = 1024, std::size_t history_limit = 4096);
    ~ObservabilityStack();

    ObservabilityStack(const ObservabilityStack &) = delete;
    ObservabilityStack &operator=(const ObservabilityStack &) = delete;

    [[nodiscard]] PublishResult publish_metric(MetricPoint point);
    [[nodiscard]] PublishResult publish_span(TraceSpan span);
    void register_rule(AlertRule rule);
    [[nodiscard]] std::size_t flush();
    [[nodiscard]] std::optional<MetricSnapshot> metric_snapshot(std::string_view metric_name) const;
    [[nodiscard]] std::vector<AlertNotification> alerts() const;
    [[nodiscard]] std::vector<TelemetryEvent> history() const;
    [[nodiscard]] std::size_t pending_events() const noexcept;
    [[nodiscard]] std::size_t dropped_events() const noexcept;

  private:
    struct Envelope
    {
        EventKind kind{EventKind::kMetric};
        MetricPoint metric;
        TraceSpan span;
    };

    struct MetricAccumulator
    {
        MetricType type{MetricType::kCounter};
        std::uint64_t samples{0};
        double sum{0.0};
        double last{0.0};
        double min{0.0};
        double max{0.0};
    };

    [[nodiscard]] Envelope *make_metric_envelope(MetricPoint &&point);
    [[nodiscard]] Envelope *make_span_envelope(TraceSpan &&span);
    void process_metric(const MetricPoint &point);
    void process_span(const TraceSpan &span);
    void evaluate_rules(const MetricPoint &point);
    void emit_alert(const AlertRule &rule, const MetricPoint &point, std::size_t breach_count);
    void record_event(TelemetryEvent event);
    void drain_and_dispose_pending();

    std::size_t history_limit_;
    lock_free_structures::SpscRingQueue<Envelope *> queue_;
    memory_pool::ObjectPool<Envelope> envelope_pool_;
    std::unordered_map<std::string, MetricAccumulator> metrics_;
    std::unordered_map<std::string, AlertRule> rules_;
    std::unordered_map<std::string, std::size_t> breach_counts_;
    std::vector<AlertNotification> alerts_;
    std::deque<TelemetryEvent> history_;
    std::size_t dropped_events_{0};
};

[[nodiscard]] TraceContext child_context(const TraceContext &parent, std::string_view component,
                                         std::uint64_t sequence);

[[nodiscard]] ModuleSummary module_summary();

[[nodiscard]] std::string project_name();

} // namespace observability_stack
