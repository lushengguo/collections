#pragma once

#include <cstdint>
#include <string>

#include "outbox.hpp"
#include "saga.hpp"
#include "tcc_wallet.hpp"

namespace distributed_consistency
{

struct ModuleSummary
{
    std::string module_name;
    std::uint32_t consistency_components;
};

[[nodiscard]] std::string project_name();

[[nodiscard]] ModuleSummary module_summary();

} // namespace distributed_consistency
