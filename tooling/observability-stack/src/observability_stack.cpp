#include "observability_stack/observability_stack.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace observability_stack
{

namespace
{

constexpr std::size_t kEnvelopePoolBlocksPerSlab = 512;
constexpr std::size_t kEnvelopePoolLocalCacheLimit = 128;

} // namespace

ObservabilityStack::ObservabilityStack(std::size_t queue_capacity, std::size_t history_limit)
    : history_limit_(history_limit), queue_(queue_capacity),
      envelope_pool_(kEnvelopePoolBlocksPerSlab, kEnvelopePoolLocalCacheLimit)
{
    if (history_limit_ == 0)
    {
        throw std::invalid_argument("ObservabilityStack history_limit must be positive");
    }
}

ObservabilityStack::~ObservabilityStack()
{
    drain_and_dispose_pending();
}

bool ObservabilityStack::publish_metric(MetricPoint point)
{
    auto *envelope = make_metric_envelope(std::move(point));
    if (!queue_.push(envelope))
    {
        envelope_pool_.destroy(envelope);
        ++dropped_events_;
        return false;
    }

    return true;
}

bool ObservabilityStack::publish_span(TraceSpan span)
{
    auto *envelope = make_span_envelope(std::move(span));
    if (!queue_.push(envelope))
    {
        envelope_pool_.destroy(envelope);
        ++dropped_events_;
        return false;
    }

    return true;
}

void ObservabilityStack::register_rule(AlertRule rule)
{
    if (rule.rule_id.empty())
    {
        throw std::invalid_argument("AlertRule rule_id must not be empty");
    }
    if (rule.metric_name.empty())
    {
        throw std::invalid_argument("AlertRule metric_name must not be empty");
    }
    if (rule.consecutive_breaches == 0)
    {
        throw std::invalid_argument("AlertRule consecutive_breaches must be positive");
    }

    breach_counts_.erase(rule.rule_id);
    rules_[rule.rule_id] = std::move(rule);
}

std::size_t ObservabilityStack::flush()
{
    std::size_t processed = 0;
    while (auto envelope = queue_.try_pop())
    {
        Envelope *value = *envelope;
        switch (value->kind)
        {
        case EventKind::kMetric:
            process_metric(value->metric);
            break;
        case EventKind::kSpan:
            process_span(value->span);
            break;
        case EventKind::kAlert:
            break;
        }
        envelope_pool_.destroy(value);
        ++processed;
    }
    return processed;
}

std::optional<MetricSnapshot> ObservabilityStack::metric_snapshot(std::string_view metric_name) const
{
    const auto it = metrics_.find(std::string(metric_name));
    if (it == metrics_.end())
    {
        return std::nullopt;
    }

    const auto &accumulator = it->second;
    return MetricSnapshot{
        .name = it->first,
        .type = accumulator.type,
        .samples = accumulator.samples,
        .sum = accumulator.sum,
        .last = accumulator.last,
        .min = accumulator.min,
        .max = accumulator.max,
    };
}

std::vector<AlertNotification> ObservabilityStack::alerts() const
{
    return alerts_;
}

std::vector<TelemetryEvent> ObservabilityStack::history() const
{
    return history_;
}

std::size_t ObservabilityStack::pending_events() const noexcept
{
    return queue_.size_approx();
}

std::size_t ObservabilityStack::dropped_events() const noexcept
{
    return dropped_events_;
}

ObservabilityStack::Envelope *ObservabilityStack::make_metric_envelope(MetricPoint &&point)
{
    auto *envelope = envelope_pool_.create();
    envelope->kind = EventKind::kMetric;
    envelope->metric = std::move(point);
    envelope->span = {};
    return envelope;
}

ObservabilityStack::Envelope *ObservabilityStack::make_span_envelope(TraceSpan &&span)
{
    auto *envelope = envelope_pool_.create();
    envelope->kind = EventKind::kSpan;
    envelope->metric = {};
    envelope->span = std::move(span);
    return envelope;
}

void ObservabilityStack::process_metric(const MetricPoint &point)
{
    auto &accumulator = metrics_[point.name];
    if (accumulator.samples == 0)
    {
        accumulator.type = point.type;
        accumulator.min = point.value;
        accumulator.max = point.value;
    }
    else
    {
        accumulator.min = std::min(accumulator.min, point.value);
        accumulator.max = std::max(accumulator.max, point.value);
    }

    ++accumulator.samples;
    accumulator.sum += point.value;
    accumulator.last = point.value;

    record_event({
        .kind = EventKind::kMetric,
        .name = point.name,
        .component = point.trace_context.component,
        .trace_id = point.trace_context.trace_id,
        .value = point.value,
        .timestamp = point.timestamp,
        .success = true,
        .severity = AlertSeverity::kInfo,
    });

    evaluate_rules(point);
}

void ObservabilityStack::process_span(const TraceSpan &span)
{
    const auto duration = static_cast<double>(span.end_timestamp - span.start_timestamp);
    record_event({
        .kind = EventKind::kSpan,
        .name = span.operation,
        .component = span.context.component,
        .trace_id = span.context.trace_id,
        .value = duration,
        .timestamp = span.end_timestamp,
        .success = span.success,
        .severity = AlertSeverity::kInfo,
    });
}

void ObservabilityStack::evaluate_rules(const MetricPoint &point)
{
    for (const auto &[rule_id, rule] : rules_)
    {
        if (rule.metric_name != point.name)
        {
            continue;
        }

        auto &breach_count = breach_counts_[rule_id];
        if (point.value >= rule.threshold)
        {
            ++breach_count;
            if (breach_count == rule.consecutive_breaches)
            {
                emit_alert(rule, point, breach_count);
            }
            breach_count = std::min(breach_count, rule.consecutive_breaches);
        }
        else
        {
            breach_count = 0;
        }
    }
}

void ObservabilityStack::emit_alert(const AlertRule &rule, const MetricPoint &point, std::size_t breach_count)
{
    alerts_.push_back({
        .rule_id = rule.rule_id,
        .metric_name = rule.metric_name,
        .observed_value = point.value,
        .threshold = rule.threshold,
        .breach_count = breach_count,
        .timestamp = point.timestamp,
        .severity = rule.severity,
        .trace_context = point.trace_context,
    });

    record_event({
        .kind = EventKind::kAlert,
        .name = rule.rule_id,
        .component = point.trace_context.component,
        .trace_id = point.trace_context.trace_id,
        .value = point.value,
        .timestamp = point.timestamp,
        .success = false,
        .severity = rule.severity,
    });
}

void ObservabilityStack::record_event(TelemetryEvent event)
{
    if (history_.size() == history_limit_)
    {
        history_.erase(history_.begin());
    }
    history_.push_back(std::move(event));
}

void ObservabilityStack::drain_and_dispose_pending()
{
    while (auto envelope = queue_.try_pop())
    {
        envelope_pool_.destroy(*envelope);
    }
}

TraceContext child_context(const TraceContext &parent, std::string component, std::uint64_t sequence)
{
    const auto suffix = std::to_string(sequence);
    TraceContext child{
        .trace_id = parent.trace_id.empty() ? component + "-trace-" + suffix : parent.trace_id,
        .span_id = parent.span_id.empty() ? component + "-span-" + suffix : parent.span_id + "." + suffix,
        .parent_span_id = parent.span_id,
        .component = std::move(component),
    };
    return child;
}

ModuleSummary module_summary()
{
    return {
        .module_name = project_name(),
        .core_capabilities = 4,
        .infrastructure_reuse_points = 2,
    };
}

std::string project_name()
{
    return "observability_stack";
}

} // namespace observability_stack
