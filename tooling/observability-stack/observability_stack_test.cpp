#include <gtest/gtest.h>

#include "observability_stack.hpp"

TEST(ObservabilityStackTest, ProjectNameIsStable)
{
    EXPECT_EQ(observability_stack::project_name(), "observability_stack");
}

TEST(ObservabilityStackTest, ModuleSummaryReportsCapabilities)
{
    const auto summary = observability_stack::module_summary();
    EXPECT_EQ(summary.module_name, "observability_stack");
    EXPECT_EQ(summary.core_capabilities, 4U);
    EXPECT_EQ(summary.infrastructure_reuse_points, 2U);
}

TEST(ObservabilityStackTest, ChildContextPreservesTraceAndLinksParent)
{
    const observability_stack::TraceContext parent{
        .trace_id = "trace-1",
        .span_id = "gateway-1",
        .parent_span_id = "",
        .component = "gateway",
    };

    const auto child = observability_stack::child_context(parent, "risk", 7);
    EXPECT_EQ(child.trace_id, "trace-1");
    EXPECT_EQ(child.span_id, "gateway-1.7");
    EXPECT_EQ(child.parent_span_id, "gateway-1");
    EXPECT_EQ(child.component, "risk");
}

TEST(ObservabilityStackTest, FlushAggregatesMetricSamplesIntoSnapshot)
{
    observability_stack::ObservabilityStack stack(16);

    EXPECT_TRUE(stack.publish_metric({
        .name = "gateway.requests",
        .value = 100.0,
        .timestamp = 1,
        .type = observability_stack::MetricType::kCounter,
        .trace_context = {.trace_id = "t-1", .span_id = "s-1", .parent_span_id = "", .component = "gateway"},
    }));
    EXPECT_TRUE(stack.publish_metric({
        .name = "gateway.requests",
        .value = 120.0,
        .timestamp = 2,
        .type = observability_stack::MetricType::kCounter,
        .trace_context = {.trace_id = "t-1", .span_id = "s-2", .parent_span_id = "", .component = "gateway"},
    }));

    EXPECT_EQ(stack.flush(), 2U);

    const auto snapshot = stack.metric_snapshot("gateway.requests");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->samples, 2U);
    EXPECT_DOUBLE_EQ(snapshot->sum, 220.0);
    EXPECT_DOUBLE_EQ(snapshot->min, 100.0);
    EXPECT_DOUBLE_EQ(snapshot->max, 120.0);
    EXPECT_DOUBLE_EQ(snapshot->last, 120.0);

    const auto history = stack.history();
    ASSERT_EQ(history.size(), 2U);
    EXPECT_EQ(history.front().trace_id, "t-1");
}

TEST(ObservabilityStackTest, ThresholdAlertRequiresConsecutiveBreaches)
{
    observability_stack::ObservabilityStack stack(16);
    stack.register_rule({
        .rule_id = "latency-p99",
        .metric_name = "gateway.latency.p99",
        .threshold = 20.0,
        .consecutive_breaches = 2,
        .severity = observability_stack::AlertSeverity::kCritical,
    });

    EXPECT_TRUE(stack.publish_metric({
        .name = "gateway.latency.p99",
        .value = 21.0,
        .timestamp = 1,
        .type = observability_stack::MetricType::kGauge,
        .trace_context = {.trace_id = "trace-a", .span_id = "span-a", .parent_span_id = "", .component = "gateway"},
    }));
    EXPECT_TRUE(stack.publish_metric({
        .name = "gateway.latency.p99",
        .value = 22.0,
        .timestamp = 2,
        .type = observability_stack::MetricType::kGauge,
        .trace_context = {.trace_id = "trace-a", .span_id = "span-b", .parent_span_id = "", .component = "gateway"},
    }));

    EXPECT_EQ(stack.flush(), 2U);

    const auto alerts = stack.alerts();
    ASSERT_EQ(alerts.size(), 1U);
    EXPECT_EQ(alerts.front().rule_id, "latency-p99");
    EXPECT_EQ(alerts.front().severity, observability_stack::AlertSeverity::kCritical);

    const auto history = stack.history();
    ASSERT_EQ(history.size(), 3U);
    EXPECT_EQ(history.back().kind, observability_stack::EventKind::kAlert);
}

TEST(ObservabilityStackTest, QueueOverflowIncrementsDroppedEventCount)
{
    observability_stack::ObservabilityStack stack(2);

    EXPECT_TRUE(stack.publish_metric({.name = "metric-1", .value = 1.0, .timestamp = 1}));
    EXPECT_TRUE(stack.publish_metric({.name = "metric-2", .value = 2.0, .timestamp = 2}));
    EXPECT_FALSE(stack.publish_metric({.name = "metric-3", .value = 3.0, .timestamp = 3}));

    EXPECT_EQ(stack.dropped_events(), 1U);
    EXPECT_EQ(stack.pending_events(), 2U);
}

TEST(ObservabilityStackTest, SpanEventsCaptureDurationAndSuccessFlag)
{
    observability_stack::ObservabilityStack stack(8);
    EXPECT_TRUE(stack.publish_span({
        .context = {.trace_id = "trace-7", .span_id = "gateway-7", .parent_span_id = "", .component = "gateway"},
        .operation = "place_order",
        .start_timestamp = 100,
        .end_timestamp = 145,
        .success = true,
    }));

    EXPECT_EQ(stack.flush(), 1U);
    const auto history = stack.history();
    ASSERT_EQ(history.size(), 1U);
    EXPECT_EQ(history.front().kind, observability_stack::EventKind::kSpan);
    EXPECT_EQ(history.front().name, "place_order");
    EXPECT_DOUBLE_EQ(history.front().value, 45.0);
    EXPECT_TRUE(history.front().success);
}
