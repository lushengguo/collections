#pragma once

#include <cstdint>
#include <string>

#include "memory_pool/fixed_block_pool.hpp"
#include "memory_pool/object_pool.hpp"

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
