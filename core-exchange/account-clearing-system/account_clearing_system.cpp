#include "account_clearing_system.hpp"

#include <stdexcept>
#include <utility>

namespace account_clearing_system
{

namespace
{

constexpr double kBalanceEpsilon = 1e-9;

double fee_amount(double notional, double fee_bps)
{
    return notional * fee_bps / 10000.0;
}

bool is_zero(double value)
{
    return value >= -kBalanceEpsilon && value <= kBalanceEpsilon;
}

} // namespace

AccountClearingSystem::AccountClearingSystem() : journal_pool_(512, 128)
{
}

AccountClearingSystem::~AccountClearingSystem()
{
    for (auto *entry : journal_storage_)
    {
        journal_pool_.destroy(entry);
    }
}

void AccountClearingSystem::upsert_account(std::string account_id, AccountSnapshot snapshot)
{
    accounts_.insert_or_assign(std::move(account_id), snapshot);
}

ClearingOperationResult AccountClearingSystem::freeze_quote(std::string_view account_id, std::string hold_id,
                                                            double amount, std::int64_t timestamp_ms)
{
    auto account_it = accounts_.find(std::string(account_id));
    if (account_it == accounts_.end())
    {
        return {.status = ClearingStatus::kAccountNotFound};
    }
    if (amount <= 0.0)
    {
        return {.status = ClearingStatus::kInvalidInput};
    }
    if (holds_.contains(hold_id))
    {
        return {.status = ClearingStatus::kHoldAlreadyExists};
    }

    auto &snapshot = account_it->second;
    if (snapshot.available_quote < amount)
    {
        return {.status = ClearingStatus::kInsufficientQuoteBalance};
    }

    snapshot.available_quote -= amount;
    snapshot.frozen_quote += amount;
    holds_[hold_id] = HoldRecord{.account_id = std::string(account_id), .asset = HoldAsset::kQuote, .amount = amount};
    append_journal(account_id, hold_id, "freeze_quote", -amount, amount, 0.0, 0.0, 0.0, 0.0, timestamp_ms);
    return {};
}

ClearingOperationResult AccountClearingSystem::freeze_base(std::string_view account_id, std::string hold_id,
                                                           double amount, std::int64_t timestamp_ms)
{
    auto account_it = accounts_.find(std::string(account_id));
    if (account_it == accounts_.end())
    {
        return {.status = ClearingStatus::kAccountNotFound};
    }
    if (amount <= 0.0)
    {
        return {.status = ClearingStatus::kInvalidInput};
    }
    if (holds_.contains(hold_id))
    {
        return {.status = ClearingStatus::kHoldAlreadyExists};
    }

    auto &snapshot = account_it->second;
    if (snapshot.available_base < amount)
    {
        return {.status = ClearingStatus::kInsufficientBasePosition};
    }

    snapshot.available_base -= amount;
    snapshot.frozen_base += amount;
    holds_[hold_id] = HoldRecord{.account_id = std::string(account_id), .asset = HoldAsset::kBase, .amount = amount};
    append_journal(account_id, hold_id, "freeze_base", 0.0, 0.0, -amount, amount, 0.0, 0.0, timestamp_ms);
    return {};
}

ClearingOperationResult AccountClearingSystem::release_hold(std::string_view hold_id, std::int64_t timestamp_ms)
{
    auto hold_it = holds_.find(std::string(hold_id));
    if (hold_it == holds_.end())
    {
        return {.status = ClearingStatus::kHoldNotFound};
    }

    const auto hold = hold_it->second;
    auto account_it = accounts_.find(hold.account_id);
    if (account_it == accounts_.end())
    {
        return {.status = ClearingStatus::kInvariantViolation};
    }

    auto &snapshot = account_it->second;
    if (hold.asset == HoldAsset::kQuote)
    {
        snapshot.frozen_quote -= hold.amount;
        snapshot.available_quote += hold.amount;
        append_journal(hold.account_id, hold_id, "release_quote", hold.amount, -hold.amount, 0.0, 0.0, 0.0, 0.0,
                       timestamp_ms);
    }
    else
    {
        snapshot.frozen_base -= hold.amount;
        snapshot.available_base += hold.amount;
        append_journal(hold.account_id, hold_id, "release_base", 0.0, 0.0, hold.amount, -hold.amount, 0.0, 0.0,
                       timestamp_ms);
    }

    holds_.erase(hold_it);
    return {};
}

SettlementResult AccountClearingSystem::settle_spot_trade(const TradeFill &fill, const FeeSchedule &fee_schedule)
{
    if (settled_trade_ids_.contains(fill.trade_id))
    {
        return SettlementResult{.status = ClearingStatus::kDuplicate};
    }

    auto buyer_it = accounts_.find(fill.buyer_account_id);
    auto seller_it = accounts_.find(fill.seller_account_id);
    if (buyer_it == accounts_.end() || seller_it == accounts_.end() || fill.price <= 0.0 || fill.quantity <= 0.0 ||
        fill.timestamp_ms <= 0)
    {
        return SettlementResult{.status = buyer_it == accounts_.end() || seller_it == accounts_.end()
                                              ? ClearingStatus::kAccountNotFound
                                              : ClearingStatus::kInvalidInput};
    }

    const auto notional = fill.price * fill.quantity;
    const auto buyer_fee =
        fee_amount(notional, fill.buyer_is_taker ? fee_schedule.taker_fee_bps : fee_schedule.maker_fee_bps);
    const auto seller_fee =
        fee_amount(notional, fill.buyer_is_taker ? fee_schedule.maker_fee_bps : fee_schedule.taker_fee_bps);
    const auto buyer_consumed_quote = notional + buyer_fee;
    const auto seller_consumed_base = fill.quantity;

    auto &buyer = buyer_it->second;
    auto &seller = seller_it->second;
    if (buyer.frozen_quote < buyer_consumed_quote)
    {
        return SettlementResult{.status = ClearingStatus::kBuyerFrozenQuoteInsufficient};
    }
    if (seller.frozen_base < seller_consumed_base)
    {
        return SettlementResult{.status = ClearingStatus::kSellerFrozenBaseInsufficient};
    }

    if (!fill.buyer_hold_id.empty())
    {
        auto buyer_hold_it = holds_.find(fill.buyer_hold_id);
        if (buyer_hold_it == holds_.end() || buyer_hold_it->second.account_id != fill.buyer_account_id ||
            buyer_hold_it->second.asset != HoldAsset::kQuote || buyer_hold_it->second.amount < buyer_consumed_quote)
        {
            return SettlementResult{.status = ClearingStatus::kBuyerHoldInsufficient};
        }
    }
    if (!fill.seller_hold_id.empty())
    {
        auto seller_hold_it = holds_.find(fill.seller_hold_id);
        if (seller_hold_it == holds_.end() || seller_hold_it->second.account_id != fill.seller_account_id ||
            seller_hold_it->second.asset != HoldAsset::kBase || seller_hold_it->second.amount < seller_consumed_base)
        {
            return SettlementResult{.status = ClearingStatus::kSellerHoldInsufficient};
        }
    }

    const auto seller_total_base_before = seller.available_base + seller.frozen_base;
    const auto seller_cost_per_unit =
        seller_total_base_before > 0.0 ? seller.base_cost / seller_total_base_before : 0.0;
    const auto seller_cost_released = seller_cost_per_unit * fill.quantity;
    const auto seller_realized_pnl = notional - seller_fee - seller_cost_released;

    buyer.frozen_quote -= buyer_consumed_quote;
    buyer.available_base += fill.quantity;
    buyer.base_cost += buyer_consumed_quote;

    seller.frozen_base -= seller_consumed_base;
    seller.available_quote += notional - seller_fee;
    seller.base_cost -= seller_cost_released;
    seller.realized_pnl += seller_realized_pnl;

    if (!fill.buyer_hold_id.empty())
    {
        auto buyer_hold_it = holds_.find(fill.buyer_hold_id);
        buyer_hold_it->second.amount -= buyer_consumed_quote;
        if (is_zero(buyer_hold_it->second.amount))
        {
            holds_.erase(buyer_hold_it);
        }
    }
    if (!fill.seller_hold_id.empty())
    {
        auto seller_hold_it = holds_.find(fill.seller_hold_id);
        seller_hold_it->second.amount -= seller_consumed_base;
        if (is_zero(seller_hold_it->second.amount))
        {
            holds_.erase(seller_hold_it);
        }
    }

    append_journal(fill.buyer_account_id, fill.trade_id, "spot_trade_buy", 0.0, -(notional + buyer_fee), fill.quantity,
                   0.0, notional + buyer_fee, 0.0, fill.timestamp_ms);
    append_journal(fill.seller_account_id, fill.trade_id, "spot_trade_sell", notional - seller_fee, 0.0, 0.0,
                   -fill.quantity, -seller_cost_released, seller_realized_pnl, fill.timestamp_ms);

    outbox_.append({
        .id = fill.trade_id,
        .topic = "clearing.settlement",
        .payload = fill.symbol + ":" + fill.buyer_account_id + ":" + fill.seller_account_id,
        .state = distributed_consistency::OutboxState::kPending,
    });
    settled_trade_ids_.insert(fill.trade_id);

    return SettlementResult{
        .status = ClearingStatus::kApplied,
        .notional = notional,
        .buyer_fee = buyer_fee,
        .seller_fee = seller_fee,
        .seller_realized_pnl = seller_realized_pnl,
    };
}

std::optional<AccountSnapshot> AccountClearingSystem::account(std::string_view account_id) const
{
    const auto it = accounts_.find(std::string(account_id));
    if (it == accounts_.end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::optional<double> AccountClearingSystem::hold_amount(std::string_view hold_id) const
{
    const auto it = holds_.find(std::string(hold_id));
    if (it == holds_.end())
    {
        return std::nullopt;
    }
    return it->second.amount;
}

std::vector<JournalEntry> AccountClearingSystem::journal(std::string_view account_id) const
{
    const auto it = journals_.find(std::string(account_id));
    if (it == journals_.end())
    {
        return {};
    }

    std::vector<JournalEntry> entries;
    entries.reserve(it->second.size());
    for (const auto *entry : it->second)
    {
        entries.push_back(*entry);
    }
    return entries;
}

bool AccountClearingSystem::reconcile_account(std::string_view account_id) const
{
    const auto snapshot = account(account_id);
    if (!snapshot.has_value())
    {
        return false;
    }

    return snapshot->available_quote >= 0.0 && snapshot->frozen_quote >= 0.0 && snapshot->available_base >= 0.0 &&
           snapshot->frozen_base >= 0.0 && snapshot->base_cost >= 0.0;
}

std::vector<distributed_consistency::OutboxMessage> AccountClearingSystem::pending_outbox(std::size_t max_items) const
{
    return outbox_.pending_batch(max_items);
}

ClearingOperationResult AccountClearingSystem::mark_outbox_dispatched(std::string_view message_id)
{
    if (!outbox_.mark_dispatched(std::string(message_id)))
    {
        return {.status = ClearingStatus::kOutboxMessageNotFound};
    }
    return {};
}

void AccountClearingSystem::append_journal(std::string_view account_id, std::string_view event_id,
                                           std::string_view operation, double delta_available_quote,
                                           double delta_frozen_quote, double delta_available_base,
                                           double delta_frozen_base, double delta_base_cost, double delta_realized_pnl,
                                           std::int64_t timestamp_ms)
{
    auto *entry = journal_pool_.create(JournalEntry{
        .account_id = std::string(account_id),
        .event_id = std::string(event_id),
        .operation = std::string(operation),
        .delta_available_quote = delta_available_quote,
        .delta_frozen_quote = delta_frozen_quote,
        .delta_available_base = delta_available_base,
        .delta_frozen_base = delta_frozen_base,
        .delta_base_cost = delta_base_cost,
        .delta_realized_pnl = delta_realized_pnl,
        .timestamp_ms = timestamp_ms,
    });
    journal_storage_.push_back(entry);
    journals_[std::string(account_id)].push_back(entry);
}

ModuleSummary module_summary()
{
    return {
        .module_name = project_name(),
        .ledger_flows = 5,
        .infrastructure_reuse_points = 2,
    };
}

std::string project_name()
{
    return "account_clearing_system";
}

std::string_view status_message(ClearingStatus status) noexcept
{
    switch (status)
    {
    case ClearingStatus::kApplied:
        return "applied";
    case ClearingStatus::kDuplicate:
        return "duplicate";
    case ClearingStatus::kInvalidInput:
        return "invalid_input";
    case ClearingStatus::kAccountNotFound:
        return "account_not_found";
    case ClearingStatus::kHoldAlreadyExists:
        return "hold_already_exists";
    case ClearingStatus::kHoldNotFound:
        return "hold_not_found";
    case ClearingStatus::kInsufficientQuoteBalance:
        return "insufficient_quote_balance";
    case ClearingStatus::kInsufficientBasePosition:
        return "insufficient_base_position";
    case ClearingStatus::kBuyerFrozenQuoteInsufficient:
        return "buyer_frozen_quote_insufficient";
    case ClearingStatus::kSellerFrozenBaseInsufficient:
        return "seller_frozen_base_insufficient";
    case ClearingStatus::kBuyerHoldInsufficient:
        return "buyer_hold_insufficient";
    case ClearingStatus::kSellerHoldInsufficient:
        return "seller_hold_insufficient";
    case ClearingStatus::kOutboxMessageNotFound:
        return "outbox_message_not_found";
    case ClearingStatus::kInvariantViolation:
        return "invariant_violation";
    }

    return "unknown";
}

} // namespace account_clearing_system
