#include <catch2/catch_test_macros.hpp>

#include <table.h>
#include <plan.h>
#include <random>

#include "hopscotch.h"

TEST_CASE("Unique insertions", "[unique]") {
    constexpr size_t inserts = 1000000;
    myHashTable<int, std::string> table;

    for (int i = 0; i < inserts; ++i) {
        table.emplace(i, std::to_string(i));    //we insert 1 million keys with their string representation as value
    }

    for (int i = 0; i < inserts; ++i) {         //check that all keys exist with correct values
        auto entry = table.find(i);
        REQUIRE(entry != table.end());
        REQUIRE(entry->second == std::to_string(i));
    }

    for (int i = inserts; i < inserts + 100; ++i) { //check that non inserted keys do not exist
        auto entry = table.find(i);
        REQUIRE(entry == table.end());
    }
}

TEST_CASE("String keys", "[strings]") {
    myHashTable<std::string, int> table;

    auto [entry1, ok1] = table.emplace("key1", 1);        //we insert some string keys
    auto [entry2, ok2] = table.emplace("key2", 2);
    auto [entry3, ok3] = table.emplace("key3", 3);

    REQUIRE(ok1 == true);
    REQUIRE(ok2 == true);
    REQUIRE(ok3 == true);

    auto f1 = table.find("key1");       //check that they can be found
    auto f2 = table.find("key2");
    auto f3 = table.find("key3");

    REQUIRE(f1 != table.end());
    REQUIRE(f2 != table.end());
    REQUIRE(f3 != table.end());

    REQUIRE(f1->second == 1);
    REQUIRE(f2->second == 2);
    REQUIRE(f3->second == 3);

    auto f4 = table.find("key4");       //check that non-existing key is not found 
    REQUIRE(f4 == table.end());
}

TEST_CASE("Duplicate insertions", "[duplicates]") {
    constexpr size_t inserts = 1000000;  
    myHashTable<int, std::string> table;

    for (int i = 0; i < inserts; ++i) {     //we insert the same key with the same value 1 million times
        table.emplace(12345, "value");
    }

    auto entry = table.find(12345);
    REQUIRE(entry != table.end());
    REQUIRE(entry->second == "value");

    auto not_found = table.find(123);     //check that no other key exists
    REQUIRE(not_found == table.end());
}

TEST_CASE("Duplicate key does not overwrite value", "[duplicates]") {
    myHashTable<int, std::string> table;

    auto [entry1, inserted1] = table.emplace(10, "value");
    REQUIRE(inserted1 == true);

    auto [entry2, inserted2] = table.emplace(10, "value2");         //we try to insert the same key with a different value
    REQUIRE(inserted2 == false);
    REQUIRE(entry2->second == "value");                             //check that the value remains the original one
}

TEST_CASE("Random insert and search", "[random]") {
    myHashTable<int, int> table;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 1000000);

    std::vector<int> keys;

    for (int i = 0; i < 200000; ++i) {          //we insert 200000 random keys
        int k = dist(rng);
        table.emplace(k, k * 2);
        keys.push_back(k);
    }

    for (int k : keys) {                 //check that all inserted keys exist with correct values
        auto entry = table.find(k);
        REQUIRE(entry != table.end());
        REQUIRE(entry->second == k * 2);
    }
}

TEST_CASE("Mixed insert and find", "[mixed]") {
    myHashTable<int, int> table;
    constexpr int inserts = 1000000;

    for (int i = 0; i < inserts; ++i) {         //we mix inserts and finds
        table.emplace(i, i + 1);

        if (i % 5 == 0) {                       //every 5 inserts we do a search
            auto entry = table.find(i);
            REQUIRE(entry != table.end());
            REQUIRE(entry->second == i + 1);
        }
    }

    for (int i = 0; i < inserts; ++i) {     //check that all inserted keys exist with correct values
        auto entry = table.find(i);
        REQUIRE(entry != table.end());
        REQUIRE(entry->second == i + 1);
    }

    for (int i = inserts; i < inserts + 100; ++i) {     //check that non inserted keys do not exist
        auto entry = table.find(i);
        REQUIRE(entry == table.end());
    }
}

TEST_CASE("Find on empty table", "[empty]") {
    myHashTable<int, int> table;
    REQUIRE(table.find(10) == table.end());
}

TEST_CASE("Rehash", "[rehash]") {
    myHashTable<int, int> table(8);

    int n = 100;
    for (int i = 0; i < n; ++i) {
        table.emplace(i, i + 10);
    }

    for (int i = 0; i < n; ++i) {       //check that all inserted keys exist with correct values after rehash
        auto entry = table.find(i);
        REQUIRE(entry != table.end());
        REQUIRE(entry->second == i + 10);
    }
}

TEST_CASE("Multiple rehashes with string keys", "[rehashes]") {
    myHashTable<std::string, int> table(2);

    constexpr int N = 1000000;          //we insert 1 million string keys to force multiple rehashes

    for (int i = 0; i < N; ++i) {
        table.emplace("key" + std::to_string(i), i * 2);
    }

    for (int i = 0; i < N; ++i) {       //check that all inserted keys exist with correct values after multiple rehashes
        auto entry = table.find("key" + std::to_string(i));
        REQUIRE(entry != table.end());
        REQUIRE(entry->second == i * 2);
    }

    for (int i = N; i < N + 1000; ++i) {    //check that non inserted keys do not exist
        auto entry = table.find("key" + std::to_string(i));
        REQUIRE(entry == table.end());
    }
}

TEST_CASE("Hopscotch neighborhood", "[neighborhood]") {
    myHashTable<int, int> table(7);
    std::vector<int> keys = {1, 8, 15, 22, 29}; //all these keys hash to index 1 in a table of size 7

    for (int i = 0; i < keys.size(); ++i) {     //insert keys that hash to the same index
        auto [entry, inserted] = table.emplace(keys[i], i);
        REQUIRE(inserted == true);
    }
    
    for (int i = 0; i < keys.size(); ++i) {    //check that all inserted keys exist with correct values
        auto entry = table.find(keys[i]);
        REQUIRE(entry != table.end());
        REQUIRE(entry->second == i);
    }
}

TEST_CASE("Hopscotch bitmap", "[bitmap]") {
    myHashTable<int, int> table(7);

    std::size_t capacity = table.get_capacity();

    int home_key = 100;
    std::size_t home_index = std::hash<int>{}(home_key) % capacity;

    for (int i = 0; i < 10; ++i) {          //insert keys that all hash to the same bucket
        int k = home_key + i * capacity;
        table.emplace(k, i);
    }

    for (int i = 0; i < 10; ++i) {          //check that all inserted keys exist with correct values
        int k = home_key + i * capacity;
        auto entry = table.find(k);

        REQUIRE(entry != table.end());
        REQUIRE(entry->second == i);
    }
}