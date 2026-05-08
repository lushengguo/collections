#pragma once

#include <cstdint>
#include <string>

#include "distributed_consistency/outbox.hpp"
#include "distributed_consistency/saga.hpp"
#include "distributed_consistency/tcc_wallet.hpp"

namespace distributed_consistency
{

struct ModuleSummary
{
    std::string module_name;
    std::uint32_t consistency_components;
};

std::string project_name();

ModuleSummary module_summary();

} // namespace distributed_consistency
