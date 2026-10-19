#pragma once

#include <vector>
#include <optional>
#include <functional>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <limits>
#include <type_traits>
#include <iostream>



template <typename Key, typename Value>
class myHashTable {
public:
    struct Entry {
        Key first;
        Value second;
    };

    using iterator = Entry*;

    explicit myHashTable(std::size_t initial_capacity = 1033)       // size at 1033 gave us better results than bigger sizes
        : table(initial_capacity), capacity(initial_capacity) {}

    iterator find(const Key& key) {
        std::size_t home = hash_key(key);
        uint32_t hop = table[home].hop_info;
        
        for (std::size_t i = 0; i < H && hop != 0; ++i) { // Only check positions in neighbourhood 
            if (hop & (1U << i)) {
                std::size_t pos = (home + i) % capacity;
                if (table[pos].occupied && table[pos].entry.first == key) {
                    return &table[pos].entry;
                }
            }
        }
        return nullptr;
    }

    std::pair<iterator, bool> emplace(const Key& key, const Value& value) {
        iterator existing = find(key);
        if (existing != nullptr) {  // Key already exist
            return {existing, false};
        }
        
        if ((size + 1) / capacity > MAX_LOAD_FACTOR) {  // Rehash if approaching load factor
            rehash(capacity * 2);
        }
        
        std::size_t home = hash_key(key);
        
        const int MAX_REHASH_ATTEMPTS = 5;  // Add limit to amount of rehash attempts to avoid excessive runtimes
        for (int rehash_attempt = 0; rehash_attempt < MAX_REHASH_ATTEMPTS; ++rehash_attempt) {
            if (insert_at(home, key, value)) {
                return {find(key), true};
            }
            
            std::size_t new_size = capacity * 3;  // Rehash with x3 instead of x2 if insertion fails
            rehash(new_size);
            home = hash_key(key);  // Recalculate homes
        }
        
        throw std::runtime_error("Failed to insert after multiple rehashing attempts");  // Error if too many rehashes happen
    }

    iterator end() { // Return nullptr instead of implementing end iterator (for now)
        return nullptr;  // In practice .end() always returns nullptr in execute.cpp (might fix later)
    }
    
    std::size_t get_size() const { return size; }
    std::size_t get_capacity() const { return capacity; }

private:
    struct Slot {
        Entry entry;
        bool occupied = false;
        uint32_t hop_info = 0;  // Bitmap
        
        Slot() : occupied(false), hop_info(0) {}
    };

    std::vector<Slot> table;
    std::size_t capacity;
    std::size_t size = 0;
    constexpr static std::size_t H = 32;  // Neighborhood size (should be 64 based on cache size, but 32 gave me better results)
    constexpr static double MAX_LOAD_FACTOR = 0.5;      //0.5 load factor gave us better results than 0.7 and 0.75

    std::size_t hash_key(const Key& key) const {
        return std::hash<Key>{}(key) % capacity;
    }

    std::size_t find_empty_slot(std::size_t start) const {   // Linear probing function to find next empty slot
        std::size_t max_search = std::min(capacity, 4 * H);  
        for (std::size_t i = 0; i < max_search; ++i) {
            std::size_t pos = (start + i) % capacity;
            if (!table[pos].occupied) {
                return pos;
            }
        }
        return capacity;  // No empty slot found within a close range with linear probing
    }

    bool move_closer(std::size_t& empty_pos, std::size_t target) { // Function to move the empty slot closer to the hashed index
        for (std::size_t dist = 1; dist < H; ++dist) {
            std::size_t check_pos = (empty_pos - dist + capacity) % capacity;
            
            if (!table[check_pos].occupied) {
                continue;
            }
        
            std::size_t home = hash_key(table[check_pos].entry.first); // Finmd home of object
            
            std::size_t current_offset = (check_pos - home + capacity) % capacity; // Calculate offset
            std::size_t new_offset = (empty_pos - home + capacity) % capacity;
            
            /*
            Check if we can move:
            1. Current object must be within the neighbourhood (< H)
            2. Bit must be set in hopinfo
            3. new_offset must also stay within negighbourhood (< H)   */
            if (current_offset < H && new_offset < H && 
                (table[home].hop_info & (1U << current_offset))) {
                
                table[empty_pos].entry.first = table[check_pos].entry.first;  // Move object
                table[empty_pos].entry.second = table[check_pos].entry.second;
                table[empty_pos].occupied = true;
                
                table[home].hop_info &= ~(1U << current_offset);  // Update
                table[home].hop_info |= (1U << new_offset);      
                
                table[check_pos].occupied = false;  // Set position as unoccupied
                empty_pos = check_pos;
                return true;
            }
        }
        return false; 
    }

    bool insert_at(std::size_t home, const Key& key, const Value& value) { // Main insert fuinction

        std::size_t empty_pos = find_empty_slot(home); // Find empty position
        if (empty_pos == capacity) {
            return false;  
        }


        std::size_t attempts = 0;
        const std::size_t MAX_ATTEMPTS = 4 * H;  
        
        while ((empty_pos - home + capacity) % capacity >= H) { // Move it closer
            if (attempts++ >= MAX_ATTEMPTS) {
                return false;  
            }
            
            if (!move_closer(empty_pos, home)) {
                return false;  // rehash if cant move
            }
        }

        table[empty_pos].entry.first = key; // Store element
        table[empty_pos].entry.second = value;
        table[empty_pos].occupied = true;
        
        std::size_t offset = (empty_pos - home + capacity) % capacity;
        table[home].hop_info |= (1U << offset);
        
        size++;
        return true;
    }

    static std::size_t next_prime(std::size_t n) {  // Find next prime (for rehashing, capacity should be a prime number)
        if (n <= 2) return 2;
        if (n == 3) return 3;
        if (n % 2 == 0) n++;
        
        while (true) {
            bool is_prime = true;
            for (std::size_t i = 3; i * i <= n; i += 2) {
                if (n % i == 0) {
                    is_prime = false;
                    break;
                }
            }
            if (is_prime) return n;
            n += 2;
        }
    }

    void rehash(std::size_t new_capacity) {
        new_capacity = next_prime(new_capacity);
        
        std::vector<Slot> old_table = std::move(table);
        std::size_t old_capacity = capacity;
        
        table = std::vector<Slot>(new_capacity);
        capacity = new_capacity;
        size = 0;
        
        for (std::size_t i = 0; i < old_capacity; ++i) {
            if (old_table[i].occupied) {
                std::size_t home = hash_key(old_table[i].entry.first);
                if (!insert_at(home, old_table[i].entry.first, old_table[i].entry.second)) {
                    table = std::move(old_table);
                    capacity = old_capacity;
                    size = 0;
                    
                    for (const auto& slot : table) {
                        if (slot.occupied) size++;
                    }
                    
                    rehash(new_capacity * 2);
                    return;
                }
            }
        }
    }
};