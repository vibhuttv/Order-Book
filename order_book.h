#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <list>
#include <memory>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <cstring>

// Order structure
struct Order {
    uint64_t order_id;     // Unique order identifier
    bool is_buy;           // true = buy, false = sell
    double price;          // Limit price
    uint64_t quantity;     // Remaining quantity
    uint64_t timestamp_ns; // Order entry timestamp in nanoseconds
    
    Order() = default;
    Order(uint64_t id, bool buy, double p, uint64_t q, uint64_t ts)
        : order_id(id), is_buy(buy), price(p), quantity(q), timestamp_ns(ts) {}
};

// Price level aggregated data
struct PriceLevel {
    double price;
    uint64_t total_quantity;
    
    PriceLevel() : price(0.0), total_quantity(0) {}
    PriceLevel(double p, uint64_t q) : price(p), total_quantity(q) {}
};

// Memory pool for order allocations - minimizes heap fragmentation
template<typename T, size_t BlockSize = 4096>
class MemoryPool {
private:
    struct Block {
        uint8_t data[BlockSize * sizeof(T)];
        Block* next;
    };
    
    Block* current_block_;
    size_t current_offset_;
    std::vector<Block*> all_blocks_;
    std::vector<T*> free_list_;
    
public:
    MemoryPool() : current_block_(nullptr), current_offset_(0) {
        allocate_new_block();
    }
    
    ~MemoryPool() {
        for (auto* block : all_blocks_) {
            delete block;
        }
    }
    
    // Non-copyable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    template<typename... Args>
    T* construct(Args&&... args) {
        T* ptr = allocate();
        new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }
    
    void destroy(T* ptr) {
        if (ptr) {
            ptr->~T();
            free_list_.push_back(ptr);
        }
    }
    
private:
    void allocate_new_block() {
        Block* new_block = new Block();
        new_block->next = current_block_;
        current_block_ = new_block;
        current_offset_ = 0;
        all_blocks_.push_back(new_block);
    }
    
    T* allocate() {
        // Reuse from free list first
        if (!free_list_.empty()) {
            T* ptr = free_list_.back();
            free_list_.pop_back();
            return ptr;
        }
        
        // Allocate from current block
        if (current_offset_ >= BlockSize) {
            allocate_new_block();
        }
        
        T* ptr = reinterpret_cast<T*>(
            current_block_->data + current_offset_ * sizeof(T)
        );
        ++current_offset_;
        return ptr;
    }
};

// Intrusive linked list node for orders at a price level
struct OrderNode {
    Order order;
    OrderNode* next;
    OrderNode* prev;
    
    OrderNode() : next(nullptr), prev(nullptr) {}
    explicit OrderNode(const Order& o) : order(o), next(nullptr), prev(nullptr) {}
};

// Price level with intrusive linked list of orders (FIFO)
class PriceLevelQueue {
private:
    double price_;
    uint64_t total_quantity_;
    OrderNode* head_;
    OrderNode* tail_;
    size_t order_count_;
    
public:
    PriceLevelQueue(double price) 
        : price_(price), total_quantity_(0), head_(nullptr), tail_(nullptr), order_count_(0) {}
    
    ~PriceLevelQueue() = default;
    
    inline double get_price() const { return price_; }
    inline uint64_t get_total_quantity() const { return total_quantity_; }
    inline size_t get_order_count() const { return order_count_; }
    inline bool is_empty() const { return head_ == nullptr; }
    
    // Add order to the end (FIFO)
    inline void add_order(OrderNode* node) {
        total_quantity_ += node->order.quantity;
        ++order_count_;
        
        if (!tail_) {
            head_ = tail_ = node;
            node->next = node->prev = nullptr;
        } else {
            tail_->next = node;
            node->prev = tail_;
            node->next = nullptr;
            tail_ = node;
        }
    }
    
    // Remove a specific order
    inline void remove_order(OrderNode* node) {
        total_quantity_ -= node->order.quantity;
        --order_count_;
        
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }
        
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }
    }
    
    // Update order quantity
    inline void update_quantity(OrderNode* /*node*/, uint64_t old_qty, uint64_t new_qty) {
        total_quantity_ = total_quantity_ - old_qty + new_qty;
    }
    
    OrderNode* get_head() const { return head_; }
};

class OrderBook {
private:
    // Buy side: descending order (highest price first)
    std::map<double, PriceLevelQueue*, std::greater<double>> bids_;
    
    // Sell side: ascending order (lowest price first)
    std::map<double, PriceLevelQueue*, std::less<double>> asks_;
    
    // Fast O(1) order lookup
    std::unordered_map<uint64_t, OrderNode*> order_lookup_;
    
    // Memory pools for cache-friendly allocations
    MemoryPool<OrderNode, 1024> order_node_pool_;
    MemoryPool<PriceLevelQueue, 256> price_level_pool_;
    
public:
    OrderBook() = default;
    ~OrderBook();
    
    // Insert a new order into the book
    void add_order(const Order& order);
    
    // Cancel an existing order by its ID
    bool cancel_order(uint64_t order_id);
    
    // Amend an existing order's price or quantity
    bool amend_order(uint64_t order_id, double new_price, uint64_t new_quantity);
    
    // Get a snapshot of top N bid and ask levels (aggregated quantities)
    void get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const;
    
    // Print current state of the order book
    void print_book(size_t depth = 10) const;
    
    // Get best bid and ask (for optional matching)
    inline double get_best_bid() const {
        return bids_.empty() ? 0.0 : bids_.begin()->first;
    }
    
    inline double get_best_ask() const {
        return asks_.empty() ? 0.0 : asks_.begin()->first;
    }
    
private:
    PriceLevelQueue* get_or_create_price_level(bool is_buy, double price);
    void remove_price_level_if_empty(bool is_buy, double price, PriceLevelQueue* level);
};
