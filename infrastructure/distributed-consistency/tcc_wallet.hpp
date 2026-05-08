#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace distributed_consistency
{

enum class ReservationState
{
    kReserved,
    kConfirmed,
    kCancelled,
};

struct ReservationRecord
{
    std::string reservation_id;
    double amount = 0.0;
    ReservationState state = ReservationState::kReserved;
};

class TccWallet
{
  public:
    explicit TccWallet(double balance) : available_(balance)
    {
    }

    [[nodiscard]] bool try_reserve(const std::string &reservation_id, double amount)
    {
        std::scoped_lock lock(mutex_);
        if (const auto iterator = reservations_.find(reservation_id); iterator != reservations_.end())
        {
            return iterator->second.state == ReservationState::kReserved && iterator->second.amount == amount;
        }

        if (amount <= 0.0 || amount > available_)
        {
            return false;
        }

        available_ -= amount;
        reserved_ += amount;
        reservations_.emplace(reservation_id, ReservationRecord{
                                                  .reservation_id = reservation_id,
                                                  .amount = amount,
                                                  .state = ReservationState::kReserved,
                                              });
        return true;
    }

    [[nodiscard]] bool confirm(const std::string &reservation_id)
    {
        std::scoped_lock lock(mutex_);
        auto iterator = reservations_.find(reservation_id);
        if (iterator == reservations_.end())
        {
            return false;
        }

        if (iterator->second.state == ReservationState::kConfirmed)
        {
            return true;
        }

        if (iterator->second.state != ReservationState::kReserved)
        {
            return false;
        }

        reserved_ -= iterator->second.amount;
        committed_ += iterator->second.amount;
        iterator->second.state = ReservationState::kConfirmed;
        return true;
    }

    [[nodiscard]] bool cancel(const std::string &reservation_id)
    {
        std::scoped_lock lock(mutex_);
        auto iterator = reservations_.find(reservation_id);
        if (iterator == reservations_.end())
        {
            return false;
        }

        if (iterator->second.state == ReservationState::kCancelled)
        {
            return true;
        }

        if (iterator->second.state != ReservationState::kReserved)
        {
            return false;
        }

        reserved_ -= iterator->second.amount;
        available_ += iterator->second.amount;
        iterator->second.state = ReservationState::kCancelled;
        return true;
    }

    [[nodiscard]] double available_balance() const
    {
        std::scoped_lock lock(mutex_);
        return available_;
    }

    [[nodiscard]] double reserved_balance() const
    {
        std::scoped_lock lock(mutex_);
        return reserved_;
    }

    [[nodiscard]] double committed_balance() const
    {
        std::scoped_lock lock(mutex_);
        return committed_;
    }

    [[nodiscard]] std::optional<ReservationRecord> lookup(const std::string &reservation_id) const
    {
        std::scoped_lock lock(mutex_);
        auto iterator = reservations_.find(reservation_id);
        if (iterator == reservations_.end())
        {
            return std::nullopt;
        }

        return iterator->second;
    }

  private:
    mutable std::mutex mutex_;
    double available_ = 0.0;
    double reserved_ = 0.0;
    double committed_ = 0.0;
    std::unordered_map<std::string, ReservationRecord> reservations_;
};

} // namespace distributed_consistency
