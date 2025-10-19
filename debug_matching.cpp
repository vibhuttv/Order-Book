#include "order_book.h"
#include <iostream>
#include <iomanip>
#include <chrono>

inline uint64_t get_nanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

int main() {
    OrderBook book;
    book.set_verbose(true);  // Enable verbose output to see trades

    uint64_t id = 1;

    std::cout << "=== Testing Matching Logic ===" << std::endl;

    // Add some initial orders
    book.add_order({id++, true, 100.0, 50, get_nanos()});  // Buy 50 @ 100.0
    book.add_order({id++, true, 99.5, 30, get_nanos()});   // Buy 30 @ 99.5
    book.add_order({id++, false, 101.0, 40, get_nanos()}); // Sell 40 @ 101.0

    std::cout << "\nInitial book:" << std::endl;
    book.print_book();

    std::cout << "\nAdding aggressive sell order at 99.5 with quantity 30..." << std::endl;
    book.add_order({id++, false, 99.5, 30, get_nanos()});  // Sell 30 @ 99.5 (should match against buy at 99.5)

    std::cout << "\nFinal book:" << std::endl;
    book.print_book();

    return 0;
}
