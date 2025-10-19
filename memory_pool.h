#pragma once

#include <cstdint>
#include <vector>
#include <new> // for new(ptr)
#include <utility> // for std::forward

// Memory pool for fixed-size object allocations to minimize heap fragmentation and improve cache performance.
template<typename T, size_t BlockSize = 4096>
class MemoryPool {
private:
    struct Block {
        // Ensure proper alignment for T to avoid undefined behavior
        typename std::aligned_storage<sizeof(T), alignof(T)>::type data[BlockSize];
        Block* next;
    };
    
    Block* current_block_;
    size_t current_offset_;
    std::vector<Block*> all_blocks_;
    std::vector<T*> free_list_; // Stores pointers to destroyed objects for reuse.
    
public:
    MemoryPool() : current_block_(nullptr), current_offset_(0) {
        allocate_new_block();
    }
    
    ~MemoryPool() {
        for (auto* block : all_blocks_) {
            delete block;
        }
    }
    
    // Non-copyable to prevent accidental copying of the pool.
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    // Allocate memory and construct an object in place.
    template<typename... Args>
    T* construct(Args&&... args) {
        T* ptr = allocate();
        new (ptr) T(std::forward<Args>(args)...); // Placement new
        return ptr;
    }
    
    // Destruct an object and return its memory to the free list.
    void destroy(T* ptr) {
        if (ptr) {
            ptr->~T(); // Explicitly call the destructor
            free_list_.push_back(ptr);
        }
    }
    
private:
    void allocate_new_block() {
        Block* new_block = new Block();
        new_block->next = nullptr;
        
        // Link the new block to the list of all blocks for eventual cleanup
        if (current_block_ != nullptr) {
            current_block_->next = new_block;
        }
        all_blocks_.push_back(new_block);
        current_block_ = new_block;
        current_offset_ = 0;
    }
    
    T* allocate() {
        // First, try to reuse a previously destroyed object.
        if (!free_list_.empty()) {
            T* ptr = free_list_.back();
            free_list_.pop_back();
            return ptr;
        }
        
        // If the current block is full, allocate a new one.
        if (current_offset_ >= BlockSize) {
            allocate_new_block();
        }
        
        // Get the pointer to the next available slot in the current block's data array.
        T* ptr = reinterpret_cast<T*>(&current_block_->data[current_offset_]);
        ++current_offset_;
        return ptr;
    }
};
