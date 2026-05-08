#include "memory_pool.hpp"

namespace memory_pool
{

std::string project_name()
{
    return "memory_pool";
}

ModuleSummary module_summary()
{
    return ModuleSummary{
        .module_name = project_name(),
        .building_blocks = 2,
    };
}

} // namespace memory_pool
