#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "memory_pool/memory_pool.hpp"

TEST(MemoryPoolTest, ProjectNameIsStable)
{
    EXPECT_EQ(memory_pool::project_name(), "memory_pool");
}

TEST(MemoryPoolTest, ModuleSummaryReportsBuildingBlocks)
{
    const auto summary = memory_pool::module_summary();
    EXPECT_EQ(summary.module_name, "memory_pool");
    EXPECT_EQ(summary.building_blocks, 2U);
}

TEST(MemoryPoolTest, FixedBlockPoolReusesFreedMemory)
{
    memory_pool::FixedBlockPool pool(64, 8, 4);

    void *first = pool.allocate();
    pool.deallocate(first);
    void *second = pool.allocate();

    EXPECT_EQ(first, second);
    pool.deallocate(second);
    EXPECT_EQ(pool.stats().outstanding_blocks, 0U);
}

TEST(MemoryPoolTest, ObjectPoolConstructsAndDestroysObjects)
{
    struct Order
    {
        int id;
        double quantity;
    };

    memory_pool::ObjectPool<Order> pool;
    Order *order = pool.create(42, 1.5);

    ASSERT_NE(order, nullptr);
    EXPECT_EQ(order->id, 42);
    EXPECT_DOUBLE_EQ(order->quantity, 1.5);

    pool.destroy(order);
    EXPECT_EQ(pool.stats().outstanding_blocks, 0U);
}

TEST(MemoryPoolTest, FixedBlockPoolSupportsThreadedAllocateAndRelease)
{
    constexpr int kThreadCount = 4;
    constexpr int kAllocationsPerThread = 500;

    memory_pool::FixedBlockPool pool(128, 64, 16);
    std::vector<std::thread> workers;
    std::atomic<int> completed{0};

    for (int index = 0; index < kThreadCount; ++index)
    {
        workers.emplace_back([&] {
            std::vector<void *> blocks;
            blocks.reserve(kAllocationsPerThread);

            for (int allocation = 0; allocation < kAllocationsPerThread; ++allocation)
            {
                blocks.push_back(pool.allocate());
            }

            for (void *block : blocks)
            {
                pool.deallocate(block);
            }

            completed.fetch_add(1, std::memory_order_release);
        });
    }

    for (auto &worker : workers)
    {
        worker.join();
    }

    EXPECT_EQ(completed.load(std::memory_order_acquire), kThreadCount);
    const auto stats = pool.stats();
    EXPECT_EQ(stats.outstanding_blocks, 0U);
    EXPECT_GT(stats.total_blocks, 0U);
    EXPECT_GT(stats.slabs_allocated, 0U);
    EXPECT_GT(stats.global_cache_hits, 0U);
}
