#include <catch2/catch_test_macros.hpp>

#include <table.h>
#include <plan.h>
#include <hardware.h>
#include <random>
#include <unordered_set>
#include <unordered_map>

#include "parallelProject.h"

using namespace Contest;

TEST_CASE("ExecutionContext", "[execution_context]") {
    SECTION("Constructor sets num_threads") {
        ExecutionContext ctx;
        
        REQUIRE(ctx.num_threads > 0);
        REQUIRE(ctx.num_threads <= 8);        //max 8
    }
    
    SECTION("num_threads is at least 1") {
        ExecutionContext ctx;
        
        REQUIRE(ctx.num_threads >= 1);    //default 4
    }
}

TEST_CASE("column_has_nulls helper", "[column_has_nulls]") {
    SECTION("Column without NULLs returns false") {
        Column col(DataType::INT32);
        Page* page = new Page();
        auto* page_data = page->data;
        
        *reinterpret_cast<uint16_t*>(page_data) = 5;      //num_rows
        *reinterpret_cast<uint16_t*>(page_data + 2) = 5;  //num_non_null
        
        col.pages.push_back(page);
        
        REQUIRE(column_has_nulls(col) == false);
    }
    
    SECTION("Column with NULLs returns true") {
        Column col(DataType::INT32);
        Page* page = new Page();
        auto* page_data = page->data;
        
        *reinterpret_cast<uint16_t*>(page_data) = 5;      //num_rows
        *reinterpret_cast<uint16_t*>(page_data + 2) = 3;  //num_non_null (has 2 nulls)
        
        col.pages.push_back(page);
        
        REQUIRE(column_has_nulls(col) == true);
    }
    
    SECTION("Empty column returns false") {
        Column col(DataType::INT32);
        
        REQUIRE(column_has_nulls(col) == false);
    }
    
    SECTION("Multiple pages - one with NULL") {
        Column col(DataType::INT32);
        
        Page* page1 = new Page();            // first page has no nulls
        auto* data1 = page1->data;
        *reinterpret_cast<uint16_t*>(data1) = 5;
        *reinterpret_cast<uint16_t*>(data1 + 2) = 5;
        col.pages.push_back(page1);
        
        Page* page2 = new Page();            //second page has nulls
        auto* data2 = page2->data;
        *reinterpret_cast<uint16_t*>(data2) = 5;
        *reinterpret_cast<uint16_t*>(data2 + 2) = 3;
        col.pages.push_back(page2);
        
        REQUIRE(column_has_nulls(col) == true);
    }
    
    SECTION("All NULL values") {
        Column col(DataType::INT32);
        Page* page = new Page();
        auto* page_data = page->data;
        
        *reinterpret_cast<uint16_t*>(page_data) = 5;      //num_rows
        *reinterpret_cast<uint16_t*>(page_data + 2) = 0;  //num_non_null (all null)
        
        col.pages.push_back(page);
        
        REQUIRE(column_has_nulls(col) == true);
    }
}

TEST_CASE("ChunkNode - Construction and destruction", "[chunk_node]") {
    SECTION("Create with size allocates memory") {
        ChunkNode* node = new ChunkNode(1024);
        
        REQUIRE(node->size == 1024);
        REQUIRE(node->used == 0);
        REQUIRE(node->next == nullptr);
        REQUIRE(node->owns_memory == true);
        REQUIRE(node->data != nullptr);
        
        delete node;
    }
    
    SECTION("Create with zero size") {
        ChunkNode* node = new ChunkNode(0);
        
        REQUIRE(node->size == 0);
        REQUIRE(node->used == 0);
        REQUIRE(node->owns_memory == false);
        REQUIRE(node->data == nullptr);
        
        delete node;
    }
    
    SECTION("Write and read from chunk") {
        ChunkNode* node = new ChunkNode(100);
        
        for (size_t i = 0; i < 100; ++i) {      //write some data
            node->data[i] = std::byte(i % 256);
        }
        
        for (size_t i = 0; i < 100; ++i) {      //read the data
            REQUIRE(node->data[i] == std::byte(i % 256));
        }
        
        delete node;
    }
    
    SECTION("Linked list of chunks") {
        ChunkNode* first = new ChunkNode(512);
        ChunkNode* second = new ChunkNode(1024);
        ChunkNode* third = new ChunkNode(2048);
        
        first->next = second;
        second->next = third;
        
        REQUIRE(first->next == second);
        REQUIRE(second->next == third);
        REQUIRE(third->next == nullptr);
        
        delete third;
        delete second;
        delete first;
    }
}

TEST_CASE("BumpAllocPartition - Level 3 allocator", "[allocator][level3]") {
    SECTION("Constructor initializes empty") {
        BumpAllocPartition alloc;
        
        REQUIRE(alloc.head == nullptr);
        REQUIRE(alloc.current == nullptr);
        REQUIRE(alloc.free_space() == 0);
    }
    
    SECTION("add_space with owned memory") {
        BumpAllocPartition alloc;
        
        std::byte* buffer = new std::byte[1024];
        alloc.add_space(buffer, 1024);
        
        REQUIRE(alloc.head != nullptr);
        REQUIRE(alloc.current != nullptr);
        REQUIRE(alloc.head == alloc.current);
        REQUIRE(alloc.free_space() == 1024);
        REQUIRE(alloc.current->owns_memory == false);
        
        delete[] buffer;
    }
    
    SECTION("allocate single entry") {
        BumpAllocPartition alloc;
        
        std::byte* buffer = new std::byte[1024];
        alloc.add_space(buffer, 1024);
        
        AdjacencyEntry* entry = alloc.allocate();
        
        REQUIRE(entry != nullptr);
        REQUIRE(alloc.free_space() == 1024 - sizeof(AdjacencyEntry));
        
        entry->key = 42;        //write to entry
        entry->payload = 100;
        
        REQUIRE(entry->key == 42);
        REQUIRE(entry->payload == 100);
        
        delete[] buffer;
    }
    
    SECTION("allocate multiple entries") {
        BumpAllocPartition alloc;
        
        std::byte* buffer = new std::byte[1024];
        alloc.add_space(buffer, 1024);
        
        std::vector<AdjacencyEntry*> entries;
        for (int i = 0; i < 10; ++i) {
            AdjacencyEntry* e = alloc.allocate();
            if (e) {
                e->key = i;
                e->payload = i * 10;
                entries.push_back(e);
            }
        }
        
        REQUIRE(entries.size() == 10);
        
        for (size_t i = 0; i < entries.size(); ++i) {       //check that all entries are correct
            REQUIRE(entries[i]->key == (int32_t)i);
            REQUIRE(entries[i]->payload == i * 10);
        }
        
        delete[] buffer;
    }
    
    SECTION("allocate until full") {
        BumpAllocPartition alloc;
        
        size_t buffer_size = 512;
        std::byte* buffer = new std::byte[buffer_size];
        alloc.add_space(buffer, buffer_size);
        
        int count = 0;
        while (alloc.free_space() >= sizeof(AdjacencyEntry)) {
            AdjacencyEntry* e = alloc.allocate();
            if (e) {
                count++;
            } else {
                break;
            }
        }
        
        REQUIRE(count > 0);
        REQUIRE(alloc.free_space() < sizeof(AdjacencyEntry));
        
        AdjacencyEntry* e = alloc.allocate();       //next allocation is out of range
        REQUIRE(e == nullptr);
        
        delete[] buffer;
    }
    
    SECTION("Multiple add_space calls") {
        BumpAllocPartition alloc;
        
        std::byte* buf1 = new std::byte[512];
        std::byte* buf2 = new std::byte[512];
        
        alloc.add_space(buf1, 512);
        alloc.add_space(buf2, 512);
        
        REQUIRE(alloc.head != nullptr);     //check that there are two chunks in the list
        REQUIRE(alloc.head->next != nullptr);
        REQUIRE(alloc.current == alloc.head->next);
        
        delete[] buf1;
        delete[] buf2;
    }
}

TEST_CASE("BumpAllocThread - Level 2 allocator", "[allocator][level2]") {
    SECTION("Constructor initializes empty") {
        BumpAllocThread alloc;
        
        REQUIRE(alloc.head == nullptr);
        REQUIRE(alloc.current == nullptr);
        REQUIRE(alloc.free_space() == 0);
    }
    
    SECTION("add_space with owned chunk") {
        BumpAllocThread alloc;
        
        std::byte* chunk = new std::byte[LARGE_CHUNK_SIZE];
        alloc.add_space(chunk, LARGE_CHUNK_SIZE);
        
        REQUIRE(alloc.head != nullptr);
        REQUIRE(alloc.current != nullptr);
        REQUIRE(alloc.free_space() == LARGE_CHUNK_SIZE);
        REQUIRE(alloc.current->owns_memory == true);
    }
    
    SECTION("allocate small chunks") {
        BumpAllocThread alloc;
        
        std::byte* chunk = new std::byte[LARGE_CHUNK_SIZE];
        alloc.add_space(chunk, LARGE_CHUNK_SIZE);
        
        std::byte* small1 = alloc.allocate(SMALL_CHUNK_SIZE);
        std::byte* small2 = alloc.allocate(SMALL_CHUNK_SIZE);
        
        REQUIRE(small1 != nullptr);
        REQUIRE(small2 != nullptr);
        REQUIRE(small1 != small2);
        REQUIRE(alloc.free_space() == LARGE_CHUNK_SIZE - 2 * SMALL_CHUNK_SIZE);
    }
    
    SECTION("allocate until full") {
        BumpAllocThread alloc;
        
        std::byte* chunk = new std::byte[LARGE_CHUNK_SIZE];
        alloc.add_space(chunk, LARGE_CHUNK_SIZE);
        
        int count = 0;
        while (alloc.free_space() >= SMALL_CHUNK_SIZE) {
            std::byte* small = alloc.allocate(SMALL_CHUNK_SIZE);
            if (small) {
                count++;
            } else {
                break;
            }
        }
        
        REQUIRE(count > 0);
        REQUIRE(alloc.free_space() < SMALL_CHUNK_SIZE);
    }
    
    SECTION("allocate larger than available fails") {
        BumpAllocThread alloc;
        
        std::byte* chunk = new std::byte[1024];
        alloc.add_space(chunk, 1024);
        
        std::byte* result = alloc.allocate(2048);
        REQUIRE(result == nullptr);
    }
    
    SECTION("Multiple add_space calls create chain") {
        BumpAllocThread alloc;
        
        std::byte* chunk1 = new std::byte[LARGE_CHUNK_SIZE];
        std::byte* chunk2 = new std::byte[LARGE_CHUNK_SIZE];
        
        alloc.add_space(chunk1, LARGE_CHUNK_SIZE);
        alloc.add_space(chunk2, LARGE_CHUNK_SIZE);
        
        REQUIRE(alloc.head != nullptr);
        REQUIRE(alloc.head->next != nullptr);
    }
}

TEST_CASE("GlobalAllocator - Level 1 allocator", "[allocator][level1]") {
    SECTION("allocate_large_chunk returns valid memory") {
        GlobalAllocator alloc;
        
        std::byte* chunk = alloc.allocate_large_chunk();
        
        REQUIRE(chunk != nullptr);
        
        for (size_t i = 0; i < 100; ++i) {      //write to check its usable
            chunk[i] = std::byte(i % 256);
        }
        
        for (size_t i = 0; i < 100; ++i) {      //read
            REQUIRE(chunk[i] == std::byte(i % 256));
        }
        
        alloc.deallocate(chunk);
    }
    
    SECTION("Multiple allocations return different pointers") {
        GlobalAllocator alloc;
        
        std::vector<std::byte*> chunks;
        for (int i = 0; i < 10; ++i) {
            chunks.push_back(alloc.allocate_large_chunk());
        }
        
        // all chunks are non null and unique
        std::unordered_set<std::byte*> unique_chunks(chunks.begin(), chunks.end());
        REQUIRE(unique_chunks.size() == chunks.size());
        
        for (auto* chunk : chunks) {
            REQUIRE(chunk != nullptr);
            alloc.deallocate(chunk);
        }
    }
    
    SECTION("Allocate and deallocate many chunks") {
        GlobalAllocator alloc;
        
        for (int i = 0; i < 100; ++i) {
            std::byte* chunk = alloc.allocate_large_chunk();
            REQUIRE(chunk != nullptr);
            alloc.deallocate(chunk);
        }
    }
}

TEST_CASE("ThreadLocalCollector - Full integration", "[allocator][collector]") {
    SECTION("Constructor initializes correctly") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 4);
        
        REQUIRE(collector.num_partitions == 4);
        REQUIRE(collector.level3.size() == 4);
        REQUIRE(collector.counts.size() == 4);
        
        for (size_t count : collector.counts) {
            REQUIRE(count == 0);
        }
    }
    
    SECTION("consume single entry single partition") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 1);
        
        uint32_t hash = crc32(42);
        collector.consume(42, 100, hash);
        
        REQUIRE(collector.counts[0] == 1);
    }
    
    SECTION("consume multiple entries") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 1);
        
        for (int i = 0; i < 100; ++i) {
            uint32_t hash = crc32(i);
            collector.consume(i, i * 10, hash);
        }
        
        REQUIRE(collector.counts[0] == 100);
    }
    
    SECTION("consume distributes across partitions") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 4);
        
        for (int i = 0; i < 1000; ++i) {
            uint32_t hash = crc32(i);
            collector.consume(i, i * 10, hash);
        }
        
        size_t total = 0;
        for (size_t count : collector.counts) {
            total += count;
        }
        
        REQUIRE(total == 1000);
        
        for (size_t count : collector.counts) {     //each partiton should have entries
            REQUIRE(count > 0);
        }
    }
    
    SECTION("consume with many entries forces allocation") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 2);
        
        for (int i = 0; i < 10000; ++i) {       //many entries to force multiple allocations
            uint32_t hash = crc32(i);
            collector.consume(i, i, hash);
        }
        
        size_t total = 0;
        for (size_t count : collector.counts) {
            total += count;
        }
        
        REQUIRE(total == 10000);
    }
    
    SECTION("Different partition counts") {
        GlobalAllocator global;
        
        for (size_t num_parts : {1, 2, 4, 8, 16}) {
            ThreadLocalCollector collector(global, num_parts);
            
            for (int i = 0; i < 1000; ++i) {
                uint32_t hash = crc32(i);
                collector.consume(i, i, hash);
            }
            
            size_t total = 0;
            for (size_t count : collector.counts) {
                total += count;
            }
            
            REQUIRE(total == 1000);
        }
    }
}

TEST_CASE("3-Level hierarchy integration", "[allocator][integration]") {
    SECTION("Complete 3-level allocation flow") {
        GlobalAllocator global;
        BumpAllocThread level2;
        BumpAllocPartition level3;
        
        std::byte* large = global.allocate_large_chunk();       //level1 global allocates large chunck
        REQUIRE(large != nullptr);
        
        level2.add_space(large, LARGE_CHUNK_SIZE);          //level2 tread allocator manages the large chunck
        REQUIRE(level2.free_space() == LARGE_CHUNK_SIZE);
        
        std::byte* small = level2.allocate(SMALL_CHUNK_SIZE);   //allocates small chuck for partition
        REQUIRE(small != nullptr);
        REQUIRE(level2.free_space() == LARGE_CHUNK_SIZE - SMALL_CHUNK_SIZE);
        
        level3.add_space(small, SMALL_CHUNK_SIZE);      //level3 partition allocates individual entries
        REQUIRE(level3.free_space() == SMALL_CHUNK_SIZE);
        
        AdjacencyEntry* e1 = level3.allocate();     //allocate entries
        AdjacencyEntry* e2 = level3.allocate();
        
        REQUIRE(e1 != nullptr);
        REQUIRE(e2 != nullptr);
        REQUIRE(e1 != e2);
        
        e1->key = 100;
        e1->payload = 200;
        e2->key = 300;
        e2->payload = 400;
        
        REQUIRE(e1->key == 100);
        REQUIRE(e2->key == 300);
    }
    
    SECTION("Multiple partitions sharing thread allocator") {
        GlobalAllocator global;
        BumpAllocThread level2;
        
        std::byte* large = global.allocate_large_chunk();
        level2.add_space(large, LARGE_CHUNK_SIZE);
        
        std::vector<BumpAllocPartition> partitions(4);
        
        for (auto& partition : partitions) {        //each partition gets a small chunk
            std::byte* small = level2.allocate(SMALL_CHUNK_SIZE);
            REQUIRE(small != nullptr);
            partition.add_space(small, SMALL_CHUNK_SIZE);
        }
        
        for (size_t i = 0; i < partitions.size(); ++i) {    //each partition allocates entries
            AdjacencyEntry* e = partitions[i].allocate();
            REQUIRE(e != nullptr);
            e->key = i;
            e->payload = i * 10;
        }
    }
}

TEST_CASE("UnchainedHashTable - Parallel build", "[hash_table][parallel]") {
    SECTION("build_parallel with empty input") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys;
        std::vector<size_t> payloads;
        
        table.build_parallel(keys, payloads, 4);
        
        int count = 0;
        table.probe(1, [&](size_t payload) {
            count++;
        });
        
        REQUIRE(count == 0);
    }
    
    SECTION("build_parallel with single element") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys = {42};
        std::vector<size_t> payloads = {100};
        
        table.build_parallel(keys, payloads, 2);
        
        int count = 0;
        size_t found = 0;
        table.probe(42, [&](size_t payload) {
            count++;
            found = payload;
        });
        
        REQUIRE(count == 1);
        REQUIRE(found == 100);
    }
    
    SECTION("build_parallel with unique keys") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys;
        std::vector<size_t> payloads;
        
        for (int i = 0; i < 1000; ++i) {
            keys.push_back(i);
            payloads.push_back(i * 2);
        }
        
        table.build_parallel(keys, payloads, 4);
        
        for (int i = 0; i < 1000; ++i) {
            int count = 0;
            size_t found = 0;
            table.probe(i, [&](size_t payload) {
                count++;
                found = payload;
            });
            REQUIRE(count == 1);
            REQUIRE(found == i * 2);
        }
    }
    
    SECTION("build_parallel with duplicate keys") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys = {1, 2, 1, 3, 2, 1};
        std::vector<size_t> payloads = {10, 20, 11, 30, 21, 12};
        
        table.build_parallel(keys, payloads, 2);
        
        std::vector<size_t> found1;
        table.probe(1, [&](size_t payload) {
            found1.push_back(payload);
        });
        std::sort(found1.begin(), found1.end());
        REQUIRE(found1 == std::vector<size_t>{10, 11, 12});
    }
    
    SECTION("build_parallel with different thread counts") {
        std::vector<int32_t> keys;
        std::vector<size_t> payloads;
        
        for (int i = 0; i < 5000; ++i) {
            keys.push_back(i % 100);
            payloads.push_back(i);
        }
        
        for (size_t num_threads : {1, 2, 4, 8}) {
            UnchainedHashTable table;
            table.build_parallel(keys, payloads, num_threads);
            
            for (int key = 0; key < 100; ++key) {       //check all keys can be found
                int count = 0;
                table.probe(key, [&](size_t payload) {
                    count++;
                });
                REQUIRE(count == 50);           //each key appears 50 times
            }
        }
    }
    
    SECTION("build_parallel vs build consistency") {
        std::vector<int32_t> keys;
        std::vector<size_t> payloads;
        
        std::mt19937 rng(42);
        std::uniform_int_distribution<int32_t> dist(0, 999);
        
        for (int i = 0; i < 10000; ++i) {
            keys.push_back(dist(rng));
            payloads.push_back(i);
        }
        
        UnchainedHashTable table_seq;
        table_seq.build(keys, payloads);
        
        UnchainedHashTable table_par;
        table_par.build_parallel(keys, payloads, 4);
        
        for (int key = 0; key < 1000; ++key) {      //both should find the same keys
            std::vector<size_t> found_seq;
            table_seq.probe(key, [&](size_t payload) {
                found_seq.push_back(payload);
            });
            
            std::vector<size_t> found_par;
            table_par.probe(key, [&](size_t payload) {
                found_par.push_back(payload);
            });
            
            std::sort(found_seq.begin(), found_seq.end());
            std::sort(found_par.begin(), found_par.end());
            
            REQUIRE(found_seq == found_par);
        }
    }
    
    SECTION("Large dataset parallel build") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys;
        std::vector<size_t> payloads;
        
        for (int i = 0; i < 100000; ++i) {
            keys.push_back(i);
            payloads.push_back(i);
        }
        
        table.build_parallel(keys, payloads, 8);
        
        for (int i = 0; i < 100000; i += 1000) {
            int count = 0;
            size_t found = 0;
            table.probe(i, [&](size_t payload) {
                count++;
                found = payload;
            });
            REQUIRE(count == 1);
            REQUIRE(found == i);
        }
    }
}
TEST_CASE("Edge cases - ChunkNode", "[edge_cases][chunk_node]") {
    SECTION("Very small chunk") {           //1 byte
        ChunkNode* node = new ChunkNode(1);
        REQUIRE(node->size == 1);
        REQUIRE(node->data != nullptr);
        node->data[0] = std::byte(255);
        REQUIRE(node->data[0] == std::byte(255));
        delete node;
    }
    
    SECTION("Very large chunk") {
        ChunkNode* node = new ChunkNode(1024 * 1024);   //1 mb
        REQUIRE(node->size == 1024 * 1024);
        REQUIRE(node->data != nullptr);
        delete node;
    }
    
    SECTION("Circular reference detection") {
        ChunkNode* node1 = new ChunkNode(100);
        ChunkNode* node2 = new ChunkNode(100);
        
        node1->next = node2;
        
        delete node2;
        delete node1;
    }
}

TEST_CASE("Edge cases - BumpAllocPartition", "[edge_cases][allocator]") {
    SECTION("Allocate with exactly enough space") {
        BumpAllocPartition alloc;
        
        size_t exact_size = sizeof(AdjacencyEntry);
        std::byte* buffer = new std::byte[exact_size];
        alloc.add_space(buffer, exact_size);
        
        AdjacencyEntry* e = alloc.allocate();
        REQUIRE(e != nullptr);
        REQUIRE(alloc.free_space() == 0);
        
        AdjacencyEntry* e2 = alloc.allocate();      //next allocation should fail because it would be out of range
        REQUIRE(e2 == nullptr);
        
        delete[] buffer;
    }
    
    SECTION("Allocate with one byte too few") {
        BumpAllocPartition alloc;
        
        size_t small_size = sizeof(AdjacencyEntry) - 1;
        std::byte* buffer = new std::byte[small_size];
        alloc.add_space(buffer, small_size);
        
        AdjacencyEntry* e = alloc.allocate();
        REQUIRE(e == nullptr);
        
        delete[] buffer;
    }
    
    SECTION("Empty partition allocate fails") {
        BumpAllocPartition alloc;
        
        AdjacencyEntry* e = alloc.allocate();
        REQUIRE(e == nullptr);
    }
}

TEST_CASE("Edge cases - BumpAllocThread", "[edge_cases][allocator]") {
    SECTION("Allocate with exactly enough space") {
        BumpAllocThread alloc;
        
        std::byte* chunk = new std::byte[SMALL_CHUNK_SIZE];
        alloc.add_space(chunk, SMALL_CHUNK_SIZE);
        
        std::byte* small = alloc.allocate(SMALL_CHUNK_SIZE);
        REQUIRE(small != nullptr);
        REQUIRE(alloc.free_space() == 0);
        
        std::byte* small2 = alloc.allocate(1);           //next allocation should fail because it would be out of ranfe
        REQUIRE(small2 == nullptr);
    }
    
    SECTION("Allocate zero bytes") {
        BumpAllocThread alloc;
        
        std::byte* chunk = new std::byte[1024];
        alloc.add_space(chunk, 1024);
        
        std::byte* result = alloc.allocate(0);
        REQUIRE(result != nullptr);   //check that 0 bytes NOT equal to nullptr (the implementation returns valid pointer even for 0 bytes)
    }
    
    SECTION("Empty thread allocator") {
        BumpAllocThread alloc;
        
        std::byte* result = alloc.allocate(SMALL_CHUNK_SIZE);
        REQUIRE(result == nullptr);
    }
}

TEST_CASE("Edge cases - ThreadLocalCollector", "[edge_cases][collector]") {
    SECTION("Single partition collector") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 1);
        
        for (int i = 0; i < 100; ++i) {
            uint32_t hash = crc32(i);
            collector.consume(i, i, hash);
        }
        
        REQUIRE(collector.counts[0] == 100);
    }
    
    SECTION("Many partitions") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 256);
        
        for (int i = 0; i < 1000; ++i) {
            uint32_t hash = crc32(i);
            collector.consume(i, i, hash);
        }
        
        size_t total = 0;
        for (size_t count : collector.counts) {
            total += count;
        }
        REQUIRE(total == 1000);
    }
    
    SECTION("Consume with hash = 0") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 4);
        
        collector.consume(0, 0, 0);
        
        size_t total = 0;
        for (size_t count : collector.counts) {
            total += count;
        }
        REQUIRE(total == 1);
    }
    
    SECTION("Consume negative keys") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 4);
        
        for (int i = -100; i < 0; ++i) {
            uint32_t hash = crc32(i);
            collector.consume(i, i, hash);
        }
        
        size_t total = 0;
        for (size_t count : collector.counts) {
            total += count;
        }
        REQUIRE(total == 100);
    }
    
    SECTION("Consume INT32_MIN and INT32_MAX") {
        GlobalAllocator global;
        ThreadLocalCollector collector(global, 4);
        
        uint32_t hash_min = crc32(INT32_MIN);
        uint32_t hash_max = crc32(INT32_MAX);
        
        collector.consume(INT32_MIN, 1, hash_min);
        collector.consume(INT32_MAX, 2, hash_max);
        
        size_t total = 0;
        for (size_t count : collector.counts) {
            total += count;
        }
        REQUIRE(total == 2);
    }
}

TEST_CASE("Edge cases - UnchainedHashTable parallel", "[edge_cases][hash_table]") {
    SECTION("Single thread parallel build") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys = {1, 2, 3};
        std::vector<size_t> payloads = {10, 20, 30};
        
        table.build_parallel(keys, payloads, 1);
        
        for (size_t i = 0; i < keys.size(); ++i) {
            size_t found = 0;
            int count = 0;
            table.probe(keys[i], [&](size_t payload) {
                found = payload;
                count++;
            });
            REQUIRE(count == 1);
            REQUIRE(found == payloads[i]);
        }
    }
    
    SECTION("More threads than elements") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys = {1, 2};
        std::vector<size_t> payloads = {10, 20};
        
        table.build_parallel(keys, payloads, 10);
        
        for (size_t i = 0; i < keys.size(); ++i) {
            size_t found = 0;
            int count = 0;
            table.probe(keys[i], [&](size_t payload) {
                found = payload;
                count++;
            });
            REQUIRE(count == 1);
            REQUIRE(found == payloads[i]);
        }
    }
    
    SECTION("Zero threads uses 1") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys = {1, 2, 3};
        std::vector<size_t> payloads = {10, 20, 30};
        
        table.build_parallel(keys, payloads, 0);
        
        for (size_t i = 0; i < keys.size(); ++i) {
            int count = 0;
            table.probe(keys[i], [&](size_t payload) {
                count++;
            });
            REQUIRE(count == 1);
        }
    }
    
    SECTION("All duplicate keys parallel") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys(10000, 42);
        std::vector<size_t> payloads;
        for (size_t i = 0; i < 10000; ++i) {
            payloads.push_back(i);
        }
        
        table.build_parallel(keys, payloads, 4);
        
        int count = 0;
        table.probe(42, [&](size_t payload) {
            count++;
        });
        REQUIRE(count == 10000);
    }
    
    SECTION("Negative keys parallel") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys = {-1, -2, -3, -100, -1000};
        std::vector<size_t> payloads = {1, 2, 3, 4, 5};
        
        table.build_parallel(keys, payloads, 2);
        
        for (size_t i = 0; i < keys.size(); ++i) {
            int count = 0;
            size_t found = 0;
            table.probe(keys[i], [&](size_t payload) {
                count++;
                found = payload;
            });
            REQUIRE(count == 1);
            REQUIRE(found == payloads[i]);
        }
    }
    
    SECTION("Probe non-existent key after parallel build") {
        UnchainedHashTable table;
        
        std::vector<int32_t> keys = {1, 2, 3};
        std::vector<size_t> payloads = {10, 20, 30};
        
        table.build_parallel(keys, payloads, 2);
        
        int count = 0;
        table.probe(999, [&](size_t payload) {
            count++;
        });
        REQUIRE(count == 0);
    }
}

TEST_CASE("Memory stress tests", "[allocator][stress]") {
    SECTION("Repeated allocation and deallocation cycles") {
        for (int cycle = 0; cycle < 100; ++cycle) {
            GlobalAllocator global;
            ThreadLocalCollector collector(global, 4);
            
            for (int i = 0; i < 1000; ++i) {
                uint32_t hash = crc32(i);
                collector.consume(i, i, hash);
            }
            
            size_t total = 0;
            for (size_t count : collector.counts) {
                total += count;
            }
            REQUIRE(total == 1000);
        }
    }
    
    SECTION("Many collectors with same global allocator") {
        GlobalAllocator global;
        
        std::vector<std::unique_ptr<ThreadLocalCollector>> collectors;
        for (int i = 0; i < 10; ++i) {
            collectors.push_back(std::make_unique<ThreadLocalCollector>(global, 4));
        }
        
        for (auto& collector : collectors) {
            for (int i = 0; i < 100; ++i) {
                uint32_t hash = crc32(i);
                collector->consume(i, i, hash);
            }
        }
        
        for (auto& collector : collectors) {        //each collector has correct counts
            size_t total = 0;
            for (size_t count : collector->counts) {
                total += count;
            }
            REQUIRE(total == 100);
        }
    }
}
