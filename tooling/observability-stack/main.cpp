#include <iostream>

#include "observability_stack.hpp"

int main()
{
    const auto summary = observability_stack::module_summary();
    std::cout << summary.module_name << " capabilities=" << summary.core_capabilities
              << " reuse_points=" << summary.infrastructure_reuse_points << '\n';
    return 0;
}
