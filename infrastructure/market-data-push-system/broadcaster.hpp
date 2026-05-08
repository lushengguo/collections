#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "market_types.hpp"

namespace market_data_push_system
{

class MarketDataBroadcaster
{
  public:
    [[nodiscard]] std::uint64_t subscribe(std::string_view topic)
    {
        std::scoped_lock lock(mutex_);
        const std::uint64_t subscriber_id = ++next_subscriber_id_;
        subscribers_.emplace(subscriber_id, SubscriptionState{.topic = std::string(topic), .next_sequence = 1});
        return subscriber_id;
    }

    [[nodiscard]] BroadcastMessage publish(std::string topic, std::string payload)
    {
        std::scoped_lock lock(mutex_);
        BroadcastMessage message{
            .sequence = ++next_sequence_,
            .topic = std::move(topic),
            .payload = std::move(payload),
        };
        history_[message.topic].push_back(message);
        return message;
    }

    [[nodiscard]] std::vector<BroadcastMessage> poll(std::uint64_t subscriber_id, std::size_t max_items)
    {
        std::scoped_lock lock(mutex_);
        auto subscriber_iterator = subscribers_.find(subscriber_id);
        if (subscriber_iterator == subscribers_.end())
        {
            return {};
        }

        auto &subscriber = subscriber_iterator->second;
        auto history_iterator = history_.find(subscriber.topic);
        if (history_iterator == history_.end())
        {
            return {};
        }

        std::vector<BroadcastMessage> batch;
        batch.reserve(max_items);
        for (const auto &message : history_iterator->second)
        {
            if (message.sequence < subscriber.next_sequence)
            {
                continue;
            }

            batch.push_back(message);
            subscriber.next_sequence = message.sequence + 1;
            if (batch.size() == max_items)
            {
                break;
            }
        }

        return batch;
    }

    [[nodiscard]] std::vector<BroadcastMessage> replay(std::string_view topic, std::uint64_t sequence_after) const
    {
        std::scoped_lock lock(mutex_);
        std::vector<BroadcastMessage> messages;
        auto iterator = history_.find(std::string(topic));
        if (iterator == history_.end())
        {
            return messages;
        }

        for (const auto &message : iterator->second)
        {
            if (message.sequence > sequence_after)
            {
                messages.push_back(message);
            }
        }

        return messages;
    }

  private:
    struct SubscriptionState
    {
        std::string topic;
        std::uint64_t next_sequence = 1;
    };

    mutable std::mutex mutex_;
    std::uint64_t next_sequence_ = 0;
    std::uint64_t next_subscriber_id_ = 0;
    std::unordered_map<std::string, std::vector<BroadcastMessage>> history_;
    std::unordered_map<std::uint64_t, SubscriptionState> subscribers_;
};

} // namespace market_data_push_system
