#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace memory_pool
{

struct PoolStats
{
    std::size_t block_size = 0;
    std::size_t blocks_per_slab = 0;
    std::size_t slabs_allocated = 0;
    std::size_t total_blocks = 0;
    std::size_t global_free_blocks = 0;
    std::size_t outstanding_blocks = 0;
    std::uint64_t local_cache_hits = 0;
    std::uint64_t global_cache_hits = 0;
    std::uint64_t slab_refills = 0;
};

class FixedBlockPool
{
  public:
    FixedBlockPool(std::size_t block_size, std::size_t blocks_per_slab = 256, std::size_t local_cache_limit = 64)
        : block_size_(std::max(block_size, sizeof(void *))), blocks_per_slab_(blocks_per_slab),
          local_cache_limit_(local_cache_limit)
    {
        if (blocks_per_slab_ == 0 || local_cache_limit_ == 0)
        {
            throw std::invalid_argument("FixedBlockPool requires non-zero slab and cache sizes");
        }
    }

    FixedBlockPool(const FixedBlockPool &) = delete;
    FixedBlockPool &operator=(const FixedBlockPool &) = delete;

    ~FixedBlockPool()
    {
        local_caches().erase(this);
    }

    [[nodiscard]] void *allocate()
    {
        auto &cache = local_cache();
        if (!cache.free_blocks.empty())
        {
            void *block = cache.free_blocks.back();
            cache.free_blocks.pop_back();
            local_cache_hits_.fetch_add(1, std::memory_order_relaxed);
            outstanding_blocks_.fetch_add(1, std::memory_order_relaxed);
            return block;
        }

        refill_local_cache(cache);

        if (cache.free_blocks.empty())
        {
            throw std::bad_alloc();
        }

        void *block = cache.free_blocks.back();
        cache.free_blocks.pop_back();
        outstanding_blocks_.fetch_add(1, std::memory_order_relaxed);
        return block;
    }

    void deallocate(void *block)
    {
        if (block == nullptr)
        {
            return;
        }

        auto &cache = local_cache();
        cache.free_blocks.push_back(block);
        outstanding_blocks_.fetch_sub(1, std::memory_order_relaxed);

        if (cache.free_blocks.size() >= local_cache_limit_)
        {
            flush_local_cache(cache, local_cache_limit_ / 2);
        }
    }

    [[nodiscard]] PoolStats stats() const
    {
        std::scoped_lock lock(global_mutex_);
        return PoolStats{
            .block_size = block_size_,
            .blocks_per_slab = blocks_per_slab_,
            .slabs_allocated = slabs_.size(),
            .total_blocks = slabs_.size() * blocks_per_slab_,
            .global_free_blocks = global_free_blocks_.size(),
            .outstanding_blocks = outstanding_blocks_.load(std::memory_order_acquire),
            .local_cache_hits = local_cache_hits_.load(std::memory_order_acquire),
            .global_cache_hits = global_cache_hits_.load(std::memory_order_acquire),
            .slab_refills = slab_refills_.load(std::memory_order_acquire),
        };
    }

  private:
    struct LocalCache
    {
        std::vector<void *> free_blocks;
    };

    using LocalCacheMap = std::unordered_map<FixedBlockPool *, LocalCache>;

    static LocalCacheMap &local_caches()
    {
        thread_local std::unordered_map<FixedBlockPool *, LocalCache> caches;
        return caches;
    }

    LocalCache &local_cache()
    {
        return local_caches()[this];
    }

    void refill_local_cache(LocalCache &cache)
    {
        std::scoped_lock lock(global_mutex_);
        const auto refill_target = std::max<std::size_t>(local_cache_limit_ / 2, 1U);

        if (global_free_blocks_.empty())
        {
            allocate_slab();
        }

        const auto transfer_count = std::min(refill_target, global_free_blocks_.size());
        for (std::size_t index = 0; index < transfer_count; ++index)
        {
            cache.free_blocks.push_back(global_free_blocks_.back());
            global_free_blocks_.pop_back();
        }

        global_cache_hits_.fetch_add(transfer_count, std::memory_order_relaxed);
    }

    void flush_local_cache(LocalCache &cache, std::size_t retain_count)
    {
        std::scoped_lock lock(global_mutex_);

        while (cache.free_blocks.size() > retain_count)
        {
            global_free_blocks_.push_back(cache.free_blocks.back());
            cache.free_blocks.pop_back();
        }
    }

    void allocate_slab()
    {
        const std::size_t slab_bytes = block_size_ * blocks_per_slab_;
        auto slab = std::make_unique<std::byte[]>(slab_bytes);
        std::byte *slab_begin = slab.get();

        for (std::size_t offset = 0; offset < blocks_per_slab_; ++offset)
        {
            global_free_blocks_.push_back(static_cast<void *>(slab_begin + (offset * block_size_)));
        }

        slabs_.push_back(std::move(slab));
        slab_refills_.fetch_add(1, std::memory_order_relaxed);
    }

    const std::size_t block_size_;
    const std::size_t blocks_per_slab_;
    const std::size_t local_cache_limit_;
    mutable std::mutex global_mutex_;
    std::vector<std::unique_ptr<std::byte[]>> slabs_;
    std::vector<void *> global_free_blocks_;
    std::atomic<std::size_t> outstanding_blocks_{0};
    std::atomic<std::uint64_t> local_cache_hits_{0};
    std::atomic<std::uint64_t> global_cache_hits_{0};
    std::atomic<std::uint64_t> slab_refills_{0};
};

} // namespace memory_pool
