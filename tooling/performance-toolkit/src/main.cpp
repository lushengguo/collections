#include <iostream>

#include "performance_toolkit/performance_toolkit.hpp"

int main()
{
    const auto summary = performance_toolkit::module_summary();
    std::cout << summary.module_name << " capabilities=" << summary.toolkit_capabilities
              << " reuse_points=" << summary.infrastructure_reuse_points << '\n';
    return 0;
}
