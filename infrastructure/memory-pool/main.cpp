#include <iostream>

#include "memory_pool.hpp"

namespace
{

struct DemoOrder
{
    int id;
    double price;
};

} // namespace

int main()
{
    memory_pool::ObjectPool<DemoOrder> pool;
    DemoOrder *order = pool.create(7, 42.5);
    const auto summary = memory_pool::module_summary();
    std::cout << summary.module_name << " building_blocks=" << summary.building_blocks << " order_id=" << order->id
              << '\n';
    pool.destroy(order);
    return 0;
}
