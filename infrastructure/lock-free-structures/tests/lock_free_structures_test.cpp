#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

#include "lock_free_structures/lock_free_structures.hpp"

TEST(LockFreeStructuresTest, ProjectNameIsStable)
{
    EXPECT_EQ(lock_free_structures::project_name(), "lock_free_structures");
}

TEST(LockFreeStructuresTest, ModuleSummaryReportsQueueVariants)
{
    const auto summary = lock_free_structures::module_summary();
    EXPECT_EQ(summary.module_name, "lock_free_structures");
    EXPECT_EQ(summary.queue_variants, 2U);
}

TEST(LockFreeStructuresTest, SpscRingQueuePreservesOrder)
{
    lock_free_structures::SpscRingQueue<int> queue(4);

    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));
    EXPECT_TRUE(queue.push(4));
    EXPECT_FALSE(queue.push(5));

    EXPECT_EQ(queue.try_pop(), 1);
    EXPECT_EQ(queue.try_pop(), 2);
    EXPECT_EQ(queue.try_pop(), 3);
    EXPECT_EQ(queue.try_pop(), 4);
    EXPECT_FALSE(queue.try_pop().has_value());
}

TEST(LockFreeStructuresTest, SpscRingQueueSupportsThreadedHandOff)
{
    constexpr int kItemCount = 20000;
    lock_free_structures::SpscRingQueue<int> queue(1024);
    std::atomic<int> produced_sum{0};
    std::atomic<int> consumed_sum{0};

    std::thread producer([&] {
        for (int value = 1; value <= kItemCount; ++value)
        {
            while (!queue.push(value))
            {
                std::this_thread::yield();
            }

            produced_sum.fetch_add(value, std::memory_order_relaxed);
        }
    });

    std::thread consumer([&] {
        int consumed = 0;
        while (consumed < kItemCount)
        {
            auto value = queue.try_pop();
            if (!value.has_value())
            {
                std::this_thread::yield();
                continue;
            }

            consumed_sum.fetch_add(*value, std::memory_order_relaxed);
            ++consumed;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(consumed_sum.load(std::memory_order_relaxed), produced_sum.load(std::memory_order_relaxed));
    EXPECT_TRUE(queue.empty());
}

TEST(LockFreeStructuresTest, MpmcLinkedQueueSupportsMultiProducerMultiConsumer)
{
    constexpr int kProducerCount = 4;
    constexpr int kConsumerCount = 4;
    constexpr int kItemsPerProducer = 500;
    constexpr int kTotalItems = kProducerCount * kItemsPerProducer;

    lock_free_structures::MpmcLinkedQueue<int> queue;
    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;
    std::vector<std::atomic<int>> hit_counts(static_cast<std::size_t>(kTotalItems));
    for (auto &counter : hit_counts)
    {
        counter.store(0, std::memory_order_relaxed);
    }

    std::atomic<int> consumed{0};

    for (int producer = 0; producer < kProducerCount; ++producer)
    {
        producers.emplace_back([&, producer] {
            auto token = queue.make_token();
            const int base = producer * kItemsPerProducer;

            for (int offset = 0; offset < kItemsPerProducer; ++offset)
            {
                const int value = base + offset;
                EXPECT_TRUE(queue.push_with_token(value, token));
            }
        });
    }

    for (int consumer_index = 0; consumer_index < kConsumerCount; ++consumer_index)
    {
        consumers.emplace_back([&] {
            auto token = queue.make_token();

            while (consumed.load(std::memory_order_acquire) < kTotalItems)
            {
                auto value = queue.try_pop_with_token(token);
                if (!value.has_value())
                {
                    std::this_thread::yield();
                    continue;
                }

                hit_counts[static_cast<std::size_t>(*value)].fetch_add(1, std::memory_order_relaxed);
                consumed.fetch_add(1, std::memory_order_release);
            }
        });
    }

    for (auto &producer : producers)
    {
        producer.join();
    }

    for (auto &consumer : consumers)
    {
        consumer.join();
    }

    EXPECT_EQ(consumed.load(std::memory_order_relaxed), kTotalItems);
    EXPECT_TRUE(queue.empty());

    for (const auto &counter : hit_counts)
    {
        EXPECT_EQ(counter.load(std::memory_order_relaxed), 1);
    }
}

TEST(LockFreeStructuresTest, QueueFacadeDrainCollectsAvailableValues)
{
    lock_free_structures::SpscRingQueue<int> queue(8);
    std::vector<int> drained;

    EXPECT_TRUE(queue.push(10));
    EXPECT_TRUE(queue.push(20));
    EXPECT_TRUE(queue.push(30));

    const auto drained_count = queue.drain(std::back_inserter(drained), 2);

    EXPECT_EQ(drained_count, 2U);
    EXPECT_EQ(drained.size(), 2U);
    EXPECT_EQ(drained[0], 10);
    EXPECT_EQ(drained[1], 20);
    EXPECT_EQ(queue.try_pop(), 30);
}
