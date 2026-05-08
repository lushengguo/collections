#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lock_free_structures/backoff.hpp"
#include "lock_free_structures/queue_facade.hpp"

namespace lock_free_structures
{

template <typename T, std::size_t MaxThreads = 64>
class MpmcLinkedQueue : public QueueFacade<MpmcLinkedQueue<T, MaxThreads>, T>
{
  public:
    class Token
    {
      public:
        Token() = default;

        Token(const Token &) = delete;
        Token &operator=(const Token &) = delete;

        Token(Token &&other) noexcept
        {
            swap(other);
        }

        Token &operator=(Token &&other) noexcept
        {
            if (this != &other)
            {
                release();
                swap(other);
            }

            return *this;
        }

        ~Token()
        {
            release();
        }

      private:
        friend class MpmcLinkedQueue;

        Token(MpmcLinkedQueue *owner, std::size_t slot) noexcept : owner_(owner), slot_(slot)
        {
        }

        void release() noexcept
        {
            if (owner_ != nullptr)
            {
                owner_->unregister_slot(slot_);
                owner_ = nullptr;
            }
        }

        void swap(Token &other) noexcept
        {
            std::swap(owner_, other.owner_);
            std::swap(slot_, other.slot_);
        }

        MpmcLinkedQueue *owner_ = nullptr;
        std::size_t slot_ = 0;
    };

    MpmcLinkedQueue()
    {
        auto *stub = new Node();
        head_.store(stub, std::memory_order_relaxed);
        tail_.store(stub, std::memory_order_relaxed);
    }

    MpmcLinkedQueue(const MpmcLinkedQueue &) = delete;
    MpmcLinkedQueue &operator=(const MpmcLinkedQueue &) = delete;

    ~MpmcLinkedQueue()
    {
        drain_all_nodes();
        reclaim_all_retired();
    }

    [[nodiscard]] Token make_token()
    {
        return Token(this, acquire_slot());
    }

    bool push_impl(const T &value)
    {
        return push_with_token(value, local_token());
    }

    bool push_impl(T &&value)
    {
        return push_with_token(std::move(value), local_token());
    }

    [[nodiscard]] std::optional<T> try_pop_impl()
    {
        return try_pop_with_token(local_token());
    }

    template <typename Value> bool push_with_token(Value &&value, Token &token)
    {
        OperationGuard guard(*this, token.slot_);
        auto *node = new Node(std::forward<Value>(value));
        ExponentialBackoff backoff;

        while (true)
        {
            Node *tail = tail_.load(std::memory_order_acquire);
            Node *next = tail->next.load(std::memory_order_acquire);

            if (tail != tail_.load(std::memory_order_acquire))
            {
                backoff.pause();
                continue;
            }

            if (next == nullptr)
            {
                if (tail->next.compare_exchange_weak(next, node, std::memory_order_release, std::memory_order_relaxed))
                {
                    tail_.compare_exchange_strong(tail, node, std::memory_order_release, std::memory_order_relaxed);
                    size_.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
            }
            else
            {
                tail_.compare_exchange_weak(tail, next, std::memory_order_release, std::memory_order_relaxed);
            }

            backoff.pause();
        }
    }

    [[nodiscard]] std::optional<T> try_pop_with_token(Token &token)
    {
        OperationGuard guard(*this, token.slot_);
        ExponentialBackoff backoff;

        while (true)
        {
            Node *head = head_.load(std::memory_order_acquire);
            Node *tail = tail_.load(std::memory_order_acquire);
            Node *next = head->next.load(std::memory_order_acquire);

            if (head != head_.load(std::memory_order_acquire))
            {
                backoff.pause();
                continue;
            }

            if (next == nullptr)
            {
                return std::nullopt;
            }

            if (head == tail)
            {
                tail_.compare_exchange_weak(tail, next, std::memory_order_release, std::memory_order_relaxed);
                backoff.pause();
                continue;
            }

            if (head_.compare_exchange_weak(head, next, std::memory_order_release, std::memory_order_relaxed))
            {
                std::optional<T> result = std::move(next->value);
                next->value.reset();
                size_.fetch_sub(1, std::memory_order_relaxed);
                retire_node(head, token.slot_);
                return result;
            }

            backoff.pause();
        }
    }

    [[nodiscard]] bool empty_impl() const
    {
        return size_approx_impl() == 0;
    }

    [[nodiscard]] std::size_t size_approx_impl() const
    {
        return size_.load(std::memory_order_acquire);
    }

  private:
    struct Node
    {
        std::atomic<Node *> next{nullptr};
        std::optional<T> value;

        Node() = default;

        template <typename Value> explicit Node(Value &&input_value) : value(std::forward<Value>(input_value))
        {
        }
    };

    struct RetiredNode
    {
        Node *node = nullptr;
        std::uint64_t retire_epoch = 0;
    };

    struct alignas(64) EpochSlot
    {
        std::atomic<bool> occupied{false};
        std::atomic<bool> active{false};
        std::atomic<std::uint64_t> epoch{0};
        std::vector<RetiredNode> retired;
    };

    class OperationGuard
    {
      public:
        OperationGuard(MpmcLinkedQueue &queue, std::size_t slot) noexcept : queue_(queue), slot_(slot)
        {
            queue_.enter_critical(slot_);
        }

        OperationGuard(const OperationGuard &) = delete;
        OperationGuard &operator=(const OperationGuard &) = delete;

        ~OperationGuard()
        {
            queue_.leave_critical(slot_);
        }

      private:
        MpmcLinkedQueue &queue_;
        std::size_t slot_;
    };

    static Token &local_token_for(MpmcLinkedQueue &queue)
    {
        thread_local std::unordered_map<MpmcLinkedQueue *, Token> tokens;
        auto [iterator, inserted] = tokens.try_emplace(&queue);

        if (inserted || iterator->second.owner_ == nullptr)
        {
            iterator->second = queue.make_token();
        }

        return iterator->second;
    }

    Token &local_token()
    {
        return local_token_for(*this);
    }

    [[nodiscard]] std::size_t acquire_slot()
    {
        for (std::size_t index = 0; index < slots_.size(); ++index)
        {
            bool expected = false;
            if (slots_[index].occupied.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                               std::memory_order_relaxed))
            {
                slots_[index].retired.clear();
                slots_[index].epoch.store(global_epoch_.load(std::memory_order_acquire), std::memory_order_release);
                return index;
            }
        }

        throw std::runtime_error("MpmcLinkedQueue thread registration limit exceeded");
    }

    void unregister_slot(std::size_t slot) noexcept
    {
        collect_retired(slot, true);
        slots_[slot].active.store(false, std::memory_order_release);
        slots_[slot].occupied.store(false, std::memory_order_release);
    }

    void enter_critical(std::size_t slot) noexcept
    {
        slots_[slot].epoch.store(global_epoch_.load(std::memory_order_acquire), std::memory_order_release);
        slots_[slot].active.store(true, std::memory_order_release);
    }

    void leave_critical(std::size_t slot) noexcept
    {
        slots_[slot].active.store(false, std::memory_order_release);
    }

    void retire_node(Node *node, std::size_t slot)
    {
        auto &retired = slots_[slot].retired;
        retired.push_back(RetiredNode{node, global_epoch_.load(std::memory_order_acquire)});

        if (retired.size() >= kRetireThreshold)
        {
            global_epoch_.fetch_add(1, std::memory_order_acq_rel);
            collect_retired(slot, false);
        }
    }

    void collect_retired(std::size_t slot, bool force_all)
    {
        auto &retired = slots_[slot].retired;
        if (retired.empty())
        {
            return;
        }

        const auto safe_epoch = force_all ? (std::numeric_limits<std::uint64_t>::max)() : min_active_epoch();

        auto output = retired.begin();
        for (auto iterator = retired.begin(); iterator != retired.end(); ++iterator)
        {
            if (force_all || iterator->retire_epoch < safe_epoch)
            {
                delete iterator->node;
            }
            else
            {
                *output++ = *iterator;
            }
        }

        retired.erase(output, retired.end());
    }

    [[nodiscard]] std::uint64_t min_active_epoch() const noexcept
    {
        std::uint64_t minimum = global_epoch_.load(std::memory_order_acquire) + 1;

        for (const auto &slot : slots_)
        {
            if (slot.occupied.load(std::memory_order_acquire) && slot.active.load(std::memory_order_acquire))
            {
                minimum = std::min(minimum, slot.epoch.load(std::memory_order_acquire));
            }
        }

        return minimum;
    }

    void drain_all_nodes() noexcept
    {
        Node *current = head_.load(std::memory_order_relaxed);
        while (current != nullptr)
        {
            Node *next = current->next.load(std::memory_order_relaxed);
            delete current;
            current = next;
        }

        head_.store(nullptr, std::memory_order_relaxed);
        tail_.store(nullptr, std::memory_order_relaxed);
    }

    void reclaim_all_retired() noexcept
    {
        for (auto &slot : slots_)
        {
            for (const auto &retired_node : slot.retired)
            {
                delete retired_node.node;
            }

            slot.retired.clear();
        }
    }

    static constexpr std::size_t kRetireThreshold = 32;

    alignas(64) std::atomic<Node *> head_{nullptr};
    alignas(64) std::atomic<Node *> tail_{nullptr};
    alignas(64) std::atomic<std::size_t> size_{0};
    alignas(64) std::atomic<std::uint64_t> global_epoch_{1};
    std::array<EpochSlot, MaxThreads> slots_{};
};

} // namespace lock_free_structures
