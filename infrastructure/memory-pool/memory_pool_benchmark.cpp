#include <benchmark/benchmark.h>

#include <thread>
#include <vector>

#include "memory_pool.hpp"

namespace
{

struct BenchmarkOrder
{
    std::uint64_t order_id;
    double price;
    double quantity;
};

} // namespace

static void BM_FixedBlockPool(benchmark::State &state)
{
    memory_pool::FixedBlockPool pool(sizeof(BenchmarkOrder), 512, 64);

    for (auto _ : state)
    {
        std::vector<void *> blocks;
        blocks.reserve(static_cast<std::size_t>(state.range(0)));

        for (int index = 0; index < state.range(0); ++index)
        {
            blocks.push_back(pool.allocate());
        }

        for (void *block : blocks)
        {
            pool.deallocate(block);
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void BM_ObjectPool(benchmark::State &state)
{
    memory_pool::ObjectPool<BenchmarkOrder> pool(512, 64);

    for (auto _ : state)
    {
        std::vector<BenchmarkOrder *> objects;
        objects.reserve(static_cast<std::size_t>(state.range(0)));

        for (int index = 0; index < state.range(0); ++index)
        {
            objects.push_back(pool.create(static_cast<std::uint64_t>(index), 100.0, 2.0));
        }

        for (BenchmarkOrder *object : objects)
        {
            pool.destroy(object);
        }
    }

    state.SetItemsProcessed(state.iterations() * state.range(0));
}

BENCHMARK(BM_FixedBlockPool)->Arg(10000);
BENCHMARK(BM_ObjectPool)->Arg(10000);
BENCHMARK_MAIN();
