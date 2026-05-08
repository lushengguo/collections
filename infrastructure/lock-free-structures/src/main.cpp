#include <iostream>

#include "lock_free_structures/lock_free_structures.hpp"

int main()
{
    lock_free_structures::SpscRingQueue<int> spsc_queue(8);
    lock_free_structures::MpmcLinkedQueue<int> mpmc_queue;
    spsc_queue.push(7);
    auto producer_token = mpmc_queue.make_token();
    mpmc_queue.push_with_token(11, producer_token);

    const auto summary = lock_free_structures::module_summary();
    std::cout << summary.module_name << " queue_variants=" << summary.queue_variants
              << " spsc_front=" << *spsc_queue.try_pop() << '\n';
    return 0;
}
