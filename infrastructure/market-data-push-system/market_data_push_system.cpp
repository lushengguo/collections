#include "market_data_push_system.hpp"

namespace market_data_push_system
{

std::string project_name()
{
    return "market_data_push_system";
}

ModuleSummary module_summary()
{
    return ModuleSummary{
        .module_name = project_name(),
        .market_data_components = 3,
    };
}

} // namespace market_data_push_system
