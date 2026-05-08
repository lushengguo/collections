#pragma once

#include <cstdint>
#include <string>

#include "market_data_push_system/broadcaster.hpp"
#include "market_data_push_system/candle_aggregator.hpp"
#include "market_data_push_system/depth_book.hpp"

namespace market_data_push_system
{

struct ModuleSummary
{
    std::string module_name;
    std::uint32_t market_data_components;
};

std::string project_name();

ModuleSummary module_summary();

} // namespace market_data_push_system
