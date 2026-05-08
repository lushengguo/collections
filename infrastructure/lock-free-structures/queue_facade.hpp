#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <utility>

namespace lock_free_structures
{

template <typename Derived, typename T> class QueueFacade
{
  public:
    using value_type = T;

    bool push(const T &value)
    {
        return derived().push_impl(value);
    }

    bool push(T &&value)
    {
        return derived().push_impl(std::move(value));
    }

    [[nodiscard]] std::optional<T> try_pop()
    {
        return derived().try_pop_impl();
    }

    [[nodiscard]] bool empty() const
    {
        return derived().empty_impl();
    }

    [[nodiscard]] std::size_t size_approx() const
    {
        return derived().size_approx_impl();
    }

    template <typename OutputIt>
    std::size_t drain(OutputIt output, std::size_t max_items = (std::numeric_limits<std::size_t>::max)())
    {
        std::size_t drained = 0;

        while (drained < max_items)
        {
            auto item = derived().try_pop_impl();
            if (!item.has_value())
            {
                break;
            }

            *output++ = std::move(*item);
            ++drained;
        }

        return drained;
    }

  protected:
    ~QueueFacade() = default;

  private:
    Derived &derived()
    {
        return static_cast<Derived &>(*this);
    }

    const Derived &derived() const
    {
        return static_cast<const Derived &>(*this);
    }
};

} // namespace lock_free_structures
