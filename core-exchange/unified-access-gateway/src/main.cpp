#include <iostream>

#include "unified_access_gateway/unified_access_gateway.hpp"

int main()
{
    const auto summary = unified_access_gateway::module_summary();
    std::cout << summary.module_name << " controls=" << summary.ingress_controls
              << " reuse_points=" << summary.infrastructure_reuse_points << '\n';
    return 0;
}
