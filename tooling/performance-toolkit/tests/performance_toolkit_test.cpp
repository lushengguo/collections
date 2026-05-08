#include <gtest/gtest.h>

#include "performance_toolkit/performance_toolkit.hpp"

TEST(PerformanceToolkitTest, ProjectNameIsStable)
{
    EXPECT_EQ(performance_toolkit::project_name(), "performance_toolkit");
}

TEST(PerformanceToolkitTest, ModuleSummaryReportsCapabilitiesAndReuse)
{
    const auto summary = performance_toolkit::module_summary();
    EXPECT_EQ(summary.module_name, "performance_toolkit");
    EXPECT_EQ(summary.toolkit_capabilities, 4U);
    EXPECT_EQ(summary.infrastructure_reuse_points, 2U);
}

TEST(PerformanceToolkitTest, EnqueueWorkloadProducesDeterministicRequests)
{
    performance_toolkit::PerformanceToolkit toolkit(16);
    EXPECT_EQ(toolkit.enqueue_workload({
                  .scenario_name = "limit-flow",
                  .pattern = performance_toolkit::FlowPattern::kLimitOnly,
                  .request_count = 3,
                  .burst_size = 1,
                  .concurrency = 1,
                  .base_timestamp_ms = 100,
              }),
              3U);

    const auto first = toolkit.poll_request();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->request_id, "limit-flow-1");
    EXPECT_FALSE(first->is_market);
    EXPECT_DOUBLE_EQ(first->price, 9995.0);
}

TEST(PerformanceToolkitTest, MarketBurstPatternMarksBurstRequests)
{
    performance_toolkit::PerformanceToolkit toolkit(16);
    EXPECT_EQ(toolkit.enqueue_workload({
                  .scenario_name = "burst",
                  .pattern = performance_toolkit::FlowPattern::kMarketBurst,
                  .request_count = 4,
                  .burst_size = 2,
                  .concurrency = 1,
                  .base_timestamp_ms = 100,
              }),
              4U);

    const auto first = toolkit.poll_request();
    const auto second = toolkit.poll_request();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(first->is_market);
    EXPECT_FALSE(second->is_market);
}

TEST(PerformanceToolkitTest, SummaryComputesPercentilesAndSuccessRatio)
{
    performance_toolkit::PerformanceToolkit toolkit;
    toolkit.record_latency(100, true);
    toolkit.record_latency(200, true);
    toolkit.record_latency(300, false);
    toolkit.record_latency(400, true);

    const auto summary = toolkit.summarize(0.004);
    EXPECT_EQ(summary.samples, 4U);
    EXPECT_EQ(summary.min_ns, 100U);
    EXPECT_EQ(summary.p50_ns, 200U);
    EXPECT_EQ(summary.max_ns, 400U);
    EXPECT_DOUBLE_EQ(summary.success_ratio, 0.75);
}

TEST(PerformanceToolkitTest, RenderReportIncludesScenarioAndHint)
{
    performance_toolkit::PerformanceToolkit toolkit;
    toolkit.record_latency(1000, true);
    toolkit.record_latency(7000000, true);

    const auto report = toolkit.render_report("mixed-load", 0.01);
    EXPECT_NE(report.find("scenario=mixed-load"), std::string::npos);
    EXPECT_NE(report.find("bottleneck_hint=tail_latency_hot_path"), std::string::npos);
}

TEST(PerformanceToolkitTest, QueueOverflowCountsDroppedRequests)
{
    performance_toolkit::PerformanceToolkit toolkit(2);
    EXPECT_EQ(toolkit.enqueue_workload({
                  .scenario_name = "overflow",
                  .pattern = performance_toolkit::FlowPattern::kMixed,
                  .request_count = 3,
                  .burst_size = 1,
                  .concurrency = 1,
                  .base_timestamp_ms = 0,
              }),
              2U);
    EXPECT_EQ(toolkit.dropped_requests(), 1U);
    EXPECT_EQ(toolkit.pending_requests(), 2U);
}
