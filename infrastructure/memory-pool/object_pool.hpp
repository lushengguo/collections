#pragma once

#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "fixed_block_pool.hpp"

namespace memory_pool
{

template <typename T> class ObjectPool
{
  public:
    explicit ObjectPool(std::size_t blocks_per_slab = 256, std::size_t local_cache_limit = 64)
        : storage_(sizeof(T), blocks_per_slab, local_cache_limit)
    {
    }

    template <typename... Args> [[nodiscard]] T *create(Args &&...args)
    {
        void *memory = storage_.allocate();
        try
        {
            return new (memory) T(std::forward<Args>(args)...);
        }
        catch (...)
        {
            storage_.deallocate(memory);
            throw;
        }
    }

    void destroy(T *instance)
    {
        if (instance == nullptr)
        {
            return;
        }

        instance->~T();
        storage_.deallocate(instance);
    }

    [[nodiscard]] PoolStats stats() const
    {
        return storage_.stats();
    }

  private:
    FixedBlockPool storage_;
};

} // namespace memory_pool
