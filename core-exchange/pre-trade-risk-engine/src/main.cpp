#include <iostream>

#include "pre_trade_risk_engine/pre_trade_risk_engine.hpp"

int main()
{
    const auto summary = pre_trade_risk_engine::module_summary();
    std::cout << summary.module_name << " checks=" << summary.risk_checks
              << " reuse_points=" << summary.infrastructure_reuse_points << '\n';
    return 0;
}
