#include <iostream>

#include "distributed_consistency/distributed_consistency.hpp"

int main()
{
    distributed_consistency::TccWallet wallet(1000.0);
    distributed_consistency::OutboxStore outbox;
    const bool reserved = wallet.try_reserve("demo", 150.0);
    outbox.append({.id = "evt-1", .topic = "ledger.transfer", .payload = "reserved"});

    const auto summary = distributed_consistency::module_summary();
    std::cout << summary.module_name << " consistency_components=" << summary.consistency_components
              << " pending_messages=" << outbox.pending_count() << " reserved=" << reserved << '\n';
    return 0;
}
