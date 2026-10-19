#ifndef PROJECT2_H
#define PROJECT2_H

#include <hardware.h>
#include <plan.h>
#include <table.h>
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <cassert>
#include <immintrin.h>

namespace Contest {

/////////////////////////////////////////////////////////////////////////////////////////
////COLUMN_T WAS ALSO CHANGED, BUT WE KEEP IT IN PROJECT2.H FILE DUE TO DEPENDENCY ISSUES
/////////////////////////////////////////////////////////////////////////////////////////

// column-store structure for intermediate results (Task 2)
// Added mapped_memory for reference counting when sharing pages
struct column_t {
    DataType type;
    std::vector<Page*> pages;  // paged storage for column data
    size_t num_rows;
    MappedMemory* mapped_memory;  // For tracking shared pages from input (BETTER INDEXING TASK)
    
    column_t(DataType t) : type(t), num_rows(0), mapped_memory(nullptr) {}
    
    ~column_t() {
        // if mapped_memory is set, pages are shared - use reference counting
        if (mapped_memory != nullptr) {
            if (--mapped_memory->refs == 0) {
                delete mapped_memory;
            }
            return;
        }
        // if not set, we own the pages and must delete them
        for (auto* page : pages) {
            delete page;
        }
    }
    
    column_t(column_t&& other) noexcept
        : type(other.type)
        , pages(std::move(other.pages))
        , num_rows(other.num_rows)
        , mapped_memory(other.mapped_memory) {
        other.pages.clear();
        other.num_rows = 0;
        other.mapped_memory = nullptr;
    }
    
    column_t& operator=(column_t&& other) noexcept {
        if (this != &other) {
            // Clean up existing resources
            if (mapped_memory != nullptr) {
                if (--mapped_memory->refs == 0) {
                    delete mapped_memory;
                }
            } else {
                for (auto* page : pages) {
                    delete page;
                }
            }

            // Move from other
            type = other.type;
            pages = std::move(other.pages);
            num_rows = other.num_rows;
            mapped_memory = other.mapped_memory;

            other.pages.clear();
            other.num_rows = 0;
            other.mapped_memory = nullptr;
        }
        return *this;
    }
    
    column_t(const column_t&) = delete;
    column_t& operator=(const column_t&) = delete;
};





/////////////////////////////////////////////////////////
/////////////////////////TASK 1//////////////////////////
/////////////////////////////////////////////////////////

using VarcharRef = uint64_t;

constexpr uint64_t NULL_VARCHAR_REF = 0xFFFFFFFFFFFFFFFFULL;

inline VarcharRef make_varchar_ref(uint16_t table_id, uint16_t column_id, 
                                    uint32_t page_id, uint16_t value_idx) {
    VarcharRef ref = 0;
    ref |= (static_cast<uint64_t>(table_id) << 48);
    ref |= (static_cast<uint64_t>(column_id) << 32);
    ref |= (static_cast<uint64_t>(page_id) << 12);
    ref |= (static_cast<uint64_t>(value_idx) & 0xFFF);
    return ref;
}

inline uint16_t ref_table_id(VarcharRef ref) {
    return (ref >> 48) & 0xFFFF; }

inline uint16_t ref_column_id(VarcharRef ref) {
    return (ref >> 32) & 0xFFFF; }

inline uint32_t ref_page_id(VarcharRef ref) {
    return (ref >> 12) & 0xFFFFF; }

inline uint16_t ref_value_idx(VarcharRef ref) {
    return ref & 0xFFF; }

struct value_t {
    union {
        int32_t int_val;
        VarcharRef varchar_ref;
    };
    bool is_null;

    value_t() : int_val(0), is_null(true) {}
    explicit value_t(int32_t v, bool null = false) : int_val(v), is_null(null) {}
    explicit value_t(VarcharRef v) : varchar_ref(v), is_null(v == NULL_VARCHAR_REF) {}
};

inline bool get_bitmap(const uint8_t* bitmap, size_t idx) {
    return (bitmap[idx / 8] >> (idx % 8)) & 1;
}

inline std::string materialize_varchar(VarcharRef ref, const Plan& plan) {
    if (ref == NULL_VARCHAR_REF) {
        return "";
    }
    
    uint16_t table_id = ref_table_id(ref); 
    uint16_t column_id = ref_column_id(ref);
    uint32_t page_id = ref_page_id(ref);
    uint16_t value_idx = ref_value_idx(ref);
    
    auto& column = plan.inputs[table_id].columns[column_id];
    auto* page = column.pages[page_id];
    auto* page_data = reinterpret_cast<const uint8_t*>(page->data);
    
    uint16_t n_r = *reinterpret_cast<const uint16_t*>(page_data);
    
    if (n_r == 0xffff) {
        std::string result;
        
        for (size_t curr_page_id = page_id; curr_page_id < column.pages.size(); ++curr_page_id) {
            auto* curr_page = column.pages[curr_page_id];
            auto* curr_data = reinterpret_cast<const uint8_t*>(curr_page->data);
            
            uint16_t curr_n_r = *reinterpret_cast<const uint16_t*>(curr_data);
            
            if (curr_n_r != 0xffff && curr_n_r != 0xfffe) {
                break;
            }
            
            uint16_t char_count = *reinterpret_cast<const uint16_t*>(curr_data + 2);
            const char* str_data = reinterpret_cast<const char*>(curr_data + 4);
            result.append(str_data, char_count);
            
            if (curr_page_id + 1 >= column.pages.size()) {
                break;
            }
            
            auto* next_page = column.pages[curr_page_id + 1];
            auto* next_data = reinterpret_cast<const uint8_t*>(next_page->data);
            uint16_t next_n_r = *reinterpret_cast<const uint16_t*>(next_data);
            
            if (next_n_r != 0xfffe) {
                break;
            }
        }
        
        return result;
    }
    
    uint16_t n_v = *reinterpret_cast<const uint16_t*>(page_data + 2);
    auto* offset_begin = reinterpret_cast<const uint16_t*>(page_data + 4);
    auto* data_begin = reinterpret_cast<const char*>(page_data + 4 + n_v * 2);
    
    uint16_t start_offset = (value_idx == 0) ? 0 : offset_begin[value_idx - 1];
    uint16_t end_offset = offset_begin[value_idx];
    
    return std::string(data_begin + start_offset, end_offset - start_offset);
}

inline void write_long_varchar(std::vector<Page*>& pages, const std::string& value) {
    size_t remaining = value.size();
    size_t offset = 0;
    bool is_first_page = true;
    
    while (remaining > 0) {
        Page* page = reinterpret_cast<Page*>(std::malloc(sizeof(Page)));
        auto* page_data = page->data;
        
        size_t chunk_size = std::min(remaining, static_cast<size_t>(PAGE_SIZE - 4));
        
        if (is_first_page) {
            *reinterpret_cast<uint16_t*>(page_data) = 0xffff;
            is_first_page = false;
        } else {
            *reinterpret_cast<uint16_t*>(page_data) = 0xfffe;
        }
        *reinterpret_cast<uint16_t*>(page_data + 2) = static_cast<uint16_t>(chunk_size);
        
        std::memcpy(page_data + 4, value.data() + offset, chunk_size);
        
        pages.push_back(page);
        offset += chunk_size;
        remaining -= chunk_size;
    }
}

inline void write_varchar_page(Page* page, const std::vector<std::pair<std::string, bool>>& strings) {
    auto* page_data = page->data;
    
    uint16_t num_rows = strings.size();
    uint16_t num_non_null = 0;
    for (const auto& [s, is_null] : strings) {
        if (!is_null) ++num_non_null;
    }
    
    *reinterpret_cast<uint16_t*>(page_data) = num_rows;
    *reinterpret_cast<uint16_t*>(page_data + 2) = num_non_null;
    
    uint16_t offset_array_size = num_non_null * 2;
    uint16_t bitmap_size = (num_rows + 7) / 8;
    
    auto* offset_array = reinterpret_cast<uint16_t*>(page_data + 4);
    auto* string_data_base = reinterpret_cast<char*>(page_data + 4 + offset_array_size);
    auto* bitmap = reinterpret_cast<uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
    
    std::memset(bitmap, 0, bitmap_size);
    
    uint16_t current_offset = 0;
    uint16_t offset_idx = 0;
    
    for (uint16_t row_idx = 0; row_idx < num_rows; ++row_idx) {
        const auto& [value, is_null] = strings[row_idx];
        
        if (!is_null) {
            bitmap[row_idx / 8] |= (1 << (row_idx % 8));
            
            if (value.size() > 0) {
                std::memcpy(string_data_base + current_offset, value.data(), value.size());
            }
            current_offset += value.size();
            
            offset_array[offset_idx++] = current_offset;
        }
    }
}

inline ColumnarTable convert_to_columnar(const std::vector<std::vector<value_t>>& rows, 
                                   const std::vector<DataType>& types,
                                   const Plan& plan) {
    ColumnarTable result;
    result.num_rows = rows.size();
    
    for (size_t col_idx = 0; col_idx < types.size(); ++col_idx) {
        result.columns.emplace_back(types[col_idx]);
    }
    
    if (rows.empty()) {
        return result;
    }
    
    for (size_t col_idx = 0; col_idx < types.size(); ++col_idx) {
        auto& column = result.columns[col_idx];
        column.type = types[col_idx];
        
        if (types[col_idx] == DataType::INT32) {
            std::vector<std::pair<int32_t, bool>> values;
            values.reserve(rows.size());
            
            for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
                int32_t value = rows[row_idx][col_idx].int_val;
                bool is_null = rows[row_idx][col_idx].is_null;
                values.emplace_back(value, is_null);
            }
            
            size_t values_written = 0;
            while (values_written < values.size()) {
                Page* page = reinterpret_cast<Page*>(std::malloc(sizeof(Page)));
                auto* page_data = page->data;
                
                size_t values_in_page = std::min(size_t(2000), values.size() - values_written);
                
                uint16_t num_rows = values_in_page;
                uint16_t num_non_null = 0;
                for (size_t i = 0; i < values_in_page; ++i) {
                    if (!values[values_written + i].second) {
                        ++num_non_null;
                    }
                }
                
                *reinterpret_cast<uint16_t*>(page_data) = num_rows;
                *reinterpret_cast<uint16_t*>(page_data + 2) = num_non_null;
                
                auto* data_begin = reinterpret_cast<int32_t*>(page_data + 4);
                uint16_t bitmap_size = (num_rows + 7) / 8;
                auto* bitmap = reinterpret_cast<uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
                
                std::memset(bitmap, 0, bitmap_size);
                
                uint16_t data_idx = 0;
                for (uint16_t i = 0; i < num_rows; ++i) {
                    const auto& [value, is_null] = values[values_written + i];
                    if (!is_null) {
                        bitmap[i / 8] |= (1 << (i % 8));
                        data_begin[data_idx++] = value;
                    }
                }
                
                column.pages.push_back(page);
                values_written += values_in_page;
            }
            
        } else if (types[col_idx] == DataType::VARCHAR) {
            std::vector<std::pair<std::string, bool>> page_strings;
            page_strings.reserve(500);
            
            for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
                VarcharRef ref = rows[row_idx][col_idx].varchar_ref;
                bool is_null = rows[row_idx][col_idx].is_null;
                
                std::string value;
                if (!is_null) {
                    value = materialize_varchar(ref, plan);
                }
                
                if (!is_null && value.size() > (PAGE_SIZE - 100)) {
                    if (!page_strings.empty()) {
                        Page* page = reinterpret_cast<Page*>(std::malloc(sizeof(Page)));
                        write_varchar_page(page, page_strings);
                        column.pages.push_back(page);
                        page_strings.clear();
                    }
                    
                    write_long_varchar(column.pages, value);
                    continue;
                }
                
                page_strings.emplace_back(value, is_null);
                
                uint16_t total_offset = 0;
                uint16_t total_non_null = 0;
                for (const auto& [s, n] : page_strings) {
                    if (!n) {
                        total_offset += s.size();
                        ++total_non_null;
                    }
                }
                
                uint16_t total_rows = page_strings.size();
                uint16_t bitmap_size = (total_rows + 7) / 8;
                uint16_t string_data_start = 4 + total_non_null * 2;
                uint16_t string_data_end = string_data_start + total_offset;
                uint16_t bitmap_start = PAGE_SIZE - bitmap_size;
                
                bool is_full = (string_data_end > bitmap_start) || 
                              (total_rows >= 65535) || 
                              (total_non_null >= 32767);
                
                if (is_full && page_strings.size() > 1) {
                    page_strings.pop_back();
                    
                    Page* page = reinterpret_cast<Page*>(std::malloc(sizeof(Page)));
                    write_varchar_page(page, page_strings);
                    column.pages.push_back(page);
                    
                    page_strings.clear();
                    page_strings.emplace_back(value, is_null);
                }
            }
            
            if (!page_strings.empty()) {
                Page* page = reinterpret_cast<Page*>(std::malloc(sizeof(Page)));
                write_varchar_page(page, page_strings);
                column.pages.push_back(page);
            }
        }
    }
    
    return result;
}

/////////////////////////////////////////////////////////
/////////////////////////TASK 2//////////////////////////
/////////////////////////////////////////////////////////

using ExecuteResult = std::vector<column_t>;

inline std::vector<value_t> materialize_column(const column_t& col) {
    std::vector<value_t> result;
    result.reserve(col.num_rows);
    
    if (col.type == DataType::INT32) {
        for (auto* page : col.pages) {
            auto* page_data = page->data;
            uint16_t num_rows = *reinterpret_cast<const uint16_t*>(page_data);
            
            size_t bitmap_size = (num_rows + 7) / 8;
            auto* bitmap = reinterpret_cast<const uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
            auto* data = reinterpret_cast<const int32_t*>(page_data + 4);
            
            uint16_t data_idx = 0;
            for (uint16_t row = 0; row < num_rows; ++row) {
                bool has_value = (bitmap[row / 8] >> (row % 8)) & 1;
                if (has_value) {
                    result.emplace_back(data[data_idx++], false);
                } else {
                    result.emplace_back(0, true);
                }
            }
        }
    } else if (col.type == DataType::VARCHAR) {
        for (auto* page : col.pages) {
            auto* page_data = page->data;
            uint16_t num_rows = *reinterpret_cast<const uint16_t*>(page_data);
            
            size_t bitmap_size = (num_rows + 7) / 8;
            auto* bitmap = reinterpret_cast<const uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
            auto* data = reinterpret_cast<const uint64_t*>(page_data + 8);
            
            uint16_t data_idx = 0;
            for (uint16_t row = 0; row < num_rows; ++row) {
                bool has_value = (bitmap[row / 8] >> (row % 8)) & 1;
                if (has_value) {
                    result.emplace_back(data[data_idx++]);
                } else {
                    result.emplace_back(NULL_VARCHAR_REF);
                }
            }
        }
    }
    
    return result;
}

class ColumnWriter {
public:
    column_t& column;
    Page* current_page;
    uint16_t num_rows_in_page;
    uint16_t num_non_null_in_page;
    size_t data_offset;
    std::vector<uint8_t> bitmap_buffer;
    
    ColumnWriter(column_t& col) 
        : column(col)
        , current_page(nullptr)
        , num_rows_in_page(0)
        , num_non_null_in_page(0)
        , data_offset(0) {
        bitmap_buffer.resize(PAGE_SIZE, 0);
        allocate_new_page();
    }
    
    void allocate_new_page() {
        current_page = new Page();
        column.pages.push_back(current_page);
        num_rows_in_page = 0;
        num_non_null_in_page = 0;
        std::memset(bitmap_buffer.data(), 0, PAGE_SIZE);
        
        data_offset = (column.type == DataType::INT32) ? 4 : 8;
    }
    
    void set_bitmap(uint16_t idx) {
        bitmap_buffer[idx / 8] |= (1 << (idx % 8));
    }
    
    void write_int32(int32_t value, bool is_null) {
        size_t bitmap_size = (num_rows_in_page + 1 + 7) / 8;
        size_t needed_space = data_offset + (is_null ? 0 : 4) + bitmap_size;
        
        if (needed_space > PAGE_SIZE) {
            flush_page();
            allocate_new_page();
        }
        
        if (!is_null) {
            auto* page_data = current_page->data;
            *reinterpret_cast<int32_t*>(page_data + data_offset) = value;
            data_offset += 4;
            set_bitmap(num_rows_in_page);
            num_non_null_in_page++;
        }
        
        num_rows_in_page++;
        column.num_rows++;
    }
    
    void write_varchar(const value_t& val, const Plan& plan) {
        uint64_t ref = val.is_null ? NULL_VARCHAR_REF : val.varchar_ref;
        bool is_null = val.is_null;
        
        size_t bitmap_size = (num_rows_in_page + 1 + 7) / 8;
        size_t needed_space = data_offset + (is_null ? 0 : 8) + bitmap_size;
        
        if (needed_space > PAGE_SIZE) {
            flush_page();
            allocate_new_page();
        }
        
        if (!is_null) {
            auto* page_data = current_page->data;
            *reinterpret_cast<uint64_t*>(page_data + data_offset) = ref;
            data_offset += 8;
            set_bitmap(num_rows_in_page);
            num_non_null_in_page++;
        }
        
        num_rows_in_page++;
        column.num_rows++;
    }
    
    void flush_page() {
        if (num_rows_in_page == 0) return;
        
        auto* page_data = current_page->data;
        *reinterpret_cast<uint16_t*>(page_data) = num_rows_in_page;
        *reinterpret_cast<uint16_t*>(page_data + 2) = num_non_null_in_page;
        
        size_t bitmap_size = (num_rows_in_page + 7) / 8;
        std::memcpy(page_data + PAGE_SIZE - bitmap_size, bitmap_buffer.data(), bitmap_size);
    }
    
    void finalize() {
        flush_page();
    }
};

/////////////////////////////////////////////////////////
/////////////////////////TASK 3//////////////////////////
/////////////////////////////////////////////////////////

inline uint32_t crc32(uint32_t v) {
    return _mm_crc32_u32(0, v);
}

inline uint64_t next_power2(uint64_t v) {
    if (v <= 1) 
        return 1;
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    return ++v;
}

inline int ilog2_u64(uint64_t v) {
    assert(v > 0);
    return 63 - __builtin_clzll(v);
}

inline uint16_t compute_bloom(uint32_t suffix) {
    uint16_t bloom = 0;
    bloom |= (1u << ((suffix >> 0) & 0xF));
    bloom |= (1u << ((suffix >> 4) & 0xF));
    bloom |= (1u << ((suffix >> 8) & 0xF));
    bloom |= (1u << ((suffix >> 12) & 0xF));
    return bloom;
}

struct AdjacencyEntry {
    int32_t key;
    size_t payload;
};

} // namespace Contest

#endif // PROJECT2_H