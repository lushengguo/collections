#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace distributed_consistency
{

enum class OutboxState
{
    kPending,
    kDispatched,
};

struct OutboxMessage
{
    std::string id;
    std::string topic;
    std::string payload;
    OutboxState state = OutboxState::kPending;
};

class OutboxStore
{
  public:
    void append(OutboxMessage message)
    {
        std::scoped_lock lock(mutex_);
        messages_.push_back(std::move(message));
    }

    [[nodiscard]] std::vector<OutboxMessage> pending_batch(std::size_t max_items) const
    {
        std::scoped_lock lock(mutex_);
        std::vector<OutboxMessage> batch;
        batch.reserve(max_items);

        for (const auto &message : messages_)
        {
            if (message.state == OutboxState::kPending)
            {
                batch.push_back(message);
            }

            if (batch.size() == max_items)
            {
                break;
            }
        }

        return batch;
    }

    [[nodiscard]] bool mark_dispatched(const std::string &message_id)
    {
        std::scoped_lock lock(mutex_);
        for (auto &message : messages_)
        {
            if (message.id == message_id)
            {
                message.state = OutboxState::kDispatched;
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] std::size_t pending_count() const
    {
        std::scoped_lock lock(mutex_);
        std::size_t count = 0;

        for (const auto &message : messages_)
        {
            if (message.state == OutboxState::kPending)
            {
                ++count;
            }
        }

        return count;
    }

  private:
    mutable std::mutex mutex_;
    std::vector<OutboxMessage> messages_;
};

} // namespace distributed_consistency
