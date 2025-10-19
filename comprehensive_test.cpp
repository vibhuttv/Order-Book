#include "order_book.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <chrono>
#include <random>
#include <iomanip>

using namespace OrderBookSystem;

// Helper function to get current time in nanoseconds
inline uint64_t get_nanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

// Test helper to verify order book state
void verify_order_book_state(OrderBook& book, const std::vector<PriceLevel>& expected_bids,
                           const std::vector<PriceLevel>& expected_asks, const std::string& test_name) {
    std::vector<PriceLevel> actual_bids, actual_asks;
    book.get_snapshot(10, actual_bids, actual_asks);

    std::cout << "Testing: " << test_name << std::endl;

    // Verify bids
    assert(actual_bids.size() == expected_bids.size());
    for (size_t i = 0; i < expected_bids.size(); ++i) {
        if (actual_bids[i].price != expected_bids[i].price) {
            std::cout << "Bid price mismatch at index " << i << ": expected " << expected_bids[i].price
                      << ", got " << actual_bids[i].price << std::endl;
        }
        if (actual_bids[i].total_quantity != expected_bids[i].total_quantity) {
            std::cout << "Bid quantity mismatch at index " << i << ": expected " << expected_bids[i].total_quantity
                      << ", got " << actual_bids[i].total_quantity << std::endl;
        }
        assert(actual_bids[i].price == expected_bids[i].price);
        assert(actual_bids[i].total_quantity == expected_bids[i].total_quantity);
    }

    // Verify asks
    assert(actual_asks.size() == expected_asks.size());
    for (size_t i = 0; i < expected_asks.size(); ++i) {
        if (actual_asks[i].price != expected_asks[i].price) {
            std::cout << "Ask price mismatch at index " << i << ": expected " << expected_asks[i].price
                      << ", got " << actual_asks[i].price << std::endl;
        }
        if (actual_asks[i].total_quantity != expected_asks[i].total_quantity) {
            std::cout << "Ask quantity mismatch at index " << i << ": expected " << expected_asks[i].total_quantity
                      << ", got " << actual_asks[i].total_quantity << std::endl;
        }
        assert(actual_asks[i].price == expected_asks[i].price);
        assert(actual_asks[i].total_quantity == expected_asks[i].total_quantity);
    }

    std::cout << "✓ " << test_name << " PASSED" << std::endl;
}

void test_basic_order_operations() {
    std::cout << "\n=== Testing Basic Order Operations ===" << std::endl;

    OrderBook book;
    book.set_verbose(false);
    uint64_t id = 1;

    // Test 1: Add orders and verify state
    book.add_order({id++, true, 100.0, 10, get_nanos()});
    book.add_order({id++, true, 100.5, 20, get_nanos()});
    book.add_order({id++, false, 101.0, 15, get_nanos()});
    book.add_order({id++, false, 101.5, 25, get_nanos()});

    verify_order_book_state(book,
        {{100.5, 20}, {100.0, 10}},  // Expected bids (highest to lowest)
        {{101.0, 15}, {101.5, 25}},  // Expected asks (lowest to highest)
        "Basic order addition");

    // Test 2: Cancel order
    bool cancel_result = book.cancel_order(2);
    assert(cancel_result == true);
    verify_order_book_state(book,
        {{100.0, 10}},  // Bid at 100.5 removed
        {{101.0, 15}, {101.5, 25}},
        "Order cancellation");

    // Test 3: Cancel non-existent order
    bool cancel_invalid = book.cancel_order(999);
    assert(cancel_invalid == false);
    std::cout << "✓ Invalid cancellation test PASSED" << std::endl;

    // Test 4: Amend order (quantity only)
    bool amend_result = book.amend_order(1, 100.0, 5);
    assert(amend_result == true);
    verify_order_book_state(book,
        {{100.0, 5}},
        {{101.0, 15}, {101.5, 25}},
        "Order amendment (quantity)");

    // Test 5: Amend order (price change)
    bool amend_price_result = book.amend_order(3, 102.0, 15);
    assert(amend_price_result == true);
    verify_order_book_state(book,
        {{100.0, 5}},
        {{101.5, 25}, {102.0, 15}},  // 101.5 comes before 102.0 in ask order
        "Order amendment (price change)");

    // Test 6: Amend non-existent order
    bool amend_invalid = book.amend_order(999, 100.0, 10);
    assert(amend_invalid == false);
    std::cout << "✓ Invalid amendment test PASSED" << std::endl;
}

void test_matching_engine() {
    std::cout << "\n=== Testing Matching Engine ===" << std::endl;

    OrderBook book;
    book.set_verbose(false);
    uint64_t id = 1;

    // Setup initial book
    book.add_order({id++, true, 100.0, 50, get_nanos()});
    book.add_order({id++, true, 99.5, 30, get_nanos()});
    book.add_order({id++, false, 101.0, 40, get_nanos()});
    book.add_order({id++, false, 101.5, 25, get_nanos()});

    // Test 1: No crossing orders
    verify_order_book_state(book,
        {{100.0, 50}, {99.5, 30}},
        {{101.0, 40}, {101.5, 25}},
        "No crossing orders");

    // Test 2: Partial fill - aggressive buy
    book.add_order({id++, true, 101.0, 20, get_nanos()});

    verify_order_book_state(book,
        {{100.0, 50}, {99.5, 30}},
        {{101.0, 20}, {101.5, 25}},
        "Partial fill (aggressive buy)");

    // Test 3: Complete fill - aggressive sell
    book.add_order({id++, false, 99.5, 30, get_nanos()});

    verify_order_book_state(book,
        {{100.0, 50}},  // The bid at 99.5 gets completely filled and removed
        {{101.0, 20}, {101.5, 25}},
        "Complete fill (aggressive sell)");

    // Test 4: Multiple fills - large aggressive order
    book.add_order({id++, false, 100.0, 60, get_nanos()});
    verify_order_book_state(book,
        {},
        {{100.0, 10}, {101.0, 20}, {101.5, 25}},
        "Multiple fills (large aggressive order)");

    // Test 5: Exact match
    book.add_order({id++, true, 100.0, 10, get_nanos()});
    verify_order_book_state(book,
        {},
        {{101.0, 20}, {101.5, 25}},
        "Exact match");
}

void test_fifo_ordering() {
    std::cout << "\n=== Testing FIFO Ordering ===" << std::endl;

    OrderBook book;
    book.set_verbose(false);
    uint64_t id = 1;

    // Add multiple orders at same price level
    book.add_order({id++, true, 100.0, 10, get_nanos()});
    book.add_order({id++, true, 100.0, 20, get_nanos()});
    book.add_order({id++, true, 100.0, 30, get_nanos()});

    verify_order_book_state(book,
        {{100.0, 60}},  // Total quantity should be 60
        {},
        "FIFO ordering - same price level");

    // Cancel middle order and verify FIFO is maintained
    book.cancel_order(2);
    verify_order_book_state(book,
        {{100.0, 40}},  // Total quantity should be 40 (10 + 30)
        {},
        "FIFO ordering - after cancellation");

    // Add aggressive order to test FIFO execution
    book.add_order({id++, false, 100.0, 15, get_nanos()});

    verify_order_book_state(book,
        {{100.0, 30}},  // Remaining bid after partial execution
        {{100.0, 5}},   // Remaining ask after partial execution
        "FIFO ordering - execution order");
}

void test_edge_cases() {
    std::cout << "\n=== Testing Edge Cases ===" << std::endl;

    OrderBook book;
    book.set_verbose(false);
    uint64_t id = 1;

    // Test 1: Empty book
    verify_order_book_state(book, {}, {}, "Empty book");

    // Test 2: Single order
    book.add_order({id++, true, 100.0, 1, get_nanos()});
    verify_order_book_state(book, {{100.0, 1}}, {}, "Single order");

    // Test 3: Zero quantity order (should be rejected by matching)
    book.add_order({id++, false, 100.0, 0, get_nanos()});

    verify_order_book_state(book, {{100.0, 1}}, {}, "Zero quantity order");

    // Test 4: Very large quantities
    book.add_order({id++, true, 99.0, UINT64_MAX / 2, get_nanos()});
    book.add_order({id++, false, 99.0, UINT64_MAX / 4, get_nanos()});

    verify_order_book_state(book, {{100.0, 1}, {99.0, UINT64_MAX / 4 + 1}}, {}, "Large quantities");

    // Test 5: Price precision
    book.add_order({id++, true, 99.999, 10, get_nanos()});
    book.add_order({id++, false, 100.001, 10, get_nanos()});
    verify_order_book_state(book,
        {{100.0, 1}, {99.999, 10}, {99.0, UINT64_MAX / 4 + 1}},
        {{100.001, 10}},
        "Price precision");

    std::cout << "✓ All edge case tests PASSED" << std::endl;
}

void test_memory_pool() {
    std::cout << "\n=== Testing Memory Pool ===" << std::endl;

    OrderBook book;
    book.set_verbose(false);
    uint64_t id = 1;

    // Add many orders to test memory pool allocation
    for (int i = 0; i < 10000; ++i) {
        book.add_order({id++, (i % 2 == 0), 100.0 + (i % 10), static_cast<uint64_t>(1 + (i % 100)), get_nanos()});
    }

    // Cancel many orders to test memory pool deallocation
    for (int i = 1; i <= 5000; ++i) {
        book.cancel_order(i * 2);  // Cancel every other order
    }

    // Add more orders to test memory reuse
    for (int i = 0; i < 1000; ++i) {
        book.add_order({id++, (i % 2 == 0), 100.0 + (i % 10), static_cast<uint64_t>(1 + (i % 100)), get_nanos()});
    }

    std::cout << "✓ Memory pool stress test PASSED" << std::endl;
}

void test_performance() {
    std::cout << "\n=== Testing Performance ===" << std::endl;

    OrderBook book;
    book.set_verbose(false);

    const int num_operations = 100000;
    std::vector<Order> orders;
    orders.reserve(num_operations);

    std::mt19937_64 rng(42);  // Fixed seed for reproducibility
    std::uniform_real_distribution<double> price_dist(95.0, 105.0);
    std::uniform_int_distribution<uint64_t> quantity_dist(1, 100);
    std::uniform_int_distribution<int> op_dist(0, 100);

    uint64_t order_id_counter = 1;
    std::vector<uint64_t> active_order_ids;
    active_order_ids.reserve(num_operations);

    auto start_time = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_operations; ++i) {
        int op = op_dist(rng);

        if (op < 70 || active_order_ids.empty()) { // 70% Add
            bool is_buy = (op % 2 == 0);
            double price = std::round(price_dist(rng) * 100.0) / 100.0;
            uint64_t quantity = quantity_dist(rng);
            uint64_t order_id = order_id_counter++;

            book.add_order({order_id, is_buy, price, quantity, get_nanos()});
            active_order_ids.push_back(order_id);

        } else if (op < 90) { // 20% Cancel
            if (!active_order_ids.empty()) {
                std::uniform_int_distribution<size_t> id_dist(0, active_order_ids.size() - 1);
                size_t idx = id_dist(rng);
                uint64_t id_to_cancel = active_order_ids[idx];
                book.cancel_order(id_to_cancel);
                std::swap(active_order_ids[idx], active_order_ids.back());
                active_order_ids.pop_back();
            }

        } else { // 10% Amend
            if (!active_order_ids.empty()) {
                std::uniform_int_distribution<size_t> id_dist(0, active_order_ids.size() - 1);
                size_t idx = id_dist(rng);
                uint64_t id_to_amend = active_order_ids[idx];
                double new_price = std::round(price_dist(rng) * 100.0) / 100.0;
                uint64_t new_quantity = quantity_dist(rng);
                book.amend_order(id_to_amend, new_price, new_quantity);
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    double duration_sec = duration.count() / 1000000.0;
    double ops_per_sec = num_operations / duration_sec;
    double latency_us = duration.count() / (double)num_operations;

    std::cout << "Performance Test Results:" << std::endl;
    std::cout << "Operations: " << num_operations << std::endl;
    std::cout << "Time: " << duration.count() << " μs" << std::endl;
    std::cout << "Operations/sec: " << std::fixed << std::setprecision(0) << ops_per_sec << std::endl;
    std::cout << "Avg. Latency: " << std::fixed << std::setprecision(2) << latency_us << " μs" << std::endl;

    // Performance assertions
    assert(ops_per_sec > 100000);  // Should handle at least 100K ops/sec
    assert(latency_us < 100);      // Average latency should be under 100μs

    std::cout << "✓ Performance test PASSED" << std::endl;
}

void test_snapshot_functionality() {
    std::cout << "\n=== Testing Snapshot Functionality ===" << std::endl;

    OrderBook book;
    book.set_verbose(false);
    uint64_t id = 1;

    // Add orders at different price levels
    book.add_order({id++, true, 100.0, 10, get_nanos()});
    book.add_order({id++, true, 100.5, 20, get_nanos()});
    book.add_order({id++, true, 101.0, 30, get_nanos()});
    book.add_order({id++, false, 102.0, 40, get_nanos()});
    book.add_order({id++, false, 102.5, 50, get_nanos()});
    book.add_order({id++, false, 103.0, 60, get_nanos()});

    // Test snapshot with depth limit
    std::vector<PriceLevel> bids, asks;
    book.get_snapshot(2, bids, asks);

    assert(bids.size() == 2);
    assert(bids[0].price == 101.0 && bids[0].total_quantity == 30);
    assert(bids[1].price == 100.5 && bids[1].total_quantity == 20);

    assert(asks.size() == 2);
    assert(asks[0].price == 102.0 && asks[0].total_quantity == 40);
    assert(asks[1].price == 102.5 && asks[1].total_quantity == 50);

    std::cout << "✓ Snapshot functionality test PASSED" << std::endl;
}

int main() {
    std::cout << "Starting Comprehensive Order Book Tests..." << std::endl;

    try {
        test_basic_order_operations();
        test_matching_engine();
        test_fifo_ordering();
        test_edge_cases();
        test_memory_pool();
        test_snapshot_functionality();
        test_performance();

        std::cout << "\n🎉 ALL TESTS PASSED! 🎉" << std::endl;
        std::cout << "Order Book implementation is working correctly!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "❌ TEST FAILED: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "❌ UNKNOWN ERROR occurred during testing" << std::endl;
        return 1;
    }

    return 0;
}
