#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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
    double available_quote{0.0};
    double frozen_quote{0.0};
    double available_base{0.0};
    double frozen_base{0.0};
    double base_cost{0.0};
    double realized_pnl{0.0};
};

struct FeeSchedule
{
    double maker_fee_bps{0.0};
    double taker_fee_bps{0.0};
};

struct TradeFill
{
    std::string trade_id;
    std::string symbol;
    std::string buyer_account_id;
    std::string seller_account_id;
    std::string buyer_hold_id;
    std::string seller_hold_id;
    double price{0.0};
    double quantity{0.0};
    bool buyer_is_taker{true};
    std::int64_t timestamp_ms{0};
};

struct SettlementResult
{
    bool success{false};
    bool duplicate{false};
    std::string failure_reason;
    double notional{0.0};
    double buyer_fee{0.0};
    double seller_fee{0.0};
    double seller_realized_pnl{0.0};
};

struct JournalEntry
{
    std::string account_id;
    std::string event_id;
    std::string operation;
    double delta_available_quote{0.0};
    double delta_frozen_quote{0.0};
    double delta_available_base{0.0};
    double delta_frozen_base{0.0};
    double delta_base_cost{0.0};
    double delta_realized_pnl{0.0};
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
    [[nodiscard]] bool freeze_quote(std::string_view account_id, std::string hold_id, double amount,
                                    std::int64_t timestamp_ms);
    [[nodiscard]] bool freeze_base(std::string_view account_id, std::string hold_id, double amount,
                                   std::int64_t timestamp_ms);
    [[nodiscard]] bool release_hold(std::string_view hold_id, std::int64_t timestamp_ms);
    [[nodiscard]] SettlementResult settle_spot_trade(const TradeFill &fill, const FeeSchedule &fee_schedule);
    [[nodiscard]] std::optional<AccountSnapshot> account(std::string_view account_id) const;
    [[nodiscard]] std::optional<double> hold_amount(std::string_view hold_id) const;
    [[nodiscard]] std::vector<JournalEntry> journal(std::string_view account_id) const;
    [[nodiscard]] bool reconcile_account(std::string_view account_id) const;
    [[nodiscard]] std::vector<distributed_consistency::OutboxMessage> pending_outbox(std::size_t max_items) const;
    [[nodiscard]] bool mark_outbox_dispatched(std::string_view message_id);

  private:
    struct HoldRecord
    {
        std::string account_id;
        HoldAsset asset{HoldAsset::kQuote};
        double amount{0.0};
    };

    void append_journal(std::string_view account_id, std::string_view event_id, std::string_view operation,
                        double delta_available_quote, double delta_frozen_quote, double delta_available_base,
                        double delta_frozen_base, double delta_base_cost, double delta_realized_pnl,
                        std::int64_t timestamp_ms);

    std::unordered_map<std::string, AccountSnapshot> accounts_;
    std::unordered_map<std::string, HoldRecord> holds_;
    std::unordered_map<std::string, std::vector<JournalEntry *>> journals_;
    std::unordered_map<std::string, bool> settled_trade_ids_;
    std::vector<JournalEntry *> journal_storage_;
    distributed_consistency::OutboxStore outbox_;
    memory_pool::ObjectPool<JournalEntry> journal_pool_;
};

[[nodiscard]] ModuleSummary module_summary();

[[nodiscard]] std::string project_name();

} // namespace account_clearing_system
