#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "market_data_push_system/market_types.hpp"

namespace market_data_push_system
{

class DepthBook
{
  public:
    explicit DepthBook(std::string symbol) : symbol_(std::move(symbol))
    {
    }

    [[nodiscard]] DepthDelta update(Side side, double price, double quantity)
    {
        if (side == Side::kBid)
        {
            apply_update(bids_, price, quantity);
        }
        else
        {
            apply_update(asks_, price, quantity);
        }

        return DepthDelta{.side = side, .price = price, .quantity = quantity};
    }

    [[nodiscard]] DepthSnapshot snapshot(std::size_t depth) const
    {
        DepthSnapshot snapshot{.symbol = symbol_};
        append_levels(bids_, depth, snapshot.bids);
        append_levels(asks_, depth, snapshot.asks);
        return snapshot;
    }

  private:
    using BidBook = std::map<double, double, std::greater<double>>;
    using AskBook = std::map<double, double, std::less<double>>;

    template <typename Book> static void apply_update(Book &book, double price, double quantity)
    {
        if (quantity <= 0.0)
        {
            book.erase(price);
        }
        else
        {
            book[price] = quantity;
        }
    }

    template <typename Book>
    static void append_levels(const Book &book, std::size_t depth, std::vector<BookLevel> &output)
    {
        output.reserve(depth);
        std::size_t count = 0;
        for (const auto &[price, quantity] : book)
        {
            output.push_back(BookLevel{.price = price, .quantity = quantity});
            ++count;
            if (count == depth)
            {
                break;
            }
        }
    }

    std::string symbol_;
    BidBook bids_;
    AskBook asks_;
};

} // namespace market_data_push_system
