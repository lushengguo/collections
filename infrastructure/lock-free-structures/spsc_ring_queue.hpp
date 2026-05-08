#pragma once

#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "queue_facade.hpp"

namespace lock_free_structures
{

template <typename T> class SpscRingQueue : public QueueFacade<SpscRingQueue<T>, T>
{
  public:
    explicit SpscRingQueue(std::size_t requested_capacity)
        : capacity_(normalize_capacity(requested_capacity)), mask_(capacity_ - 1), buffer_(capacity_)
    {
    }

    [[nodiscard]] std::size_t capacity() const noexcept
    {
        return capacity_;
    }

    bool push_impl(const T &value)
    {
        return emplace(value);
    }

    bool push_impl(T &&value)
    {
        return emplace(std::move(value));
    }

    [[nodiscard]] std::optional<T> try_pop_impl()
    {
        const auto head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }

        auto &slot = buffer_[head & mask_];
        std::optional<T> result = std::move(slot);
        slot.reset();
        head_.store(head + 1, std::memory_order_release);
        return result;
    }

    [[nodiscard]] bool empty_impl() const
    {
        return size_approx_impl() == 0;
    }

    [[nodiscard]] std::size_t size_approx_impl() const
    {
        const auto head = head_.load(std::memory_order_acquire);
        const auto tail = tail_.load(std::memory_order_acquire);
        return tail - head;
    }

  private:
    template <typename Value> bool emplace(Value &&value)
    {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if ((tail - head_.load(std::memory_order_acquire)) == capacity_)
        {
            return false;
        }

        buffer_[tail & mask_] = std::forward<Value>(value);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    static std::size_t normalize_capacity(std::size_t requested_capacity)
    {
        if (requested_capacity < 2)
        {
            throw std::invalid_argument("SpscRingQueue capacity must be at least 2");
        }

        return std::bit_ceil(requested_capacity);
    }

    const std::size_t capacity_;
    const std::size_t mask_;
    std::vector<std::optional<T>> buffer_;
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace lock_free_structures
