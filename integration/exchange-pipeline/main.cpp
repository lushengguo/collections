#include <iostream>

#include "exchange_pipeline.hpp"

int main()
{
    std::cout << exchange_pipeline::project_name() << " integrates gateway,risk,matching,clearing,market-data\n";
    return 0;
}
