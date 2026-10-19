#pragma once

#include <vector>
#include <functional>
#include <cstddef>
#include <utility>
#include <iostream>

template<typename K, typename V>
class myHashTable {
private:
    struct Bucket {
        K first;
        V second;
        int psl;
        bool occupied;

        Bucket() : psl(0), occupied(false) {}
    };

    std::vector<Bucket> table;      // Actual hash table
    std::size_t capacity;
    std::size_t size;

    constexpr static double MAX_LOAD_FACTOR = 0.5;      //0.5 load factor gave us better results than 0.7 and 0.75

    std::size_t hash_key(const K& key) const {
        return std::hash<K>{}(key) % capacity;
    }

    void rehash(std::size_t new_capacity) {

        std::vector<Bucket> old_table = std::move(table);
        capacity = new_capacity;
        size = 0;
        table.clear();
        table.resize(capacity);

        for (auto& b : old_table) {     // re-insert old entries
            if (b.occupied) {
                emplace(std::move(b.first), std::move(b.second));
            }
        }
    }

public:
    using iterator = Bucket*;

    explicit myHashTable(std::size_t cap = 1024): capacity(cap), size(0) {      // size at 1024 gave us better results than bigger sizes
        table.resize(capacity);
    }

    ~myHashTable() = default;

    iterator find(const K& key) {
    
        std::size_t pos = hash_key(key);
        int current_psl = 0;

        while (table[pos].occupied) {               // While bucket is occupied
            if (table[pos].first == key)
                return &table[pos];
            if (table[pos].psl < current_psl)       // If current psl is greater than the psl of the bucket, key not found
                break;

            pos = (pos + 1) % capacity;
            current_psl++;
        }
        return end();
    }

    std::pair<iterator, bool> emplace(const K& key, const V& value) {
        if (static_cast<double>(size) / capacity > MAX_LOAD_FACTOR)     // if approaching load factor, we rehash
            rehash(capacity * 2);

        std::size_t pos = hash_key(key);
        int current_psl = 0;
        K current_key = key;
        V current_value = value;

        while (true) {          //if bucket is empty we insert the new value
            if (!table[pos].occupied) {
                table[pos].first = std::move(current_key);
                table[pos].second = std::move(current_value);
                table[pos].occupied = true;
                table[pos].psl = current_psl;
                size++;
                return { &table[pos], true };
            }

            if (table[pos].first == key) {          // Key already exists
                return { &table[pos], false };
            }

            if (table[pos].psl < current_psl) {     //the new key has higher psl so we swap
                std::swap(current_key, table[pos].first);
                std::swap(current_value, table[pos].second);
                std::swap(current_psl, table[pos].psl);
            }

            pos = (pos + 1) % capacity;
            current_psl++;
        }
    }

    iterator end() {
        return nullptr;
    }
};