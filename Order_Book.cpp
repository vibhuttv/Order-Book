#include "order_book.h"

OrderBook::~OrderBook() {
    // Clean up all orders
    for (auto& [order_id, node] : order_lookup_) {
        order_node_pool_.destroy(node);
    }
    order_lookup_.clear();
    
    // Clean up all price levels
    for (auto& [price, level] : bids_) {
        price_level_pool_.destroy(level);
    }
    for (auto& [price, level] : asks_) {
        price_level_pool_.destroy(level);
    }
    
    bids_.clear();
    asks_.clear();
}

PriceLevelQueue* OrderBook::get_or_create_price_level(bool is_buy, double price) {
    if (is_buy) {
        auto it = bids_.find(price);
        if (it != bids_.end()) {
            return it->second;
        }
        PriceLevelQueue* new_level = price_level_pool_.construct(price);
        bids_[price] = new_level;
        return new_level;
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) {
            return it->second;
        }
        PriceLevelQueue* new_level = price_level_pool_.construct(price);
        asks_[price] = new_level;
        return new_level;
    }
}

void OrderBook::remove_price_level_if_empty(bool is_buy, double price, PriceLevelQueue* level) {
    if (level->is_empty()) {
        if (is_buy) {
            bids_.erase(price);
        } else {
            asks_.erase(price);
        }
        price_level_pool_.destroy(level);
    }
}

void OrderBook::add_order(const Order& order) {
    // Check if order already exists
    if (order_lookup_.find(order.order_id) != order_lookup_.end()) {
        return; // Duplicate order ID
    }
    
    // Allocate order node from memory pool
    OrderNode* node = order_node_pool_.construct(order);
    
    // Get or create price level
    PriceLevelQueue* level = get_or_create_price_level(order.is_buy, order.price);
    
    // Add order to price level (FIFO)
    level->add_order(node);
    
    // Add to lookup table
    order_lookup_[order.order_id] = node;
}

bool OrderBook::cancel_order(uint64_t order_id) {
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) {
        return false; // Order not found
    }
    
    OrderNode* node = it->second;
    const Order& order = node->order;
    
    // Get the price level
    PriceLevelQueue* level = nullptr;
    if (order.is_buy) {
        auto level_it = bids_.find(order.price);
        if (level_it == bids_.end()) {
            return false;
        }
        level = level_it->second;
    } else {
        auto level_it = asks_.find(order.price);
        if (level_it == asks_.end()) {
            return false;
        }
        level = level_it->second;
    }
    
    // Remove order from price level
    level->remove_order(node);
    
    // Remove from lookup
    order_lookup_.erase(it);
    
    // Destroy node
    order_node_pool_.destroy(node);
    
    // Clean up empty price level
    remove_price_level_if_empty(order.is_buy, order.price, level);
    
    return true;
}

bool OrderBook::amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) {
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) {
        return false; // Order not found
    }
    
    OrderNode* node = it->second;
    Order& order = node->order;
    
    // If price changed, treat as cancel + add
    if (order.price != new_price) {
        // Save order details
        bool is_buy = order.is_buy;
        uint64_t timestamp_ns = order.timestamp_ns;
        
        // Cancel old order
        cancel_order(order_id);
        
        // Add new order with new price
        Order new_order(order_id, is_buy, new_price, new_quantity, timestamp_ns);
        add_order(new_order);
        
        return true;
    }
    
    // Only quantity changed - update in place
    if (order.quantity != new_quantity) {
        // Get the price level
        PriceLevelQueue* level = nullptr;
        if (order.is_buy) {
            auto level_it = bids_.find(order.price);
            if (level_it != bids_.end()) {
                level = level_it->second;
            }
        } else {
            auto level_it = asks_.find(order.price);
            if (level_it != asks_.end()) {
                level = level_it->second;
            }
        }
        
        if (level) {
            uint64_t old_qty = order.quantity;
            order.quantity = new_quantity;
            level->update_quantity(node, old_qty, new_quantity);
        }
    }
    
    return true;
}

void OrderBook::get_snapshot(size_t depth, std::vector<PriceLevel>& bids, std::vector<PriceLevel>& asks) const {
    bids.clear();
    asks.clear();
    
    // Reserve space for efficiency
    bids.reserve(std::min(depth, bids_.size()));
    asks.reserve(std::min(depth, asks_.size()));
    
    // Collect top N bids (already sorted descending)
    size_t count = 0;
    for (const auto& [price, level] : bids_) {
        if (count >= depth) break;
        bids.emplace_back(price, level->get_total_quantity());
        ++count;
    }
    
    // Collect top N asks (already sorted ascending)
    count = 0;
    for (const auto& [price, level] : asks_) {
        if (count >= depth) break;
        asks.emplace_back(price, level->get_total_quantity());
        ++count;
    }
}

void OrderBook::print_book(size_t depth) const {
    std::vector<PriceLevel> bids, asks;
    get_snapshot(depth, bids, asks);
    
    std::cout << "\n========== ORDER BOOK ==========\n";
    std::cout << std::fixed << std::setprecision(2);
    
    // Print header
    std::cout << std::setw(15) << "BID QTY" 
              << std::setw(12) << "BID PX"
              << "  |  "
              << std::setw(12) << "ASK PX"
              << std::setw(15) << "ASK QTY" << "\n";
    std::cout << std::string(60, '-') << "\n";
    
    // Print levels
    size_t max_levels = std::max(bids.size(), asks.size());
    for (size_t i = 0; i < max_levels; ++i) {
        // Bid side
        if (i < bids.size()) {
            std::cout << std::setw(15) << bids[i].total_quantity
                      << std::setw(12) << bids[i].price;
        } else {
            std::cout << std::setw(15) << "" << std::setw(12) << "";
        }
        
        std::cout << "  |  ";
        
        // Ask side
        if (i < asks.size()) {
            std::cout << std::setw(12) << asks[i].price
                      << std::setw(15) << asks[i].total_quantity;
        } else {
            std::cout << std::setw(12) << "" << std::setw(15) << "";
        }
        
        std::cout << "\n";
    }
    
    std::cout << "================================\n";
    
    // Print spread
    if (!bids.empty() && !asks.empty()) {
        double spread = asks[0].price - bids[0].price;
        double mid_price = (asks[0].price + bids[0].price) / 2.0;
        std::cout << "Spread: " << spread 
                  << " | Mid: " << mid_price << "\n";
    }
    
    std::cout << std::endl;
}
