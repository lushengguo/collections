#include <iostream>

#include "account_clearing_system/account_clearing_system.hpp"

int main()
{
    const auto summary = account_clearing_system::module_summary();
    std::cout << summary.module_name << " flows=" << summary.ledger_flows
              << " reuse_points=" << summary.infrastructure_reuse_points << '\n';
    return 0;
}
