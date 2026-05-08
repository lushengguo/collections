#include <gtest/gtest.h>

#include <string>

#include "distributed_consistency.hpp"

TEST(DistributedConsistencyTest, ProjectNameIsStable)
{
    EXPECT_EQ(distributed_consistency::project_name(), "distributed_consistency");
}

TEST(DistributedConsistencyTest, SagaCoordinatorCompletesSuccessfulWorkflow)
{
    distributed_consistency::SagaCoordinator coordinator;
    distributed_consistency::SagaContext context;

    coordinator.add_step(
        "reserve-funds",
        [](distributed_consistency::SagaContext &saga_context) {
            saga_context.attributes["reserve"] = "done";
            return distributed_consistency::StepResult::kSuccess;
        },
        [](distributed_consistency::SagaContext &saga_context) { saga_context.attributes["reserve"] = "compensated"; });

    coordinator.add_step(
        "publish-outbox",
        [](distributed_consistency::SagaContext &saga_context) {
            saga_context.attributes["outbox"] = "done";
            return distributed_consistency::StepResult::kSuccess;
        },
        [](distributed_consistency::SagaContext &saga_context) { saga_context.attributes["outbox"] = "compensated"; });

    const auto report = coordinator.execute(context);

    EXPECT_TRUE(report.committed);
    EXPECT_EQ(report.completed_steps.size(), 2U);
    EXPECT_FALSE(report.failed_step.has_value());
}

TEST(DistributedConsistencyTest, SagaCoordinatorCompensatesCompletedStepsOnFailure)
{
    distributed_consistency::SagaCoordinator coordinator;
    distributed_consistency::SagaContext context;

    coordinator.add_step(
        "reserve-funds",
        [](distributed_consistency::SagaContext &saga_context) {
            saga_context.attributes["reserve"] = "done";
            return distributed_consistency::StepResult::kSuccess;
        },
        [](distributed_consistency::SagaContext &saga_context) { saga_context.attributes["reserve"] = "compensated"; });

    coordinator.add_step(
        "settle-ledger",
        [](distributed_consistency::SagaContext &) { return distributed_consistency::StepResult::kPermanentFailure; },
        [](distributed_consistency::SagaContext &) {});

    const auto report = coordinator.execute(context);

    EXPECT_FALSE(report.committed);
    ASSERT_TRUE(report.failed_step.has_value());
    EXPECT_EQ(*report.failed_step, "settle-ledger");
    EXPECT_EQ(context.attributes["reserve"], "compensated");
    EXPECT_EQ(report.compensated_steps.size(), 1U);
}

TEST(DistributedConsistencyTest, TccWalletSupportsIdempotentReserveConfirmAndCancel)
{
    distributed_consistency::TccWallet wallet(500.0);

    EXPECT_TRUE(wallet.try_reserve("r-1", 125.0));
    EXPECT_TRUE(wallet.try_reserve("r-1", 125.0));
    EXPECT_DOUBLE_EQ(wallet.available_balance(), 375.0);
    EXPECT_DOUBLE_EQ(wallet.reserved_balance(), 125.0);

    EXPECT_TRUE(wallet.confirm("r-1"));
    EXPECT_TRUE(wallet.confirm("r-1"));
    EXPECT_DOUBLE_EQ(wallet.reserved_balance(), 0.0);
    EXPECT_DOUBLE_EQ(wallet.committed_balance(), 125.0);

    EXPECT_FALSE(wallet.cancel("r-1"));
}

TEST(DistributedConsistencyTest, OutboxStoreTracksPendingMessages)
{
    distributed_consistency::OutboxStore outbox;
    outbox.append({.id = "m-1", .topic = "orders", .payload = "created"});
    outbox.append({.id = "m-2", .topic = "orders", .payload = "settled"});

    const auto batch = outbox.pending_batch(10);
    ASSERT_EQ(batch.size(), 2U);
    EXPECT_EQ(outbox.pending_count(), 2U);

    EXPECT_TRUE(outbox.mark_dispatched("m-1"));
    EXPECT_EQ(outbox.pending_count(), 1U);
}

TEST(DistributedConsistencyTest, ModuleSummaryReportsComponentCount)
{
    const auto summary = distributed_consistency::module_summary();
    EXPECT_EQ(summary.module_name, "distributed_consistency");
    EXPECT_EQ(summary.consistency_components, 3U);
}
