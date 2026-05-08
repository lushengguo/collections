#include "lock_free_structures.hpp"

namespace lock_free_structures
{

std::string project_name()
{
    return "lock_free_structures";
}

ModuleSummary module_summary()
{
    return ModuleSummary{
        .module_name = project_name(),
        .queue_variants = 2,
    };
}

} // namespace lock_free_structures
