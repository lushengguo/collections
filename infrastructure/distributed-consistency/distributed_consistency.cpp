#include "distributed_consistency.hpp"

namespace distributed_consistency
{

std::string project_name()
{
    return "distributed_consistency";
}

ModuleSummary module_summary()
{
    return ModuleSummary{
        .module_name = project_name(),
        .consistency_components = 3,
    };
}

} // namespace distributed_consistency
