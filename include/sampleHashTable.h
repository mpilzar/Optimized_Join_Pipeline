#pragma once

#include <vector>
#include <functional>
#include <cstddef>
#include <utility>


template <typename Key, typename Value>
class myHashTable {
public:
    struct Entry {
        Key   first;
        Value second;   // Named first and second so the names are the same as std::unordered_map
    };

private:
    using Bucket = std::vector<Entry>;

    std::vector<Bucket> buckets;
    std::size_t capacity;

    std::size_t size = 0;
    constexpr static double MAX_LOAD_FACTOR = 0.75;


    std::size_t hash_key(const Key& key) const {  //We use std::hash as our hashing function (for now)
        return std::hash<Key>{}(key) % capacity;
    }

public:

    using iterator = Entry*; // Pointers as iterators

    explicit myHashTable(std::size_t initial_capacity = 1033) // Size (at least the initial one) should be a prime number
        : buckets(initial_capacity), capacity(initial_capacity) {}

    iterator find(const Key& key) {     // Returns pointer to entry or nullptr if not found
        std::size_t index = hash_key(key);
        for (auto& entry : buckets[index]) {
            if (entry.first == key) {
                return &entry;
            }
        }
        return nullptr;
    }

    std::pair<iterator, bool> emplace(const Key& key, const Value& value) {  // Pair entry (key-value pair) and bool that tells us if emplace succeeded
        std::size_t index = hash_key(key);
        auto& bucket = buckets[index];

        for (auto& entry : bucket) {
            if (entry.first == key) {
                return {&entry, false}; // Key already exist
            }
        }

        bucket.push_back({key, value});  // Create new key
        size++;

        if (size / capacity > MAX_LOAD_FACTOR) {  // Rehash if load factor exceeds 0.75   //
            rehash(capacity * 2);
            index = hash_key(key);
            for (auto& entry : buckets[index]) {
                if (entry.first == key) {
                    return { &entry, true };
                }
            }
        }


        return {&bucket.back(), true};
    }

    void rehash(std::size_t new_capacity) {  // Rehash
        std::vector<Bucket> new_buckets(new_capacity);
        for (auto& bucket : buckets) {
            for (auto& entry : bucket) {
                std::size_t new_index = std::hash<Key>{}(entry.first) % new_capacity;
                new_buckets[new_index].push_back(std::move(entry));
            }
        }
        buckets = std::move(new_buckets);
        capacity = new_capacity;
    }   


    iterator end() { // Return nullptr instead of implementing end iterator (for now)
        return nullptr;  // In practice .end() always returns nullptr in execute.cpp (might fix later)
    }
};