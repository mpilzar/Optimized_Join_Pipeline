#include <catch2/catch_test_macros.hpp>

#include <table.h>
#include <plan.h>
#include <hardware.h>
#include <random>

#include "part1functions.h"
#include "part2functions.h"

using namespace Contest;

TEST_CASE("column_t construction and destruction", "[column_t]") {
    SECTION("Constructor initializes correctly") {
        column_t col(DataType::INT32);
        
        REQUIRE(col.type == DataType::INT32);
        REQUIRE(col.num_rows == 0);
        REQUIRE(col.pages.size() == 0);
    }
    
    SECTION("VARCHAR column type") {
        column_t col(DataType::VARCHAR);
        
        REQUIRE(col.type == DataType::VARCHAR);
        REQUIRE(col.num_rows == 0);
        REQUIRE(col.pages.size() == 0);
    }
}

TEST_CASE("column_t move semantics", "[column_t]") {
    SECTION("Move constructor") {
        column_t col1(DataType::INT32);
        col1.pages.push_back(new Page());
        col1.num_rows = 100;
        
        column_t col2(std::move(col1));
        
        REQUIRE(col2.type == DataType::INT32);
        REQUIRE(col2.num_rows == 100);
        REQUIRE(col2.pages.size() == 1);
        REQUIRE(col1.pages.size() == 0);
        REQUIRE(col1.num_rows == 0);
    }
    
    SECTION("Move assignment") {
        column_t col1(DataType::INT32);
        col1.pages.push_back(new Page());
        col1.num_rows = 100;
        
        column_t col2(DataType::VARCHAR);
        col2 = std::move(col1);
        
        REQUIRE(col2.type == DataType::INT32);
        REQUIRE(col2.num_rows == 100);
        REQUIRE(col2.pages.size() == 1);
        REQUIRE(col1.pages.size() == 0);
        REQUIRE(col1.num_rows == 0);
    }
}

TEST_CASE("ColumnWriter INT32 - Basic operations", "[column_writer_int32]") {
    SECTION("Write single value") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        writer.write_int32(42, false);
        writer.finalize();
        
        REQUIRE(col.num_rows == 1);
        REQUIRE(col.pages.size() == 1);
        
        auto* page_data = col.pages[0]->data;
        uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        
        REQUIRE(num_rows == 1);
        REQUIRE(num_non_null == 1);
        
        auto* data = reinterpret_cast<int32_t*>(page_data + 4);
        REQUIRE(data[0] == 42);
    }
    
    SECTION("Write multiple values") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        writer.write_int32(10, false);
        writer.write_int32(20, false);
        writer.write_int32(30, false);
        writer.finalize();
        
        REQUIRE(col.num_rows == 3);
        REQUIRE(col.pages.size() == 1);
        
        auto* page_data = col.pages[0]->data;
        auto* data = reinterpret_cast<int32_t*>(page_data + 4);
        REQUIRE(data[0] == 10);
        REQUIRE(data[1] == 20);
        REQUIRE(data[2] == 30);
    }
    
    SECTION("Write NULL values") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        writer.write_int32(10, false);
        writer.write_int32(0, true);
        writer.write_int32(30, false);
        writer.finalize();
        
        REQUIRE(col.num_rows == 3);
        
        auto* page_data = col.pages[0]->data;
        uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        
        REQUIRE(num_rows == 3);
        REQUIRE(num_non_null == 2);
        
        size_t bitmap_size = (num_rows + 7) / 8;
        auto* bitmap = reinterpret_cast<uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
        REQUIRE(get_bitmap(bitmap, 0) == true);
        REQUIRE(get_bitmap(bitmap, 1) == false);
        REQUIRE(get_bitmap(bitmap, 2) == true);
    }
    
    SECTION("Write all NULL values") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        writer.write_int32(0, true);
        writer.write_int32(0, true);
        writer.write_int32(0, true);
        writer.finalize();
        
        REQUIRE(col.num_rows == 3);
        
        auto* page_data = col.pages[0]->data;
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        REQUIRE(num_non_null == 0);
    }
}

TEST_CASE("ColumnWriter INT32 - Large data", "[column_writer_int32_large]") {
    SECTION("Write many values spanning multiple pages") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        for (int i = 0; i < 5000; ++i) {
            writer.write_int32(i, false);
        }
        writer.finalize();
        
        REQUIRE(col.num_rows == 5000);
        REQUIRE(col.pages.size() >= 2);
    }
    
    SECTION("Write with alternating NULLs") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        for (int i = 0; i < 1000; ++i) {
            writer.write_int32(i, i % 2 == 0);
        }
        writer.finalize();
        
        REQUIRE(col.num_rows == 1000);
        
        size_t total_non_null = 0;
        for (auto* page : col.pages) {
            auto* page_data = page->data;
            uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
            total_non_null += num_non_null;
        }
        REQUIRE(total_non_null == 500);
    }
}

TEST_CASE("ColumnWriter VARCHAR - Basic operations", "[column_writer_varchar]") {
    Plan plan;
    
    SECTION("Write single VARCHAR reference") {
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        VarcharRef ref = make_varchar_ref(1, 2, 3, 4);
        value_t val(ref);
        
        writer.write_varchar(val, plan);
        writer.finalize();
        
        REQUIRE(col.num_rows == 1);
        REQUIRE(col.pages.size() == 1);
        
        auto* page_data = col.pages[0]->data;
        uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        
        REQUIRE(num_rows == 1);
        REQUIRE(num_non_null == 1);
        
        auto* data = reinterpret_cast<uint64_t*>(page_data + 8);
        REQUIRE(data[0] == ref);
    }
    
    SECTION("Write multiple VARCHAR references") {
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        VarcharRef ref1 = make_varchar_ref(1, 2, 3, 4);
        VarcharRef ref2 = make_varchar_ref(5, 6, 7, 8);
        VarcharRef ref3 = make_varchar_ref(9, 10, 11, 12);
        
        writer.write_varchar(value_t(ref1), plan);
        writer.write_varchar(value_t(ref2), plan);
        writer.write_varchar(value_t(ref3), plan);
        writer.finalize();
        
        REQUIRE(col.num_rows == 3);
        
        auto* page_data = col.pages[0]->data;
        auto* data = reinterpret_cast<uint64_t*>(page_data + 8);
        REQUIRE(data[0] == ref1);
        REQUIRE(data[1] == ref2);
        REQUIRE(data[2] == ref3);
    }
    
    SECTION("Write NULL VARCHAR") {
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        VarcharRef ref1 = make_varchar_ref(1, 2, 3, 4);
        value_t val_null(NULL_VARCHAR_REF);
        VarcharRef ref3 = make_varchar_ref(9, 10, 11, 12);
        
        writer.write_varchar(value_t(ref1), plan);
        writer.write_varchar(val_null, plan);
        writer.write_varchar(value_t(ref3), plan);
        writer.finalize();
        
        REQUIRE(col.num_rows == 3);
        
        auto* page_data = col.pages[0]->data;
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        REQUIRE(num_non_null == 2);
        
        size_t bitmap_size = 1;
        auto* bitmap = reinterpret_cast<uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
        REQUIRE(get_bitmap(bitmap, 0) == true);
        REQUIRE(get_bitmap(bitmap, 1) == false);
        REQUIRE(get_bitmap(bitmap, 2) == true);
    }
}

TEST_CASE("ColumnWriter VARCHAR - Large data", "[column_writer_varchar_large]") {
    Plan plan;
    
    SECTION("Write many VARCHAR references") {
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        for (int i = 0; i < 3000; ++i) {
            VarcharRef ref = make_varchar_ref(i % 100, i % 50, i, i % 1000);
            writer.write_varchar(value_t(ref), plan);
        }
        writer.finalize();
        
        REQUIRE(col.num_rows == 3000);
        REQUIRE(col.pages.size() >= 2);
    }
}

TEST_CASE("Materialize column - INT32", "[materialize_int32]") {
    SECTION("Materialize single page") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        writer.write_int32(10, false);
        writer.write_int32(20, false);
        writer.write_int32(30, false);
        writer.finalize();
        
        auto values = materialize_column(col);
        
        REQUIRE(values.size() == 3);
        REQUIRE(values[0].int_val == 10);
        REQUIRE(values[0].is_null == false);
        REQUIRE(values[1].int_val == 20);
        REQUIRE(values[1].is_null == false);
        REQUIRE(values[2].int_val == 30);
        REQUIRE(values[2].is_null == false);
    }
    
    SECTION("Materialize with NULLs") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        writer.write_int32(10, false);
        writer.write_int32(0, true);
        writer.write_int32(30, false);
        writer.finalize();
        
        auto values = materialize_column(col);
        
        REQUIRE(values.size() == 3);
        REQUIRE(values[0].int_val == 10);
        REQUIRE(values[0].is_null == false);
        REQUIRE(values[1].is_null == true);
        REQUIRE(values[2].int_val == 30);
        REQUIRE(values[2].is_null == false);
    }
    
    SECTION("Materialize multiple pages") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        for (int i = 0; i < 5000; ++i) {
            writer.write_int32(i, false);
        }
        writer.finalize();
        
        auto values = materialize_column(col);
        
        REQUIRE(values.size() == 5000);
        for (int i = 0; i < 5000; ++i) {
            REQUIRE(values[i].int_val == i);
            REQUIRE(values[i].is_null == false);
        }
    }
    
    SECTION("Materialize empty column") {
        column_t col(DataType::INT32);
        
        auto values = materialize_column(col);
        
        REQUIRE(values.size() == 0);
    }
}

TEST_CASE("Materialize column - VARCHAR", "[materialize_varchar]") {
    Plan plan;
    
    SECTION("Materialize single page") {
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        VarcharRef ref1 = make_varchar_ref(1, 2, 3, 4);
        VarcharRef ref2 = make_varchar_ref(5, 6, 7, 8);
        VarcharRef ref3 = make_varchar_ref(9, 10, 11, 12);
        
        writer.write_varchar(value_t(ref1), plan);
        writer.write_varchar(value_t(ref2), plan);
        writer.write_varchar(value_t(ref3), plan);
        writer.finalize();
        
        auto values = materialize_column(col);
        
        REQUIRE(values.size() == 3);
        REQUIRE(values[0].varchar_ref == ref1);
        REQUIRE(values[0].is_null == false);
        REQUIRE(values[1].varchar_ref == ref2);
        REQUIRE(values[1].is_null == false);
        REQUIRE(values[2].varchar_ref == ref3);
        REQUIRE(values[2].is_null == false);
    }
    
    SECTION("Materialize with NULLs") {
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        VarcharRef ref1 = make_varchar_ref(1, 2, 3, 4);
        value_t val_null(NULL_VARCHAR_REF);
        VarcharRef ref3 = make_varchar_ref(9, 10, 11, 12);
        
        writer.write_varchar(value_t(ref1), plan);
        writer.write_varchar(val_null, plan);
        writer.write_varchar(value_t(ref3), plan);
        writer.finalize();
        
        auto values = materialize_column(col);
        
        REQUIRE(values.size() == 3);
        REQUIRE(values[0].varchar_ref == ref1);
        REQUIRE(values[0].is_null == false);
        REQUIRE(values[1].is_null == true);
        REQUIRE(values[2].varchar_ref == ref3);
        REQUIRE(values[2].is_null == false);
    }
    
    SECTION("Materialize multiple pages") {
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        std::vector<VarcharRef> refs;
        for (int i = 0; i < 3000; ++i) {
            VarcharRef ref = make_varchar_ref(i % 100, i % 50, i, i % 1000);
            refs.push_back(ref);
            writer.write_varchar(value_t(ref), plan);
        }
        writer.finalize();
        
        auto values = materialize_column(col);
        
        REQUIRE(values.size() == 3000);
        for (int i = 0; i < 3000; ++i) {
            REQUIRE(values[i].varchar_ref == refs[i]);
            REQUIRE(values[i].is_null == false);
        }
    }
}

TEST_CASE("Round-trip write and materialize", "[round_trip]") {
    SECTION("INT32 round-trip") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        std::vector<int32_t> expected_values;
        std::vector<bool> expected_nulls;
        
        std::mt19937 rng(42);
        std::uniform_int_distribution<int32_t> value_dist(-1000, 1000);
        std::uniform_int_distribution<int> null_dist(0, 9);
        
        for (int i = 0; i < 10000; ++i) {
            int32_t val = value_dist(rng);
            bool is_null = (null_dist(rng) == 0);
            expected_values.push_back(val);
            expected_nulls.push_back(is_null);
            writer.write_int32(val, is_null);
        }
        writer.finalize();
        
        auto values = materialize_column(col);
        
        REQUIRE(values.size() == 10000);
        for (int i = 0; i < 10000; ++i) {
            REQUIRE(values[i].is_null == expected_nulls[i]);
            if (!expected_nulls[i]) {
                REQUIRE(values[i].int_val == expected_values[i]);
            }
        }
    }
    
    SECTION("VARCHAR round-trip") {
        Plan plan;
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        std::vector<VarcharRef> expected_refs;
        std::vector<bool> expected_nulls;
        
        std::mt19937 rng(123);
        std::uniform_int_distribution<uint16_t> table_dist(0, 100);
        std::uniform_int_distribution<uint16_t> col_dist(0, 50);
        std::uniform_int_distribution<uint32_t> page_dist(0, 1000);
        std::uniform_int_distribution<uint16_t> idx_dist(0, 500);
        std::uniform_int_distribution<int> null_dist(0, 9);
        
        for (int i = 0; i < 5000; ++i) {
            bool is_null = (null_dist(rng) == 0);
            VarcharRef ref;
            if (is_null) {
                ref = NULL_VARCHAR_REF;
            } else {
                ref = make_varchar_ref(table_dist(rng), col_dist(rng), 
                                      page_dist(rng), idx_dist(rng));
            }
            expected_refs.push_back(ref);
            expected_nulls.push_back(is_null);
            writer.write_varchar(value_t(ref), plan);
        }
        writer.finalize();
        
        auto values = materialize_column(col);
        
        REQUIRE(values.size() == 5000);
        for (int i = 0; i < 5000; ++i) {
            REQUIRE(values[i].is_null == expected_nulls[i]);
            if (!expected_nulls[i]) {
                REQUIRE(values[i].varchar_ref == expected_refs[i]);
            }
        }
    }
}

TEST_CASE("ColumnWriter stress test", "[stress]") {
    SECTION("Very large INT32 column") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        constexpr int N = 100000;
        for (int i = 0; i < N; ++i) {
            writer.write_int32(i, false);
        }
        writer.finalize();
        
        REQUIRE(col.num_rows == N);
        
        auto values = materialize_column(col);
        REQUIRE(values.size() == N);
        
        REQUIRE(values[0].int_val == 0);
        REQUIRE(values[N/2].int_val == N/2);
        REQUIRE(values[N-1].int_val == N-1);
    }
    
    SECTION("Very large VARCHAR column") {
        Plan plan;
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        constexpr int N = 50000;
        for (int i = 0; i < N; ++i) {
            VarcharRef ref = make_varchar_ref(i % 1000, i % 500, i, i % 2000);
            writer.write_varchar(value_t(ref), plan);
        }
        writer.finalize();
        
        REQUIRE(col.num_rows == N);
        
        auto values = materialize_column(col);
        REQUIRE(values.size() == N);
    }
}

TEST_CASE("Edge cases and boundary conditions", "[edge_cases]") {
    SECTION("Write single value then finalize") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        writer.write_int32(42, false);
        writer.finalize();
        
        REQUIRE(col.num_rows == 1);
    }
    
    SECTION("Finalize empty writer") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        writer.finalize();
        
        REQUIRE(col.num_rows == 0);
    }
    
    SECTION("Write max int32 value") {
        column_t col(DataType::INT32);
        ColumnWriter writer(col);
        
        writer.write_int32(INT32_MAX, false);
        writer.write_int32(INT32_MIN, false);
        writer.finalize();
        
        auto values = materialize_column(col);
        REQUIRE(values[0].int_val == INT32_MAX);
        REQUIRE(values[1].int_val == INT32_MIN);
    }
    
    SECTION("Write max VarcharRef values") {
        Plan plan;
        column_t col(DataType::VARCHAR);
        ColumnWriter writer(col);
        
        VarcharRef max_ref = make_varchar_ref(0xFFFF, 0xFFFF, 0xFFFFF, 0xFFF);
        writer.write_varchar(value_t(max_ref), plan);
        writer.finalize();
        
        auto values = materialize_column(col);
        REQUIRE(values[0].varchar_ref == max_ref);
    }
}