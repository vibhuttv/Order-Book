#include "order_book.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <random>
#include <iomanip>
#include <cmath>

using namespace OrderBookSystem;

// A simple function to get high-resolution time
inline uint64_t get_nanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

void run_performance_benchmark() {
    std::cout << "\n--- Running Performance Benchmark ---\n";

    // Create order book with performance-optimized configuration
    OrderBookConfig config(false, 10, 0.01); // Disable verbose logging for performance
    OrderBook book(config);
    const int num_ops = 5000000;
    std::vector<Order> orders;
    orders.reserve(num_ops);

    std::mt19937_64 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::uniform_real_distribution<double> price_dist(95.0, 105.0);
    std::uniform_int_distribution<uint64_t> quantity_dist(1, 100);
    std::uniform_int_distribution<int> op_dist(0, 100);

    uint64_t order_id_counter = 1;
    std::vector<uint64_t> active_order_ids;
    active_order_ids.reserve(num_ops);

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_ops; ++i) {
        int op = op_dist(rng);

        if (op < 60 || active_order_ids.empty()) { // 60% Add
            bool is_buy = (op % 2 == 0);
            double price = std::round(price_dist(rng) * 100.0) / 100.0;
            uint64_t quantity = quantity_dist(rng);
            uint64_t order_id = order_id_counter++;

            book.add_order({order_id, is_buy, price, quantity, get_nanos()});
            active_order_ids.push_back(order_id);

        } else if (op < 85) { // 25% Cancel
             std::uniform_int_distribution<size_t> id_dist(0, active_order_ids.size() - 1);
             size_t idx = id_dist(rng);
             uint64_t id_to_cancel = active_order_ids[idx];
             book.cancel_order(id_to_cancel);
             std::swap(active_order_ids[idx], active_order_ids.back());
             active_order_ids.pop_back();

        } else { // 15% Amend
            std::uniform_int_distribution<size_t> id_dist(0, active_order_ids.size() - 1);
            size_t idx = id_dist(rng);
            uint64_t id_to_amend = active_order_ids[idx];
            double new_price = std::round(price_dist(rng) * 100.0) / 100.0;
            uint64_t new_quantity = quantity_dist(rng);
            book.amend_order(id_to_amend, new_price, new_quantity);
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    double duration_sec = duration.count() / 1000.0;
    double ops_per_sec = num_ops / duration_sec;
    double latency_ns = (duration.count() * 1e6) / num_ops;

    std::cout << "Total Operations: " << num_ops << std::endl;
    std::cout << "Total Time: " << duration.count() << " ms" << std::endl;
    std::cout << "Operations/sec: " << std::fixed << std::setprecision(0) << ops_per_sec << std::endl;
    std::cout << "Avg. Latency/op: " << std::fixed << std::setprecision(2) << latency_ns << " ns" << std::endl;
}

int main() {
    run_performance_benchmark();
    return 0;
}

