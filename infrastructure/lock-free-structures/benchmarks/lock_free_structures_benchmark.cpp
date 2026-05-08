#include <benchmark/benchmark.h>

#include <atomic>
#include <thread>

#include "lock_free_structures/lock_free_structures.hpp"

static void BM_SpscRingQueue(benchmark::State &state)
{
    lock_free_structures::SpscRingQueue<int> queue(4096);

    for (auto _ : state)
    {
        std::atomic<bool> ready{false};
        std::thread consumer([&] {
            ready.store(true, std::memory_order_release);
            int consumed = 0;
            while (consumed < state.range(0))
            {
                auto value = queue.try_pop();
                if (!value.has_value())
                {
                    std::this_thread::yield();
                    continue;
                }

                benchmark::DoNotOptimize(*value);
                ++consumed;
            }
        });

        while (!ready.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }

        for (int value = 0; value < state.range(0); ++value)
        {
            while (!queue.push(value))
            {
                std::this_thread::yield();
            }
        }

        consumer.join();
    }

    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(state.range(0)));
}

static void BM_MpmcLinkedQueue(benchmark::State &state)
{
    constexpr int kProducerCount = 4;
    constexpr int kConsumerCount = 4;

    for (auto _ : state)
    {
        lock_free_structures::MpmcLinkedQueue<int> queue;
        std::atomic<int> consumed{0};
        std::vector<std::thread> producers;
        std::vector<std::thread> consumers;

        for (int producer = 0; producer < kProducerCount; ++producer)
        {
            producers.emplace_back([&, producer] {
                auto token = queue.make_token();
                for (int index = 0; index < state.range(0); ++index)
                {
                    while (!queue.push_with_token((producer * state.range(0)) + index, token))
                    {
                        std::this_thread::yield();
                    }
                }
            });
        }

        for (int consumer_index = 0; consumer_index < kConsumerCount; ++consumer_index)
        {
            consumers.emplace_back([&] {
                auto token = queue.make_token();
                const auto target = static_cast<int>(kProducerCount * state.range(0));

                while (consumed.load(std::memory_order_acquire) < target)
                {
                    auto value = queue.try_pop_with_token(token);
                    if (!value.has_value())
                    {
                        std::this_thread::yield();
                        continue;
                    }

                    benchmark::DoNotOptimize(*value);
                    consumed.fetch_add(1, std::memory_order_release);
                }
            });
        }

        for (auto &producer_thread : producers)
        {
            producer_thread.join();
        }

        for (auto &consumer_thread : consumers)
        {
            consumer_thread.join();
        }
    }

    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kProducerCount * state.range(0)));
}

BENCHMARK(BM_SpscRingQueue)->Arg(100000);
BENCHMARK(BM_MpmcLinkedQueue)->Arg(5000);
BENCHMARK_MAIN();
