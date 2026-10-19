#ifndef PARALLEL_PROJECT_H
#define PARALLEL_PROJECT_H

#include <hardware.h>
#include <plan.h>
#include <table.h>
#include <cstdint>
#include <vector>
#include <thread>
#include <memory>
#include <atomic>

#include "project2.h"

namespace Contest {

/////////////////////////////////////////////////////////
///////////////////////NEW://///////////////////////////
/////////////////////////////////////////////////////////

// Thread pool context (hash table build parallelization)
struct ExecutionContext {
    size_t num_threads;
    
    ExecutionContext() {
        // Use hardware concurrency for actual parallel work
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
        if (num_threads > 8) num_threads = 8;  // Cap at 8
    }
};

// Helper function to check if a column has any NULL values (better indexing task)
inline bool column_has_nulls(const Column& input_column) {
    for (auto* page : input_column.pages) {
        auto* page_data = page->data;
        uint16_t num_rows = *reinterpret_cast<const uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<const uint16_t*>(page_data + 2);
        
        // If num_rows != num_non_null, there are nulls
        if (num_rows != num_non_null) {
            return true;
        }
    }
    return false;
}



////////// 3-Level Slab Allocator

constexpr size_t LARGE_CHUNK_SIZE = 64 * 1024;      // Level 1: 64KB per thread
constexpr size_t SMALL_CHUNK_SIZE = 8 * 1024;       // Level 2: 8KB per partition
constexpr size_t TUPLE_SIZE = sizeof(AdjacencyEntry);

// Linked list node for chunks
struct ChunkNode {
    std::byte* data;
    size_t size;
    size_t used;
    ChunkNode* next;
    bool owns_memory; // tracks if data should be deleted
    
    ChunkNode(size_t sz) : size(sz), used(0), next(nullptr), owns_memory(sz > 0) {
        if (sz > 0) {
            data = new std::byte[sz];
        } else {
            data = nullptr;
        }
    }
    
    ~ChunkNode() {
        if (owns_memory && data) {
            delete[] data;
        }
    }
};

// Level 3: Partition-local allocator (allocates individual tuples)
class BumpAllocPartition {
public:
    ChunkNode* head;
    ChunkNode* current;
    
    BumpAllocPartition() : head(nullptr), current(nullptr) {}
    
    ~BumpAllocPartition() {
        ChunkNode* node = head;
        while (node) {
            ChunkNode* next = node->next;
            delete node;
            node = next;
        }
    }
    
    size_t free_space() const {
        if (!current) return 0;
        return current->size - current->used;
    }
    
    void add_space(std::byte* data, size_t size) {
        ChunkNode* node = new ChunkNode(0);  // Empty node, just wraps existing data
        node->data = data;
        node->size = size;
        node->used = 0;
        node->next = nullptr;
        node->owns_memory = false; 
        
        if (!head) {
            head = current = node;
        } else {
            current->next = node;
            current = node;
        }
    }
    
    AdjacencyEntry* allocate() {
        if (!current || free_space() < TUPLE_SIZE) {
            return nullptr;
        }
        auto* result = reinterpret_cast<AdjacencyEntry*>(current->data + current->used);
        current->used += TUPLE_SIZE;
        return result;
    }
};

// Level 2: Thread-local allocator (allocates small chunks for partitions)
class BumpAllocThread {
public:
    ChunkNode* head;
    ChunkNode* current;
    
    BumpAllocThread() : head(nullptr), current(nullptr) {}
    
    ~BumpAllocThread() {
        ChunkNode* node = head;
        while (node) {
            ChunkNode* next = node->next;
            delete node;
            node = next;
        }
    }
    
    size_t free_space() const {
        if (!current) return 0;
        return current->size - current->used;
    }
    
    void add_space(std::byte* data, size_t size) {
        ChunkNode* node = new ChunkNode(0);
        node->data = data;
        node->size = size;
        node->used = 0;
        node->next = nullptr;
        node->owns_memory = true; 
        
        if (!head) {
            head = current = node;
        } else {
            current->next = node;
            current = node;
        }
    }
    
    std::byte* allocate(size_t size) {
        if (!current || free_space() < size) {
            return nullptr;
        }
        std::byte* result = current->data + current->used;
        current->used += size;
        return result;
    }
};

// Level 1: Global allocator (allocates large chunks per thread)
class GlobalAllocator {
public:
    std::byte* allocate_large_chunk() {
        return new std::byte[LARGE_CHUNK_SIZE];
    }
    
    void deallocate(std::byte* ptr) {
        delete[] ptr;
    }
};

// Per-thread tuple collection struct
struct ThreadLocalCollector {
    GlobalAllocator& global_alloc;
    BumpAllocThread level2;
    std::vector<BumpAllocPartition> level3;
    std::vector<size_t> counts;
    size_t num_partitions;
    
    ThreadLocalCollector(GlobalAllocator& alloc, size_t n_parts) 
        : global_alloc(alloc)
        , num_partitions(n_parts) {
        level3.resize(num_partitions);
        counts.resize(num_partitions, 0);
    }
    
    void consume(int32_t key, size_t payload, uint32_t hash) {
        // Determine partition from bits of hash
        uint32_t part = 0;
        if (num_partitions > 1) {
            int log2_parts = __builtin_ctz(num_partitions);  // Fast log2 for power of 2
            part = (hash >> (32 - log2_parts));
        }
        
        // Ensure level3[part] has space
        auto& partition = level3[part];
        if (partition.free_space() < TUPLE_SIZE) {
            // Need new small chunk from level2
            if (level2.free_space() < SMALL_CHUNK_SIZE) {
                // Need new large chunk from level1
                std::byte* large = global_alloc.allocate_large_chunk();
                level2.add_space(large, LARGE_CHUNK_SIZE);
            }
            std::byte* small = level2.allocate(SMALL_CHUNK_SIZE);
            partition.add_space(small, SMALL_CHUNK_SIZE);
        }
        
        // Allocate tuple in level3[part]
        AdjacencyEntry* entry = partition.allocate();
        entry->key = key;
        entry->payload = payload;
        
        counts[part]++;
    }
};


/////////////////////////////////////////////////////////
///////////////////////CHANGED://////////////////////////
/////////////////////////////////////////////////////////




/////////////////////////////////////////////////////////////////////////////////////////
////COLUMN_T WAS ALSO CHANGED, BUT WE KEEP IT IN PROJECT2.H FILE DUE TO DEPENDENCY ISSUES
/////////////////////////////////////////////////////////////////////////////////////////



// Parallel Hash Table with build_parallel method (changed existing unchained hashtbl)
class UnchainedHashTable {
private:
    std::vector<uint64_t> directory;
    std::vector<AdjacencyEntry> adjacency;
    size_t num_buckets = 0;
    unsigned prefix_shift = 32;

public:
    UnchainedHashTable() = default;

    // Parallel build with partitioning
    void build_parallel(const std::vector<int32_t>& keys, const std::vector<size_t>& payloads, size_t num_threads) {
        size_t n = keys.size();
        assert(n == payloads.size());
        
        directory.clear();
        adjacency.clear();
        num_buckets = 0;
        prefix_shift = 32;
        
        if (n == 0) return;
        if (num_threads == 0) num_threads = 1;
        
        // Use fixed number of partitions (4) to minimize merging overhead 
        size_t num_partitions = 4;
        
        // PHASE 1: Parallel tuple collection with 3-level slab allocator
        GlobalAllocator global_alloc;
        std::vector<std::unique_ptr<ThreadLocalCollector>> collectors;
        collectors.reserve(num_threads);
        
        for (size_t i = 0; i < num_threads; ++i) {
            collectors.push_back(std::make_unique<ThreadLocalCollector>(global_alloc, num_partitions));
        }
        
        // Partition tuples among threads
        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        size_t chunk_size = (n + num_threads - 1) / num_threads;
        
        for (size_t t = 0; t < num_threads; ++t) {
            size_t start = t * chunk_size;
            size_t end = std::min(start + chunk_size, n);
            
            threads.emplace_back([&, t, start, end]() {
                auto& collector = *collectors[t];
                for (size_t i = start; i < end; ++i) {
                    uint32_t hash = crc32(keys[i] ^ 0x9e3779b9u);
                    collector.consume(keys[i], payloads[i], hash);
                }
            });
        }
        
        // Wait for all threads to finish collection
        for (auto& thread : threads) {
            thread.join();
        }
        threads.clear();
        
        // PHASE 2: Process each partition 
        std::vector<std::vector<AdjacencyEntry>> partition_tuples(num_partitions);
        std::vector<size_t> partition_counts(num_partitions, 0);
        
        for (size_t p = 0; p < num_partitions; ++p) {
            size_t total = 0;
            for (size_t t = 0; t < num_threads; ++t) {
                total += collectors[t]->counts[p];
            }
            partition_counts[p] = total;
            partition_tuples[p].reserve(total);
            
            for (size_t t = 0; t < num_threads; ++t) {
                auto& level3 = collectors[t]->level3[p];
                ChunkNode* node = level3.head;
                while (node) {
                    size_t count = node->used / TUPLE_SIZE;
                    AdjacencyEntry* entries = reinterpret_cast<AdjacencyEntry*>(node->data);
                    for (size_t i = 0; i < count; ++i) {
                        partition_tuples[p].push_back(entries[i]);
                    }
                    node = node->next;
                }
            }
        }
        
        size_t total_tuples = 0;
        for (size_t count : partition_counts) {
            total_tuples += count;
        }
        
        if (total_tuples == 0) return;
        
        uint64_t target = (total_tuples + 3) / 4;
        if (target == 0) target = 1;
        num_buckets = next_power2(target);
        
        int prefix_bits = ilog2_u64(num_buckets);
        prefix_shift = 32 - prefix_bits;
        
        // Count tuples per directory bucket
        std::vector<size_t> dir_counts(num_buckets, 0);
        
        for (size_t p = 0; p < num_partitions; ++p) {
            for (const auto& entry : partition_tuples[p]) {
                uint32_t hash = crc32(entry.key ^ 0x9e3779b9u);
                uint32_t prefix = (prefix_bits == 0) ? 0 : (hash >> prefix_shift);
                dir_counts[prefix]++;
            }
        }
        
        // Compute prefix sum (start positions)
        std::vector<size_t> starts(num_buckets + 1, 0);
        for (size_t b = 0; b < num_buckets; ++b) {
            starts[b + 1] = starts[b] + dir_counts[b];
        }
        
        // Allocate adjacency array
        adjacency.resize(total_tuples);
        
        // Initialize bloom filters and positions
        std::vector<uint16_t> blooms(num_buckets, 0);
        std::vector<size_t> positions = starts;
        
        // Process partitions sequentially
        for (size_t p = 0; p < num_partitions; ++p) {
            for (const auto& entry : partition_tuples[p]) {
                uint32_t hash = crc32(entry.key ^ 0x9e3779b9u);
                uint32_t prefix = (prefix_bits == 0) ? 0 : (hash >> prefix_shift);
                
                uint32_t suffix;
                if (prefix_shift == 32 || prefix_shift == 0) {
                    suffix = hash;
                } else {
                    uint32_t mask = (1u << prefix_shift) - 1u;
                    suffix = hash & mask;
                }
                blooms[prefix] |= compute_bloom(suffix);
                
                size_t idx = positions[prefix]++;
                adjacency[idx] = entry;
            }
        }
        
        // Build directory
        directory.resize(num_buckets + 1);
        constexpr uintptr_t MASK = 0x0000FFFFFFFFFFFFULL;
        
        for (size_t b = 0; b < num_buckets; ++b) {
            uintptr_t ptr = reinterpret_cast<uintptr_t>(&adjacency[starts[b]]);
            directory[b] = ((ptr & MASK) << 16) | blooms[b];
        }
        uintptr_t end_ptr = reinterpret_cast<uintptr_t>(&adjacency[total_tuples]);
        directory[num_buckets] = ((end_ptr & MASK) << 16);
    }

    // Original sequential build (unchanged)
    void build(const std::vector<int32_t>& keys, const std::vector<size_t>& payloads) {
        size_t n = keys.size();
        assert(n == payloads.size());

        directory.clear();
        adjacency.clear();
        num_buckets = 0;
        prefix_shift = 32;

        if (n == 0) 
            return;

        uint64_t target = (n + 3) / 4;
        if (target == 0) 
            target = 1;
        num_buckets = next_power2(target);

        int prefix_bits = ilog2_u64(num_buckets);
        prefix_shift = 32 - prefix_bits;

        struct Build { 
            int32_t key; 
            size_t payload; 
            uint32_t hash; 
        };

        std::vector<Build> build_vec;
        std::vector<size_t> counts(num_buckets, 0);
        
        build_vec.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            uint32_t h = crc32(keys[i] ^ 0x9e3779b9u);
            build_vec.push_back({keys[i], payloads[i], h});

            uint32_t prefix = 0;
            if (prefix_bits == 0)
                prefix = 0;
            else 
                prefix = (h >> prefix_shift);

            counts[prefix]++;
        }

        std::vector<size_t> starts(num_buckets + 1, 0);
        for (size_t b = 0; b < num_buckets; ++b)
            starts[b + 1] = starts[b] + counts[b];
        
        assert(starts[num_buckets] == n);

        adjacency.resize(n);
        std::vector<size_t> pos = starts;
        std::vector<uint16_t> blooms(num_buckets, 0);

        for (const auto& be : build_vec) {
            uint32_t prefix = 0;
            if (prefix_bits == 0)
                prefix = 0;
            else
                prefix = (be.hash >> prefix_shift);

            uint32_t suffix;
            if (prefix_shift == 32 || prefix_shift == 0) {
                suffix = be.hash;
            } 
            else {
                uint32_t mask = (prefix_shift >= 32) ? 0xFFFFFFFFu : ((1u << prefix_shift) - 1u);
                suffix = be.hash & mask;
            }
            
            size_t idx = pos[prefix]++;
            adjacency[idx].key = be.key;
            adjacency[idx].payload = be.payload;
            blooms[prefix] |= compute_bloom(suffix);
        }

        directory.resize(num_buckets + 1);
        constexpr uintptr_t MASK = 0x0000FFFFFFFFFFFFULL;

        for (size_t b = 0; b < num_buckets; ++b) {
            uintptr_t ptr = reinterpret_cast<uintptr_t>(&adjacency[starts[b]]);
            directory[b] = ((ptr & MASK) << 16) | blooms[b];
        }
        uintptr_t end_ptr = reinterpret_cast<uintptr_t>(&adjacency[n]);
        directory[num_buckets] = ((end_ptr & MASK) << 16);
    }

    template<typename Callback>
    void probe(int32_t key, Callback&& callback) const {
        if (directory.empty() || num_buckets == 0) 
            return;

        uint32_t h = crc32(key ^ 0x9e3779b9u);
        int prefix_bits = ilog2_u64(num_buckets);

        uint32_t prefix = 0;
        if (num_buckets == 1) {
            prefix = 0;
        }
        else {
            prefix = (h >> prefix_shift);
            if (prefix >= num_buckets) 
                return;
        }

        uint64_t entry = directory[prefix];
        uint16_t bloom = entry & 0xFFFF;
        uint32_t suffix = (prefix_shift >= 32) ? h : (h & ((1u << prefix_shift) - 1));
        uint16_t bloom_mask = compute_bloom(suffix);
        
        if ((bloom & bloom_mask) != bloom_mask)
            return;

        uintptr_t start_ptr = entry >> 16;
        uintptr_t end_ptr = directory[prefix + 1] >> 16;

        const AdjacencyEntry* start = reinterpret_cast<const AdjacencyEntry*>(start_ptr);
        const AdjacencyEntry* end = reinterpret_cast<const AdjacencyEntry*>(end_ptr);

        for (const AdjacencyEntry* it = start; it < end; ++it) {
            if (it->key == key) {
                callback(it->payload);
            }
        }
    }
};

} // namespace Contest

#endif // PARALLEL_PROJECT_H