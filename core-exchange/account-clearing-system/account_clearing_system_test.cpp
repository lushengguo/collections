#include <gtest/gtest.h>

#include "account_clearing_system.hpp"

TEST(AccountClearingSystemTest, ProjectNameIsStable)
{
    EXPECT_EQ(account_clearing_system::project_name(), "account_clearing_system");
}

TEST(AccountClearingSystemTest, ModuleSummaryReportsFlowsAndReuse)
{
    const auto summary = account_clearing_system::module_summary();
    EXPECT_EQ(summary.module_name, "account_clearing_system");
    EXPECT_EQ(summary.ledger_flows, 5U);
    EXPECT_EQ(summary.infrastructure_reuse_points, 2U);
}

TEST(AccountClearingSystemTest, FreezeAndReleaseQuoteAdjustBalances)
{
    account_clearing_system::AccountClearingSystem system;
    system.upsert_account("buyer", {.available_quote = 1000.0});

    EXPECT_TRUE(system.freeze_quote("buyer", "hold-1", 250.0, 1).ok());
    auto snapshot = system.account("buyer");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_DOUBLE_EQ(snapshot->available_quote, 750.0);
    EXPECT_DOUBLE_EQ(snapshot->frozen_quote, 250.0);

    EXPECT_TRUE(system.release_hold("hold-1", 2).ok());
    snapshot = system.account("buyer");
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_DOUBLE_EQ(snapshot->available_quote, 1000.0);
    EXPECT_DOUBLE_EQ(snapshot->frozen_quote, 0.0);
}

TEST(AccountClearingSystemTest, SpotSettlementMovesBalancesAndAppliesFees)
{
    account_clearing_system::AccountClearingSystem system;
    system.upsert_account("buyer", {.available_quote = 1200.0});
    system.upsert_account("seller", {.available_base = 1.0, .base_cost = 800.0});

    ASSERT_TRUE(system.freeze_quote("buyer", "hold-buy", 1005.0, 1).ok());
    ASSERT_TRUE(system.freeze_base("seller", "hold-sell", 1.0, 1).ok());

    const auto result = system.settle_spot_trade(
        {
            .trade_id = "trade-1",
            .symbol = "BTCUSDT",
            .buyer_account_id = "buyer",
            .seller_account_id = "seller",
            .buyer_hold_id = "hold-buy",
            .seller_hold_id = "hold-sell",
            .price = 1000.0,
            .quantity = 1.0,
            .buyer_is_taker = true,
            .timestamp_ms = 2,
        },
        {.maker_fee_bps = 2.0, .taker_fee_bps = 5.0});

    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(result.notional, 1000.0);
    EXPECT_DOUBLE_EQ(result.buyer_fee, 0.5);
    EXPECT_DOUBLE_EQ(result.seller_fee, 0.2);

    const auto buyer = system.account("buyer");
    const auto seller = system.account("seller");
    ASSERT_TRUE(buyer.has_value());
    ASSERT_TRUE(seller.has_value());
    EXPECT_DOUBLE_EQ(buyer->available_base, 1.0);
    EXPECT_DOUBLE_EQ(buyer->frozen_quote, 4.5);
    EXPECT_DOUBLE_EQ(buyer->base_cost, 1000.5);
    const auto buyer_hold = system.hold_amount("hold-buy");
    ASSERT_TRUE(buyer_hold.has_value());
    EXPECT_DOUBLE_EQ(*buyer_hold, 4.5);
    EXPECT_DOUBLE_EQ(seller->available_quote, 999.8);
    EXPECT_DOUBLE_EQ(seller->frozen_base, 0.0);
    EXPECT_DOUBLE_EQ(seller->realized_pnl, 199.8);
    EXPECT_FALSE(system.hold_amount("hold-sell").has_value());
}

TEST(AccountClearingSystemTest, DuplicateTradeSettlementIsIdempotent)
{
    account_clearing_system::AccountClearingSystem system;
    system.upsert_account("buyer", {.available_quote = 1200.0});
    system.upsert_account("seller", {.available_base = 1.0, .base_cost = 800.0});
    ASSERT_TRUE(system.freeze_quote("buyer", "hold-buy", 1005.0, 1).ok());
    ASSERT_TRUE(system.freeze_base("seller", "hold-sell", 1.0, 1).ok());

    const account_clearing_system::TradeFill fill{
        .trade_id = "trade-2",
        .symbol = "BTCUSDT",
        .buyer_account_id = "buyer",
        .seller_account_id = "seller",
        .buyer_hold_id = "hold-buy",
        .seller_hold_id = "hold-sell",
        .price = 1000.0,
        .quantity = 1.0,
        .buyer_is_taker = true,
        .timestamp_ms = 2,
    };

    const auto first = system.settle_spot_trade(fill, {.maker_fee_bps = 2.0, .taker_fee_bps = 5.0});
    const auto second = system.settle_spot_trade(fill, {.maker_fee_bps = 2.0, .taker_fee_bps = 5.0});

    EXPECT_TRUE(first.ok());
    EXPECT_TRUE(second.ok());
    EXPECT_TRUE(second.duplicate());
    EXPECT_EQ(system.pending_outbox(10).size(), 1U);
}

TEST(AccountClearingSystemTest, ReconcilePassesAfterValidOperations)
{
    account_clearing_system::AccountClearingSystem system;
    system.upsert_account("buyer", {.available_quote = 500.0});
    ASSERT_TRUE(system.freeze_quote("buyer", "hold-3", 100.0, 1).ok());
    ASSERT_TRUE(system.release_hold("hold-3", 2).ok());

    EXPECT_TRUE(system.reconcile_account("buyer"));
}

TEST(AccountClearingSystemTest, JournalAndOutboxExposeSettlementHistory)
{
    account_clearing_system::AccountClearingSystem system;
    system.upsert_account("buyer", {.available_quote = 1200.0});
    system.upsert_account("seller", {.available_base = 1.0, .base_cost = 800.0});
    ASSERT_TRUE(system.freeze_quote("buyer", "hold-buy", 1005.0, 1).ok());
    ASSERT_TRUE(system.freeze_base("seller", "hold-sell", 1.0, 1).ok());
    ASSERT_TRUE(system
                    .settle_spot_trade(
                        {
                            .trade_id = "trade-3",
                            .symbol = "BTCUSDT",
                            .buyer_account_id = "buyer",
                            .seller_account_id = "seller",
                            .buyer_hold_id = "hold-buy",
                            .seller_hold_id = "hold-sell",
                            .price = 1000.0,
                            .quantity = 1.0,
                            .buyer_is_taker = true,
                            .timestamp_ms = 2,
                        },
                        {.maker_fee_bps = 2.0, .taker_fee_bps = 5.0})
                    .ok());

    const auto buyer_journal = system.journal("buyer");
    const auto outbox = system.pending_outbox(10);
    ASSERT_EQ(buyer_journal.size(), 2U);
    ASSERT_EQ(outbox.size(), 1U);
    EXPECT_EQ(outbox.front().topic, "clearing.settlement");
    EXPECT_TRUE(system.mark_outbox_dispatched(outbox.front().id).ok());
    EXPECT_TRUE(system.pending_outbox(10).empty());
}

TEST(AccountClearingSystemTest, SettlementConsumesHoldMetadataForMatchedFill)
{
    account_clearing_system::AccountClearingSystem system;
    system.upsert_account("buyer", {.available_quote = 1200.0});
    system.upsert_account("seller", {.available_base = 1.0, .base_cost = 800.0});
    ASSERT_TRUE(system.freeze_quote("buyer", "hold-buy", 1005.0, 1).ok());
    ASSERT_TRUE(system.freeze_base("seller", "hold-sell", 1.0, 1).ok());

    const auto result = system.settle_spot_trade(
        {
            .trade_id = "trade-4",
            .symbol = "BTCUSDT",
            .buyer_account_id = "buyer",
            .seller_account_id = "seller",
            .buyer_hold_id = "hold-buy",
            .seller_hold_id = "hold-sell",
            .price = 1000.0,
            .quantity = 1.0,
            .buyer_is_taker = true,
            .timestamp_ms = 2,
        },
        {.maker_fee_bps = 2.0, .taker_fee_bps = 5.0});

    ASSERT_TRUE(result.ok());
    const auto buyer_hold = system.hold_amount("hold-buy");
    ASSERT_TRUE(buyer_hold.has_value());
    EXPECT_DOUBLE_EQ(*buyer_hold, 4.5);
    EXPECT_FALSE(system.hold_amount("hold-sell").has_value());
}

TEST(AccountClearingSystemTest, FreezeQuoteReturnsTypedFailureForDuplicateHold)
{
    account_clearing_system::AccountClearingSystem system;
    system.upsert_account("buyer", {.available_quote = 1000.0});

    ASSERT_TRUE(system.freeze_quote("buyer", "hold-dup", 50.0, 1).ok());
    const auto duplicate = system.freeze_quote("buyer", "hold-dup", 25.0, 2);

    EXPECT_EQ(duplicate.status, account_clearing_system::ClearingStatus::kHoldAlreadyExists);
    EXPECT_EQ(account_clearing_system::status_message(duplicate.status), "hold_already_exists");
}
