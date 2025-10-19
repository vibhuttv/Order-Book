#pragma once

#include "common.h"
#include "memory_pool.h"
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <functional> // for std::greater
#include <memory>

namespace OrderBookSystem {

// Configuration for the order book system
struct OrderBookConfig {
    bool verbose_logging = true;
    size_t default_snapshot_depth = 10;
    double price_precision = 0.01;

    OrderBookConfig() = default;
    OrderBookConfig(bool verbose, size_t depth, double precision)
        : verbose_logging(verbose), default_snapshot_depth(depth), price_precision(precision) {}
};

// Forward declaration for circular dependency
struct PriceLevelQueue;

// Internal node for the intrusive doubly-linked list
struct OrderNode {
    Order order_data;
    OrderNode *prev;
    OrderNode *next;
    PriceLevelQueue *parent_price_level_queue;

    OrderNode() : prev(nullptr), next(nullptr), parent_price_level_queue(nullptr) {}

    // Disable copy constructor and assignment
    OrderNode(const OrderNode&) = delete;
    OrderNode& operator=(const OrderNode&) = delete;
};

// Price level queue structure - manages orders at a specific price
struct PriceLevelQueue {
    double price;
    uint64_t total_quantity;
    OrderNode *head;
    OrderNode *tail;

    PriceLevelQueue(double p) : price(p), total_quantity(0), head(nullptr), tail(nullptr) {}

    // Allow move operations for std::map compatibility
    PriceLevelQueue(PriceLevelQueue&& other) noexcept
        : price(other.price), total_quantity(other.total_quantity),
          head(other.head), tail(other.tail) {
        other.head = nullptr;
        other.tail = nullptr;
    }

    PriceLevelQueue& operator=(PriceLevelQueue&& other) noexcept {
        if (this != &other) {
            price = other.price;
            total_quantity = other.total_quantity;
            head = other.head;
            tail = other.tail;
            other.head = nullptr;
            other.tail = nullptr;
        }
        return *this;
    }

    // Disable copy constructor and assignment to prevent accidental copying
    PriceLevelQueue(const PriceLevelQueue&) = delete;
    PriceLevelQueue& operator=(const PriceLevelQueue&) = delete;
};

// Interface for order book operations
class IOrderBook {
public:
    virtual ~IOrderBook() = default;
    virtual void add_order(const Order &order) = 0;
    virtual bool cancel_order(uint64_t order_id) = 0;
    virtual bool amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) = 0;
    virtual void get_snapshot(size_t depth, std::vector<PriceLevel> &bids, std::vector<PriceLevel> &asks) const = 0;
    virtual void print_book(size_t depth = 10) const = 0;
    virtual void set_verbose(bool enabled) = 0;
};

// Main OrderBook class implementing the core functionality
class OrderBook : public IOrderBook {
public:
    explicit OrderBook(const OrderBookConfig& config = OrderBookConfig{});
    ~OrderBook() override = default;

    // Core Public Interface
    void add_order(const Order &order) override;
    bool cancel_order(uint64_t order_id) override;
    bool amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) override;
    void get_snapshot(size_t depth, std::vector<PriceLevel> &bids, std::vector<PriceLevel> &asks) const override;
    void print_book(size_t depth = 10) const override;
    void set_verbose(bool enabled) override { config_.verbose_logging = enabled; }

    // Configuration access
    const OrderBookConfig& get_config() const { return config_; }
    void update_config(const OrderBookConfig& new_config) { config_ = new_config; }

private:
    // Data structures
    using BidMap = std::map<double, PriceLevelQueue, std::greater<double>>;
    using AskMap = std::map<double, PriceLevelQueue>;

    OrderBookConfig config_;
    BidMap bids_;
    AskMap asks_;
    std::unordered_map<uint64_t, OrderNode *> order_lookup_;
    MemoryPool<OrderNode> order_pool_;

    // Internal helper methods
    OrderNode* create_order_node(const Order& order);
    void cleanup_order_node(OrderNode* node);

    // Price level management
    void add_order_to_price_level_queue(OrderNode *node, PriceLevelQueue &price_level);
    void remove_order_from_price_level_queue(OrderNode *node);
    PriceLevelQueue* find_or_create_price_level(double price, bool is_buy);
    void remove_empty_price_level(double price, bool is_buy);

    // Matching engine
    void match_aggressive_order(Order &order);
    void match_buy_order(Order &order);
    void match_sell_order(Order &order);
    void match_orders();
};

} // namespace OrderBookSystem
