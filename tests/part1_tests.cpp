#include <catch2/catch_test_macros.hpp>

#include <table.h>
#include <plan.h>
#include <hardware.h>
#include <cstring>

#include "part1functions.h"

using namespace Contest;

TEST_CASE("VarcharRef encoding and decoding", "[varchar_ref]") {
    SECTION("Basic encoding") {
        VarcharRef ref = make_varchar_ref(10, 20, 30, 40);
        
        REQUIRE(ref_table_id(ref) == 10);
        REQUIRE(ref_column_id(ref) == 20);
        REQUIRE(ref_page_id(ref) == 30);
        REQUIRE(ref_value_idx(ref) == 40);
    }
    
    SECTION("Max values") {
        VarcharRef ref = make_varchar_ref(0xFFFF, 0xFFFF, 0xFFFFF, 0xFFF);
        
        REQUIRE(ref_table_id(ref) == 0xFFFF);
        REQUIRE(ref_column_id(ref) == 0xFFFF);
        REQUIRE(ref_page_id(ref) == 0xFFFFF);
        REQUIRE(ref_value_idx(ref) == 0xFFF);
    }
    
    SECTION("Zero values") {
        VarcharRef ref = make_varchar_ref(0, 0, 0, 0);
        
        REQUIRE(ref_table_id(ref) == 0);
        REQUIRE(ref_column_id(ref) == 0);
        REQUIRE(ref_page_id(ref) == 0);
        REQUIRE(ref_value_idx(ref) == 0);
    }
    
    SECTION("NULL reference") {
        VarcharRef ref = NULL_VARCHAR_REF;
        REQUIRE(ref == 0xFFFFFFFFFFFFFFFFULL);
    }
}

TEST_CASE("value_t construction", "[value_t]") {
    SECTION("Default constructor creates NULL") {
        value_t v;
        REQUIRE(v.is_null == true);
    }
    
    SECTION("Int32 constructor") {
        value_t v(42);
        REQUIRE(v.int_val == 42);
        REQUIRE(v.is_null == false);
    }
    
    SECTION("Int32 NULL constructor") {
        value_t v(42, true);
        REQUIRE(v.int_val == 42);
        REQUIRE(v.is_null == true);
    }
    
    SECTION("VarcharRef constructor") {
        VarcharRef ref = make_varchar_ref(1, 2, 3, 4);
        value_t v(ref);
        REQUIRE(v.varchar_ref == ref);
        REQUIRE(v.is_null == false);
    }
    
    SECTION("NULL VarcharRef constructor") {
        value_t v(NULL_VARCHAR_REF);
        REQUIRE(v.varchar_ref == NULL_VARCHAR_REF);
        REQUIRE(v.is_null == true);
    }
}

TEST_CASE("Bitmap operations", "[bitmap]") {
    uint8_t bitmap[8] = {0};
    
    SECTION("All bits initially zero") {
        for (size_t i = 0; i < 64; ++i) {
            REQUIRE(get_bitmap(bitmap, i) == false);
        }
    }
    
    SECTION("Set individual bits") {
        bitmap[0] = 0b00000001;
        REQUIRE(get_bitmap(bitmap, 0) == true);
        REQUIRE(get_bitmap(bitmap, 1) == false);
        
        bitmap[0] = 0b10000000;
        REQUIRE(get_bitmap(bitmap, 7) == true);
        REQUIRE(get_bitmap(bitmap, 6) == false);
    }
    
    SECTION("Multiple bytes") {
        bitmap[1] = 0b00000100;
        REQUIRE(get_bitmap(bitmap, 10) == true);
        REQUIRE(get_bitmap(bitmap, 9) == false);
        REQUIRE(get_bitmap(bitmap, 11) == false);
    }
    
    SECTION("All bits set") {
        std::memset(bitmap, 0xFF, 8);
        for (size_t i = 0; i < 64; ++i) {
            REQUIRE(get_bitmap(bitmap, i) == true);
        }
    }
}

TEST_CASE("Write VARCHAR page", "[varchar_page]") {
    SECTION("Single string") {
        Page* page = reinterpret_cast<Page*>(std::malloc(sizeof(Page)));
        std::vector<std::pair<std::string, bool>> strings = {
            {"hello", false}
        };
        
        write_varchar_page(page, strings);
        
        auto* page_data = page->data;
        uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        
        REQUIRE(num_rows == 1);
        REQUIRE(num_non_null == 1);
        
        uint16_t bitmap_size = (num_rows + 7) / 8;
        auto* bitmap = reinterpret_cast<uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
        REQUIRE(get_bitmap(bitmap, 0) == true);
        
        std::free(page);
    }
    
    SECTION("Multiple strings") {
        Page* page = reinterpret_cast<Page*>(std::malloc(sizeof(Page)));
        std::vector<std::pair<std::string, bool>> strings = {
            {"first", false},
            {"second", false},
            {"third", false}
        };
        
        write_varchar_page(page, strings);
        
        auto* page_data = page->data;
        uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        
        REQUIRE(num_rows == 3);
        REQUIRE(num_non_null == 3);
        
        std::free(page);
    }
    
    SECTION("Strings with NULLs") {
        Page* page = reinterpret_cast<Page*>(std::malloc(sizeof(Page)));
        std::vector<std::pair<std::string, bool>> strings = {
            {"first", false},
            {"", true},
            {"third", false}
        };
        
        write_varchar_page(page, strings);
        
        auto* page_data = page->data;
        uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        
        REQUIRE(num_rows == 3);
        REQUIRE(num_non_null == 2);
        
        uint16_t bitmap_size = (num_rows + 7) / 8;
        auto* bitmap = reinterpret_cast<uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
        REQUIRE(get_bitmap(bitmap, 0) == true);
        REQUIRE(get_bitmap(bitmap, 1) == false);
        REQUIRE(get_bitmap(bitmap, 2) == true);
        
        std::free(page);
    }
    
    SECTION("Empty strings") {
        Page* page = reinterpret_cast<Page*>(std::malloc(sizeof(Page)));
        std::vector<std::pair<std::string, bool>> strings = {
            {"", false},
            {"test", false},
            {"", false}
        };
        
        write_varchar_page(page, strings);
        
        auto* page_data = page->data;
        uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        
        REQUIRE(num_rows == 3);
        REQUIRE(num_non_null == 3);
        
        std::free(page);
    }
}

TEST_CASE("Write long VARCHAR", "[long_varchar]") {
    SECTION("String larger than page size") {
        std::string long_string(PAGE_SIZE * 2, 'x');
        std::vector<Page*> pages;
        
        write_long_varchar(pages, long_string);
        
        REQUIRE(pages.size() >= 2);
        
        auto* first_data = pages[0]->data;
        uint16_t marker = *reinterpret_cast<uint16_t*>(first_data);
        REQUIRE(marker == 0xffff);
        
        if (pages.size() > 1) {
            auto* cont_data = pages[1]->data;
            uint16_t cont_marker = *reinterpret_cast<uint16_t*>(cont_data);
            REQUIRE(cont_marker == 0xfffe);
        }
        
        for (auto* page : pages) {
            std::free(page);
        }
    }
    
    SECTION("String exactly one page minus overhead") {
        std::string exact_string(PAGE_SIZE - 4, 'y');
        std::vector<Page*> pages;
        
        write_long_varchar(pages, exact_string);
        
        REQUIRE(pages.size() == 1);
        
        for (auto* page : pages) {
            std::free(page);
        }
    }
}

TEST_CASE("Convert to columnar - INT32", "[convert_columnar]") {
    Plan plan;
    
    SECTION("Empty rows") {
        std::vector<std::vector<value_t>> rows;
        std::vector<DataType> types = {DataType::INT32};
        
        ColumnarTable result = convert_to_columnar(rows, types, plan);
        
        REQUIRE(result.num_rows == 0);
        REQUIRE(result.columns.size() == 1);
        REQUIRE(result.columns[0].pages.size() == 0);
        
    }
    
    SECTION("Single INT32 column") {
        std::vector<std::vector<value_t>> rows = {
            {value_t(10)},
            {value_t(20)},
            {value_t(30)}
        };
        std::vector<DataType> types = {DataType::INT32};
        
        ColumnarTable result = convert_to_columnar(rows, types, plan);
        
        REQUIRE(result.num_rows == 3);
        REQUIRE(result.columns.size() == 1);
        REQUIRE(result.columns[0].type == DataType::INT32);
        REQUIRE(result.columns[0].pages.size() == 1);
        
        auto* page_data = result.columns[0].pages[0]->data;
        uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        
        REQUIRE(num_rows == 3);
        REQUIRE(num_non_null == 3);
        
        auto* data = reinterpret_cast<int32_t*>(page_data + 4);
        REQUIRE(data[0] == 10);
        REQUIRE(data[1] == 20);
        REQUIRE(data[2] == 30);
        
    }
    
    SECTION("INT32 with NULLs") {
        std::vector<std::vector<value_t>> rows = {
            {value_t(10)},
            {value_t(20, true)},
            {value_t(30)}
        };
        std::vector<DataType> types = {DataType::INT32};
        
        ColumnarTable result = convert_to_columnar(rows, types, plan);
        
        REQUIRE(result.num_rows == 3);
        REQUIRE(result.columns[0].pages.size() == 1);
        
        auto* page_data = result.columns[0].pages[0]->data;
        uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        
        REQUIRE(num_rows == 3);
        REQUIRE(num_non_null == 2);
        
        uint16_t bitmap_size = (num_rows + 7) / 8;
        auto* bitmap = reinterpret_cast<uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
        REQUIRE(get_bitmap(bitmap, 0) == true);
        REQUIRE(get_bitmap(bitmap, 1) == false);
        REQUIRE(get_bitmap(bitmap, 2) == true);
        
    }
    
    SECTION("Large number of INT32 rows") {
        std::vector<std::vector<value_t>> rows;
        for (int i = 0; i < 5000; ++i) {
            rows.push_back({value_t(i)});
        }
        std::vector<DataType> types = {DataType::INT32};
        
        ColumnarTable result = convert_to_columnar(rows, types, plan);
        
        REQUIRE(result.num_rows == 5000);
        REQUIRE(result.columns[0].pages.size() >= 2);
    }
}

TEST_CASE("Convert to columnar - Multiple columns", "[convert_columnar_multi]") {
    Plan plan;
    
    SECTION("Two INT32 columns") {
        std::vector<std::vector<value_t>> rows = {
            {value_t(10), value_t(100)},
            {value_t(20), value_t(200)},
            {value_t(30), value_t(300)}
        };
        std::vector<DataType> types = {DataType::INT32, DataType::INT32};
        
        ColumnarTable result = convert_to_columnar(rows, types, plan);
        
        REQUIRE(result.num_rows == 3);
        REQUIRE(result.columns.size() == 2);
        
        auto* page_data_0 = result.columns[0].pages[0]->data;
        auto* data_0 = reinterpret_cast<int32_t*>(page_data_0 + 4);
        REQUIRE(data_0[0] == 10);
        REQUIRE(data_0[1] == 20);
        REQUIRE(data_0[2] == 30);
        
        auto* page_data_1 = result.columns[1].pages[0]->data;
        auto* data_1 = reinterpret_cast<int32_t*>(page_data_1 + 4);
        REQUIRE(data_1[0] == 100);
        REQUIRE(data_1[1] == 200);
        REQUIRE(data_1[2] == 300);
    }
    
    SECTION("Multiple columns with NULLs") {
        std::vector<std::vector<value_t>> rows = {
            {value_t(10), value_t(100, true)},
            {value_t(20, true), value_t(200)},
            {value_t(30), value_t(300)}
        };
        std::vector<DataType> types = {DataType::INT32, DataType::INT32};
        
        ColumnarTable result = convert_to_columnar(rows, types, plan);
        
        REQUIRE(result.num_rows == 3);
        
        auto* page_data_0 = result.columns[0].pages[0]->data;
        uint16_t bitmap_size_0 = 1;
        auto* bitmap_0 = reinterpret_cast<uint8_t*>(page_data_0 + PAGE_SIZE - bitmap_size_0);
        REQUIRE(get_bitmap(bitmap_0, 0) == true);
        REQUIRE(get_bitmap(bitmap_0, 1) == false);
        REQUIRE(get_bitmap(bitmap_0, 2) == true);
        
        auto* page_data_1 = result.columns[1].pages[0]->data;
        uint16_t bitmap_size_1 = 1;
        auto* bitmap_1 = reinterpret_cast<uint8_t*>(page_data_1 + PAGE_SIZE - bitmap_size_1);
        REQUIRE(get_bitmap(bitmap_1, 0) == false);
        REQUIRE(get_bitmap(bitmap_1, 1) == true);
        REQUIRE(get_bitmap(bitmap_1, 2) == true);
        
    }
}

TEST_CASE("Edge cases", "[edge_cases]") {
    SECTION("All NULL INT32 column") {
        Plan plan;
        std::vector<std::vector<value_t>> rows = {
            {value_t(0, true)},
            {value_t(0, true)},
            {value_t(0, true)}
        };
        std::vector<DataType> types = {DataType::INT32};
        
        ColumnarTable result = convert_to_columnar(rows, types, plan);
        
        REQUIRE(result.num_rows == 3);
        auto* page_data = result.columns[0].pages[0]->data;
        uint16_t num_non_null = *reinterpret_cast<uint16_t*>(page_data + 2);
        REQUIRE(num_non_null == 0);
        
    }
    
    SECTION("Single row") {
        Plan plan;
        std::vector<std::vector<value_t>> rows = {
            {value_t(42)}
        };
        std::vector<DataType> types = {DataType::INT32};
        
        ColumnarTable result = convert_to_columnar(rows, types, plan);
        
        REQUIRE(result.num_rows == 1);
        REQUIRE(result.columns[0].pages.size() == 1);
    }
}