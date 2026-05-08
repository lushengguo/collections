#pragma once

#include <cstdint>
#include <string>

#include "mpmc_linked_queue.hpp"
#include "spsc_ring_queue.hpp"

namespace lock_free_structures
{

struct ModuleSummary
{
    std::string module_name;
    std::uint32_t queue_variants;
};

std::string project_name();

ModuleSummary module_summary();

} // namespace lock_free_structures
