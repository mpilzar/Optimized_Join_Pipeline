#include <vector>
#include <functional>
#include <utility>
#include <cstddef>
#include <stdexcept>
#include <limits>


template <typename Key, typename Value>
class myHashTable {
public:
    struct Entry {
        Key   first;
        Value second;
        bool  occupied = false;  // Needed for emplace logic
    };

    using iterator = Entry*;

    myHashTable(std::size_t initial_capacity = 1024)        // size at 1024 gave us better results than bigger sizes
        : capacity(initial_capacity) {
        T1.assign(capacity, Entry());
        T2.assign(capacity, Entry());
    }

    iterator find(const Key& key) {
        std::size_t i1 = hash1(key);
        if (T1[i1].occupied && T1[i1].first == key)
            return &T1[i1];

        std::size_t i2 = hash2(key);
        if (T2[i2].occupied && T2[i2].first == key)
            return &T2[i2];

        return nullptr;
    }

    std::pair<iterator, bool> emplace(const Key& key, const Value& value) {
        if (iterator it = find(key)) {
            return {it, false};   // Key already exist
        }

        Key cur_key = key;
        Value cur_value = value;
        bool placed = false;
        int n_max_relocations = size + 1;  // Case 2 of rehash (+1 is added for the first insertion)

        for (int attempts = 0; attempts < n_max_relocations; attempts++) {
            std::size_t i1 = hash1(cur_key);
            if (!T1[i1].occupied) {
                T1[i1] = {cur_key, cur_value, true};
                size++;
                placed = true;

                if (load_factor_exceeded())
                    rehash(capacity * 2);

                return {&T1[i1], true};
            }

            std::swap(cur_key, T1[i1].first);  // Eviction from t1
            std::swap(cur_value, T1[i1].second);

            std::size_t i2 = hash2(cur_key);
            if (!T2[i2].occupied) {
                T2[i2] = {cur_key, cur_value, true};
                size++;
                placed = true;

                if (load_factor_exceeded())
                    rehash(capacity * 2);

                return {&T2[i2], true};
            }

            std::swap(cur_key, T2[i2].first);  // Eviction from t2
            std::swap(cur_value, T2[i2].second);
        }

        rehash(capacity * 2);  // Rehash when a cycle is detected
        return emplace(cur_key, cur_value);
    }
 
    iterator end() { // Return nullptr instead of implementing end iterator (for now)
        return nullptr;  // In practice .end() always returns nullptr in execute.cpp (might fix later)
    }

private:
    std::vector<Entry> T1; // Tables 1 and 2 (same size and type)
    std::vector<Entry> T2;
    std::size_t size = 0;
    std::size_t capacity;

    constexpr static double MAX_LOAD_FACTOR = 0.5;      //0.5 load factor gave us better results than 0.7 and 0.75

    std::size_t hash1(const Key& k) const {    // Hash function for table 1 (remainder)
        return std::hash<Key>{}(k) % capacity;
    }
    std::size_t hash2(const Key& k) const {    // Hash function for table 2  (shifted range remainder)
        return (std::hash<Key>{}(k) / capacity) % capacity;
    }

    bool load_factor_exceeded() const {
        return size / (capacity * 2) > MAX_LOAD_FACTOR;
    }

    void rehash(std::size_t new_capacity) {
        std::vector<Entry> old_T1 = std::move(T1);
        std::vector<Entry> old_T2 = std::move(T2);

        capacity = new_capacity;
        T1.assign(capacity, Entry());
        T2.assign(capacity, Entry());
        size = 0;

        for (auto& e : old_T1)
            if (e.occupied)
                emplace(e.first, e.second);
        for (auto& e : old_T2)
            if (e.occupied)
                emplace(e.first, e.second);
    }
    
};

