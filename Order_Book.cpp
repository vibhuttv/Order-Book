#include "order_book.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace OrderBookSystem {

// OrderBook Implementation
OrderBook::OrderBook(const OrderBookConfig& config)
    : config_(config) {
}

void OrderBook::add_order(const Order &order) {
    // First, try to match the new order against existing orders
    Order remaining_order = order;
    match_aggressive_order(remaining_order);

    // If there's remaining quantity, add it to the book
    if (remaining_order.quantity > 0) {
        OrderNode* node = create_order_node(remaining_order);

        PriceLevelQueue* price_level = find_or_create_price_level(
            remaining_order.price, remaining_order.is_buy);

        add_order_to_price_level_queue(node, *price_level);
        order_lookup_[remaining_order.order_id] = node;
    }
}

bool OrderBook::cancel_order(uint64_t order_id) {
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) {
        return false; // Order not found
    }

    OrderNode *node_to_cancel = it->second;
    PriceLevelQueue *price_level = node_to_cancel->parent_price_level_queue;

    remove_order_from_price_level_queue(node_to_cancel);
    order_lookup_.erase(it);
    cleanup_order_node(node_to_cancel);

    // If the price level is now empty, remove it from the map
    if (price_level->total_quantity == 0) {
        remove_empty_price_level(price_level->price,
                                node_to_cancel->order_data.is_buy);
    }

    return true;
}

bool OrderBook::amend_order(uint64_t order_id, double new_price, uint64_t new_quantity) {
    auto it = order_lookup_.find(order_id);
    if (it == order_lookup_.end()) {
        return false; // Order not found
    }

    OrderNode *node = it->second;
    const Order &old_order = node->order_data;

    // If price changes, it's a cancel + add, which changes priority.
    if (old_order.price != new_price) {
        Order new_order = old_order;
        new_order.price = new_price;
        new_order.quantity = new_quantity;

        cancel_order(order_id);
        add_order(new_order);
    }
    else if (old_order.quantity != new_quantity) {
        // If only quantity changes, update in place.
        PriceLevelQueue *price_level = node->parent_price_level_queue;
        price_level->total_quantity -= old_order.quantity;
        price_level->total_quantity += new_quantity;
        node->order_data.quantity = new_quantity;
    }

    return true;
}

void OrderBook::get_snapshot(size_t depth, std::vector<PriceLevel> &bids, std::vector<PriceLevel> &asks) const {
    bids.clear();
    asks.clear();
    bids.reserve(depth);
    asks.reserve(depth);

    auto bid_it = bids_.begin();
    for (size_t i = 0; i < depth && bid_it != bids_.end(); ++i, ++bid_it) {
        bids.push_back({bid_it->first, bid_it->second.total_quantity});
    }

    auto ask_it = asks_.begin();
    for (size_t i = 0; i < depth && ask_it != asks_.end(); ++i, ++ask_it) {
        asks.push_back({ask_it->first, ask_it->second.total_quantity});
    }
}

void OrderBook::print_book(size_t depth) const {
    std::vector<PriceLevel> ask_levels, bid_levels;
    get_snapshot(depth, bid_levels, ask_levels);

    std::cout << std::string(50, '-') << std::endl;
    std::cout << "ORDER BOOK" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    std::cout << std::setw(24) << std::left << "ASKS" << "|" << std::setw(24) << std::right << "BIDS" << std::endl;
    std::cout << std::setw(12) << std::left << "Price" << std::setw(12) << std::right << "Quantity" << "|";
    std::cout << std::setw(12) << std::left << " Quantity" << std::setw(12) << std::right << "Price" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    size_t num_levels = std::max(ask_levels.size(), bid_levels.size());
    std::reverse(ask_levels.begin(), ask_levels.end()); // Print asks from high to low

    for (size_t i = 0; i < num_levels; ++i) {
        // Print asks
        if (i < ask_levels.size()) {
            std::cout << std::fixed << std::setprecision(2) << std::setw(12) << std::left << ask_levels[i].price
                      << std::setw(12) << std::right << ask_levels[i].total_quantity;
        }
        else {
            std::cout << std::setw(24) << " ";
        }

        std::cout << "|";

        // Print bids
        if (i < bid_levels.size()) {
            std::cout << std::fixed << std::setprecision(2) << std::setw(13) << std::left << bid_levels[i].total_quantity
                      << std::setw(11) << std::right << bid_levels[i].price;
        }
        else {
            std::cout << std::setw(24) << " ";
        }
        std::cout << std::endl;
    }
    std::cout << std::string(50, '-') << std::endl;
}

OrderNode* OrderBook::create_order_node(const Order& order) {
    OrderNode *node = order_pool_.construct();
    node->order_data = order;
    node->prev = nullptr;
    node->next = nullptr;
    node->parent_price_level_queue = nullptr;
    return node;
}

void OrderBook::cleanup_order_node(OrderNode* node) {
    order_pool_.destroy(node);
}

// Price Level Management
void OrderBook::add_order_to_price_level_queue(OrderNode *node, PriceLevelQueue &price_level) {
    node->parent_price_level_queue = &price_level;
    if (price_level.head == nullptr) {
        // Price level is empty
        price_level.head = node;
        price_level.tail = node;
    }
    else {
        // Append to tail for FIFO
        price_level.tail->next = node;
        node->prev = price_level.tail;
        price_level.tail = node;
    }
    price_level.total_quantity += node->order_data.quantity;
}

void OrderBook::remove_order_from_price_level_queue(OrderNode *node) {
    PriceLevelQueue *price_level = node->parent_price_level_queue;
    price_level->total_quantity -= node->order_data.quantity;

    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }
    if (price_level->head == node) {
        price_level->head = node->next;
    }
    if (price_level->tail == node) {
        price_level->tail = node->prev;
    }
}

PriceLevelQueue* OrderBook::find_or_create_price_level(double price, bool is_buy) {
    if (is_buy) {
        auto it = bids_.find(price);
        if (it == bids_.end()) {
            it = bids_.emplace(price, PriceLevelQueue(price)).first;
        }
        return &it->second;
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) {
            it = asks_.emplace(price, PriceLevelQueue(price)).first;
        }
        return &it->second;
    }
}

void OrderBook::remove_empty_price_level(double price, bool is_buy) {
    if (is_buy) {
        bids_.erase(price);
    } else {
        asks_.erase(price);
    }
}

// Matching Engine
void OrderBook::match_aggressive_order(Order &order) {
    if (order.is_buy) {
        match_buy_order(order);
    } else {
        match_sell_order(order);
    }
}

void OrderBook::match_buy_order(Order &order) {
    // For buy orders, match against asks (sell orders)
    while (!asks_.empty() && order.quantity > 0) {
        auto ask_it = asks_.begin();
        double ask_price = ask_it->first;

        // Only match if the buy order price is >= ask price
        if (order.price >= ask_price) {
            PriceLevelQueue &ask_level = ask_it->second;
            OrderNode *ask_node = ask_level.head;

            uint64_t trade_quantity = std::min(order.quantity, ask_node->order_data.quantity);

            if (config_.verbose_logging) {
                std::cout << "--- TRADE EXECUTED ---\n"
                          << "Price: " << std::fixed << std::setprecision(2) << ask_price
                          << " | Quantity: " << trade_quantity << "\n"
                          << "Buy Order ID: " << order.order_id
                          << " | Sell Order ID: " << ask_node->order_data.order_id << std::endl;
            }

            order.quantity -= trade_quantity;
            ask_node->order_data.quantity -= trade_quantity;
            ask_level.total_quantity -= trade_quantity;

            if (ask_node->order_data.quantity == 0) {
                uint64_t id = ask_node->order_data.order_id;
                remove_order_from_price_level_queue(ask_node);
                order_lookup_.erase(id);
                cleanup_order_node(ask_node);
            }

            if (ask_level.total_quantity == 0) {
                asks_.erase(ask_it);
            }
        }
        else {
            break; // No more matching possible
        }
    }
}

void OrderBook::match_sell_order(Order &order) {
    // For sell orders, match against bids (buy orders)
    auto bid_it = bids_.lower_bound(order.price);
    if (bid_it == bids_.end()) {
        return; // No bids at or above the sell price
    }

    // Match against bids starting from the best price level
    while (bid_it != bids_.end() && order.quantity > 0) {
        double bid_price = bid_it->first;

        // Only match if the bid price is >= sell order price
        if (bid_price >= order.price) {
            PriceLevelQueue &bid_level = bid_it->second;
            OrderNode *bid_node = bid_level.head;

            uint64_t trade_quantity = std::min(order.quantity, bid_node->order_data.quantity);

            if (config_.verbose_logging) {
                std::cout << "--- TRADE EXECUTED ---\n"
                          << "Price: " << std::fixed << std::setprecision(2) << bid_price
                          << " | Quantity: " << trade_quantity << "\n"
                          << "Buy Order ID: " << bid_node->order_data.order_id
                          << " | Sell Order ID: " << order.order_id << std::endl;
            }

            order.quantity -= trade_quantity;
            bid_node->order_data.quantity -= trade_quantity;
            bid_level.total_quantity -= trade_quantity;

            if (bid_node->order_data.quantity == 0) {
                uint64_t id = bid_node->order_data.order_id;
                remove_order_from_price_level_queue(bid_node);
                order_lookup_.erase(id);
                cleanup_order_node(bid_node);
            }

            if (bid_level.total_quantity == 0) {
                bid_it = bids_.erase(bid_it);
            }
            else {
                ++bid_it;
            }
        }
        else {
            break; // No more matching possible
        }
    }
}

void OrderBook::match_orders() {
    // Keep matching while there are crossing orders
    while (!bids_.empty() && !asks_.empty() &&
           bids_.begin()->first >= asks_.begin()->first) {

        PriceLevelQueue &best_bid_price_level = bids_.begin()->second;
        PriceLevelQueue &best_ask_price_level = asks_.begin()->second;

        OrderNode *bid_order_node = best_bid_price_level.head;
        OrderNode *ask_order_node = best_ask_price_level.head;

        uint64_t trade_quantity = std::min(bid_order_node->order_data.quantity,
                                          ask_order_node->order_data.quantity);

        double trade_price = best_ask_price_level.price;  // Use ask price as trade price

        if (config_.verbose_logging) {
            std::cout << "--- TRADE EXECUTED ---\n"
                      << "Price: " << std::fixed << std::setprecision(2) << trade_price
                      << " | Quantity: " << trade_quantity << "\n"
                      << "Buy Order ID: " << bid_order_node->order_data.order_id
                      << " | Sell Order ID: " << ask_order_node->order_data.order_id << std::endl;
        }

        bid_order_node->order_data.quantity -= trade_quantity;
        ask_order_node->order_data.quantity -= trade_quantity;

        best_bid_price_level.total_quantity -= trade_quantity;
        best_ask_price_level.total_quantity -= trade_quantity;

        if (bid_order_node->order_data.quantity == 0) {
            uint64_t id = bid_order_node->order_data.order_id;
            remove_order_from_price_level_queue(bid_order_node);
            order_lookup_.erase(id);
            cleanup_order_node(bid_order_node);
        }

        if (ask_order_node->order_data.quantity == 0) {
            uint64_t id = ask_order_node->order_data.order_id;
            remove_order_from_price_level_queue(ask_order_node);
            order_lookup_.erase(id);
            cleanup_order_node(ask_order_node);
        }

        if (best_bid_price_level.total_quantity == 0) {
            bids_.erase(bids_.begin());
        }
        if (best_ask_price_level.total_quantity == 0) {
            asks_.erase(asks_.begin());
        }
    }
}

} // namespace OrderBookSystem
