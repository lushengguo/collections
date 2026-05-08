#pragma once

#include <cstdint>
#include <string>

#include "broadcaster.hpp"
#include "candle_aggregator.hpp"
#include "depth_book.hpp"

namespace market_data_push_system
{

struct ModuleSummary
{
    std::string module_name;
    std::uint32_t market_data_components;
};

[[nodiscard]] std::string project_name();

[[nodiscard]] ModuleSummary module_summary();

} // namespace market_data_push_system
