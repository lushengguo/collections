#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "object_pool.hpp"
#include "outbox.hpp"

namespace account_clearing_system
{

enum class HoldAsset
{
    kQuote,
    kBase,
};

struct AccountSnapshot
{
    // Quote asset immediately available for new buy-side reservations or withdrawals.
    double available_quote{0.0};
    // Quote asset currently frozen by open orders or in-flight settlement.
    double frozen_quote{0.0};
    // Base asset immediately available for new sell-side reservations or withdrawals.
    double available_base{0.0};
    // Base asset currently frozen by open orders or in-flight settlement.
    double frozen_base{0.0};
    // Book cost of the remaining base position, used to derive realized PnL when selling.
    double base_cost{0.0};
    // Accumulated realized profit and loss after completed settlements.
    double realized_pnl{0.0};
};

struct FeeSchedule
{
    // Maker fee in basis points, applied to the passive side of a match.
    double maker_fee_bps{0.0};
    // Taker fee in basis points, applied to the aggressive side of a match.
    double taker_fee_bps{0.0};
};

enum class ClearingStatus
{
    kApplied,
    kDuplicate,
    kInvalidInput,
    kAccountNotFound,
    kHoldAlreadyExists,
    kHoldNotFound,
    kInsufficientQuoteBalance,
    kInsufficientBasePosition,
    kBuyerFrozenQuoteInsufficient,
    kSellerFrozenBaseInsufficient,
    kBuyerHoldInsufficient,
    kSellerHoldInsufficient,
    kOutboxMessageNotFound,
    kInvariantViolation,
};

struct ClearingOperationResult
{
    // Result of a freeze/release/outbox operation.
    ClearingStatus status{ClearingStatus::kApplied};

    [[nodiscard]] bool ok() const noexcept
    {
        return status == ClearingStatus::kApplied;
    }
};

struct TradeFill
{
    // Stable id for the fill used for idempotent settlement.
    std::string trade_id;
    // Trading pair this fill belongs to, for example BTCUSDT.
    std::string symbol;
    // Buyer account that receives base and spends quote.
    std::string buyer_account_id;
    // Seller account that receives quote and delivers base.
    std::string seller_account_id;
    // Quote hold consumed on the buyer side of the fill.
    std::string buyer_hold_id;
    // Base hold consumed on the seller side of the fill.
    std::string seller_hold_id;
    // Execution price of this matched trade.
    double price{0.0};
    // Executed base quantity.
    double quantity{0.0};
    // Whether the buyer was the aggressive side, which decides maker/taker fees.
    bool buyer_is_taker{true};
    // Event time used for journal and outbox records.
    std::int64_t timestamp_ms{0};
};

struct SettlementResult
{
    // Final settlement status for this fill.
    ClearingStatus status{ClearingStatus::kApplied};
    // Gross quote notional equal to price * quantity.
    double notional{0.0};
    // Fee charged to the buyer leg.
    double buyer_fee{0.0};
    // Fee charged to the seller leg.
    double seller_fee{0.0};
    // Realized PnL crystallized on the seller side.
    double seller_realized_pnl{0.0};

    [[nodiscard]] bool ok() const noexcept
    {
        return status == ClearingStatus::kApplied || status == ClearingStatus::kDuplicate;
    }

    [[nodiscard]] bool duplicate() const noexcept
    {
        return status == ClearingStatus::kDuplicate;
    }
};

struct JournalEntry
{
    // Account whose balances were mutated by this ledger event.
    std::string account_id;
    // Business event id, usually a hold id or trade id.
    std::string event_id;
    // Human-readable operation name such as freeze_quote or spot_trade_sell.
    std::string operation;
    // Delta applied to spendable quote balance.
    double delta_available_quote{0.0};
    // Delta applied to frozen quote balance.
    double delta_frozen_quote{0.0};
    // Delta applied to spendable base balance.
    double delta_available_base{0.0};
    // Delta applied to frozen base balance.
    double delta_frozen_base{0.0};
    // Delta applied to remaining base cost basis.
    double delta_base_cost{0.0};
    // Delta applied to realized PnL.
    double delta_realized_pnl{0.0};
    // Ledger event timestamp.
    std::int64_t timestamp_ms{0};
};

struct ModuleSummary
{
    std::string module_name;
    std::size_t ledger_flows{0};
    std::size_t infrastructure_reuse_points{0};
};

class AccountClearingSystem
{
  public:
    AccountClearingSystem();
    ~AccountClearingSystem();

    AccountClearingSystem(const AccountClearingSystem &) = delete;
    AccountClearingSystem &operator=(const AccountClearingSystem &) = delete;

    void upsert_account(std::string account_id, AccountSnapshot snapshot);
    [[nodiscard]] ClearingOperationResult freeze_quote(std::string_view account_id, std::string hold_id, double amount,
                                                       std::int64_t timestamp_ms);
    [[nodiscard]] ClearingOperationResult freeze_base(std::string_view account_id, std::string hold_id, double amount,
                                                      std::int64_t timestamp_ms);
    [[nodiscard]] ClearingOperationResult release_hold(std::string_view hold_id, std::int64_t timestamp_ms);
    [[nodiscard]] SettlementResult settle_spot_trade(const TradeFill &fill, const FeeSchedule &fee_schedule);
    [[nodiscard]] std::optional<AccountSnapshot> account(std::string_view account_id) const;
    [[nodiscard]] std::optional<double> hold_amount(std::string_view hold_id) const;
    [[nodiscard]] std::vector<JournalEntry> journal(std::string_view account_id) const;
    [[nodiscard]] bool reconcile_account(std::string_view account_id) const;
    [[nodiscard]] std::vector<distributed_consistency::OutboxMessage> pending_outbox(std::size_t max_items) const;
    [[nodiscard]] ClearingOperationResult mark_outbox_dispatched(std::string_view message_id);

  private:
    struct HoldRecord
    {
        // Account that owns this hold.
        std::string account_id;
        // Which asset was reserved: quote for buys, base for sells.
        HoldAsset asset{HoldAsset::kQuote};
        // Remaining reserved amount that can still be consumed or released.
        double amount{0.0};
    };

    void append_journal(std::string_view account_id, std::string_view event_id, std::string_view operation,
                        double delta_available_quote, double delta_frozen_quote, double delta_available_base,
                        double delta_frozen_base, double delta_base_cost, double delta_realized_pnl,
                        std::int64_t timestamp_ms);

    std::unordered_map<std::string, AccountSnapshot> accounts_;
    std::unordered_map<std::string, HoldRecord> holds_;
    std::unordered_map<std::string, std::vector<JournalEntry *>> journals_;
    std::unordered_set<std::string> settled_trade_ids_;
    std::vector<JournalEntry *> journal_storage_;
    distributed_consistency::OutboxStore outbox_;
    memory_pool::ObjectPool<JournalEntry> journal_pool_;
};

[[nodiscard]] ModuleSummary module_summary();

[[nodiscard]] std::string project_name();

[[nodiscard]] std::string_view status_message(ClearingStatus status) noexcept;

} // namespace account_clearing_system
