#include <catch2/catch_test_macros.hpp>

#include <table.h>
#include <plan.h>
#include <random>
#include <immintrin.h>

#include "unchained.h"

using namespace Contest;

TEST_CASE("Helper function tests", "[helpers]") {
    SECTION("crc32 consistency") {
        REQUIRE(crc32(0) == 0);
        REQUIRE(crc32(123456) == crc32(123456));
        REQUIRE(crc32(123456) != 0);
    }

    SECTION("next_power2") {
        REQUIRE(next_power2(0) == 1);
        REQUIRE(next_power2(1) == 1);
        REQUIRE(next_power2(2) == 2);
        REQUIRE(next_power2(3) == 4);
        REQUIRE(next_power2(15) == 16);
        REQUIRE(next_power2(16) == 16);
        REQUIRE(next_power2(17) == 32);
        REQUIRE(next_power2(1023) == 1024);
        REQUIRE(next_power2(1024) == 1024);
        REQUIRE(next_power2(1025) == 2048);
    }

    SECTION("ilog2_u64") {
        REQUIRE(ilog2_u64(1) == 0);
        REQUIRE(ilog2_u64(2) == 1);
        REQUIRE(ilog2_u64(3) == 1);
        REQUIRE(ilog2_u64(16) == 4);
        REQUIRE(ilog2_u64(31) == 4);
        REQUIRE(ilog2_u64(0xFFFFFFFFFFFFFFFFULL) == 63);
    }

    SECTION("compute_bloom") {
        uint16_t bloom = compute_bloom(0x1234);
        uint16_t expected = (1u << (0x4 & 0xF)) | (1u << (0x3 & 0xF)) | (1u << (0x2 & 0xF)) | (1u << (0x1 & 0xF));
        REQUIRE(bloom == expected);

        REQUIRE(compute_bloom(0) == 1);
        REQUIRE(compute_bloom(0xFFFF) != 0);
    }
}

TEST_CASE("UnchainedHashTable basic insert and probe", "[unchained]") {
    std::vector<int32_t> keys = {1, 2, 3, 42, 1000};
    std::vector<size_t> payloads = {10, 20, 30, 420, 10000};

    UnchainedHashTable table;
    table.build(keys, payloads);

    SECTION("All keys present") {
        for (size_t i = 0; i < keys.size(); ++i) {
            bool found = false;
            table.probe(keys[i], [&](size_t val) {
                found = true;
                REQUIRE(val == payloads[i]);
            });
            REQUIRE(found);
        }
    }

    SECTION("Non-existing key returns nothing") {
        bool called = false;
        table.probe(9999, [&](size_t) {
            called = true;
        });
        REQUIRE(!called);
    }
}

TEST_CASE("UnchainedHashTable edge cases", "[unchained]") {
    SECTION("Empty table") {
        UnchainedHashTable table;
        bool called = false;
        table.probe(42, [&](size_t) { called = true; });
        REQUIRE(!called);
    }

    SECTION("Single element") {
        UnchainedHashTable table;
        table.build({123}, {456});
        bool found = false;
        table.probe(123, [&](size_t val) {
            found = true;
            REQUIRE(val == 456);
        });
        REQUIRE(found);
    }

    SECTION("Colliding hashes") {
        UnchainedHashTable table;
        table.build({1, 1, 1}, {100, 200, 300});

        int count = 0;
        table.probe(1, [&](size_t val) { count++; });
        REQUIRE(count == 3);            // all entries are returned
    }
}

TEST_CASE("UnchainedHashTable random test", "[unchained][stress]") {
    constexpr int N = 100000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int32_t> dist(0, 1000000);

    std::vector<int32_t> keys;
    std::vector<size_t> payloads;

    for (int i = 0; i < N; ++i) {
        int32_t k = dist(rng);
        keys.push_back(k);
        payloads.push_back(k * 2);
    }

    UnchainedHashTable table;
    table.build(keys, payloads);

    for (int i = 0; i < N; ++i) {
        bool found = false;
        table.probe(keys[i], [&](size_t val) {
            found = true;
            REQUIRE(val == payloads[i]);
        });
        REQUIRE(found);
    }
}
