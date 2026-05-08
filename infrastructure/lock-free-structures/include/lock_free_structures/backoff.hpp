#pragma once

#include <cstddef>
#include <thread>

namespace lock_free_structures
{

class ExponentialBackoff
{
  public:
    void pause() noexcept
    {
        if (spins_ < kYieldThreshold)
        {
            ++spins_;
            return;
        }

        std::this_thread::yield();
    }

    void reset() noexcept
    {
        spins_ = 0;
    }

  private:
    static constexpr std::size_t kYieldThreshold = 16;
    std::size_t spins_ = 0;
};

} // namespace lock_free_structures
