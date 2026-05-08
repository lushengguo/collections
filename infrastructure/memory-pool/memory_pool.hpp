#pragma once

#include <cstdint>
#include <string>

#include "fixed_block_pool.hpp"
#include "object_pool.hpp"

namespace memory_pool
{

struct ModuleSummary
{
    std::string module_name;
    std::uint32_t building_blocks;
};

std::string project_name();

ModuleSummary module_summary();

} // namespace memory_pool
