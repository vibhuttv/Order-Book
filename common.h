#pragma once

#include <cstdint>

// As per assignment requirements
struct Order {
    uint64_t order_id;      // Unique order identifier
    bool is_buy;            // true = buy, false = sell
    double price;           // Limit price
    uint64_t quantity;      // Remaining quantity
    uint64_t timestamp_ns;  // Order entry timestamp in nanoseconds
};

struct PriceLevel {
    double price;
    uint64_t total_quantity;
};
