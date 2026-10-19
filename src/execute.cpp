#include <hardware.h>
#include <plan.h>
#include <table.h>
#include <iostream>
#include <atomic>

#include <project2.h>
#include <parallelProject.h>

namespace Contest {

struct JoinAlgorithm { 
    bool build_left;
    ExecuteResult& left;
    ExecuteResult& right;
    ExecuteResult& results;
    size_t left_col, right_col;
    const std::vector<std::tuple<size_t, DataType>>& output_attrs;
    const Plan& plan;
    void* context;  // added context for thread pool
    
    template <class T>
    void run() {
        // Materialize columns for join processing
        std::vector<std::vector<value_t>> left_cols;
        left_cols.reserve(left.size());
        for (const auto& col : left) {
            left_cols.push_back(materialize_column(col));
        }
        
        std::vector<std::vector<value_t>> right_cols;
        right_cols.reserve(right.size());
        for (const auto& col : right) {
            right_cols.push_back(materialize_column(col));
        }
        
        // Use UnchainedHashTable for join
        UnchainedHashTable hash_table;
        
        const auto& build_cols = build_left ? left_cols : right_cols;
        const auto& probe_cols = build_left ? right_cols : left_cols;
        const auto& build_col_idx = build_left ? left_col : right_col;
        const auto& probe_col_idx = build_left ? right_col : left_col;
        
        // Build phase: extract keys and row indices
        std::vector<int32_t> build_keys;
        std::vector<size_t> build_payloads;
        
        const auto& build_col = build_cols[build_col_idx];
        for (size_t idx = 0; idx < build_col.size(); ++idx) {
            const auto& val = build_col[idx];
            if (val.is_null) continue;
            
            T key = *reinterpret_cast<const T*>(&val.int_val);
            build_keys.push_back(static_cast<int32_t>(key));
            build_payloads.push_back(idx);
        }
        
        // Use parallel build for large hash tables
        ExecutionContext* exec_ctx = static_cast<ExecutionContext*>(context);
        if (exec_ctx && build_keys.size() > 100000) {  // (worked better for 100000 and larger numbers)
            hash_table.build_parallel(build_keys, build_payloads, exec_ctx->num_threads);
        } else {
            hash_table.build(build_keys, build_payloads);
        }
        
        // Parallel probing
        const auto& probe_col = probe_cols[probe_col_idx];
        size_t num_probe_rows = probe_col.size();
        
        // Decide whether to use parallel probing
        size_t num_threads = (exec_ctx && num_probe_rows > 5000) ? exec_ctx->num_threads : 1;   
        
        if (num_threads > 1) {
            //////// PARALLEL PROBING WITH WORK STEALING
            
            // Each thread gets its own local result storage
            std::vector<ExecuteResult> thread_results(num_threads);
            std::vector<std::thread> threads;
            threads.reserve(num_threads);
            
            // Atomic work counter for work stealing
            std::atomic<size_t> work_counter(0);
            constexpr size_t WORK_CHUNK_SIZE = 1000;   // 1000 worked better, more or less had worse times
            
            for (size_t t = 0; t < num_threads; ++t) {
                threads.emplace_back([&, t]() {
                    // Initialize thread-local result columns
                    auto& local_results = thread_results[t];
                    for (auto [col_idx, type] : output_attrs) {
                        local_results.emplace_back(type);
                    }
                    
                    // Create writers for this thread
                    std::vector<ColumnWriter> local_writers;
                    for (auto& col : local_results) {
                        local_writers.emplace_back(col);
                    }
                    
                    // Work stealing loop
                    while (true) {
                        // Atomically fetch next work chunk
                        size_t chunk_start = work_counter.fetch_add(WORK_CHUNK_SIZE, std::memory_order_relaxed);
                        
                        if (chunk_start >= num_probe_rows) {
                            break;
                        }
                        
                        size_t chunk_end = std::min(chunk_start + WORK_CHUNK_SIZE, num_probe_rows);
                        
                        for (size_t probe_idx = chunk_start; probe_idx < chunk_end; ++probe_idx) {
                            const auto& probe_val = probe_col[probe_idx];
                            if (probe_val.is_null) continue;
                            
                            T key = *reinterpret_cast<const T*>(&probe_val.int_val);
                            
                            hash_table.probe(static_cast<int32_t>(key), [&](size_t build_idx) {
                                // Write output tuple
                                for (size_t out_idx = 0; out_idx < output_attrs.size(); ++out_idx) {
                                    auto [col_idx, type] = output_attrs[out_idx];
                                    
                                    const auto& val = (col_idx < left_cols.size()) ? 
                                        left_cols[col_idx][build_left ? build_idx : probe_idx] :
                                        right_cols[col_idx - left_cols.size()][build_left ? probe_idx : build_idx];
                                    
                                    if (type == DataType::INT32) {
                                        local_writers[out_idx].write_int32(val.int_val, val.is_null);
                                    } else {
                                        local_writers[out_idx].write_varchar(val, plan);
                                    }
                                }
                            });
                        }
                    }
                    
                    // Finalize thread-local writers
                    for (auto& writer : local_writers) {
                        writer.finalize();
                    }
                });
            }
            
            // Wait for all threads to finish probing
            for (auto& thread : threads) {
                thread.join();
            }
            
            // MERGE PHASE: Single thread gathers all results
            for (auto [col_idx, type] : output_attrs) {
                results.emplace_back(type);
            }
            
            // Gather pages from all threads
            for (size_t col_idx = 0; col_idx < output_attrs.size(); ++col_idx) {
                auto& result_col = results[col_idx];
                
                for (size_t t = 0; t < num_threads; ++t) {
                    auto& thread_col = thread_results[t][col_idx];
                    
                    // Move pages from thread-local to final result
                    for (auto* page : thread_col.pages) {
                        result_col.pages.push_back(page);
                    }
                    result_col.num_rows += thread_col.num_rows;
                    
                    thread_col.pages.clear();
                }
            }
            
        } else {
            // SEQUENTIAL PROBING
            
            for (auto [col_idx, type] : output_attrs) {
                results.emplace_back(type);
            }
            
            std::vector<ColumnWriter> writers;
            for (auto& col : results) {
                writers.emplace_back(col);
            }
            
            for (size_t probe_idx = 0; probe_idx < num_probe_rows; ++probe_idx) {
                const auto& probe_val = probe_col[probe_idx];
                if (probe_val.is_null) continue;
                
                T key = *reinterpret_cast<const T*>(&probe_val.int_val);
                
                hash_table.probe(static_cast<int32_t>(key), [&](size_t build_idx) {
                    for (size_t out_idx = 0; out_idx < output_attrs.size(); ++out_idx) {
                        auto [col_idx, type] = output_attrs[out_idx];
                        
                        const auto& val = (col_idx < left_cols.size()) ? 
                            left_cols[col_idx][build_left ? build_idx : probe_idx] :
                            right_cols[col_idx - left_cols.size()][build_left ? probe_idx : build_idx];
                        
                        if (type == DataType::INT32) {
                            writers[out_idx].write_int32(val.int_val, val.is_null);
                        } else {
                            writers[out_idx].write_varchar(val, plan);
                        }
                    }
                });
            }
            
            for (auto& writer : writers) {
                writer.finalize();
            }
        }
    }
};

ExecuteResult execute_impl(const Plan& plan, size_t node_idx, void* context);

ExecuteResult execute_hash_join(const Plan& plan,
    const JoinNode& join,
    const std::vector<std::tuple<size_t, DataType>>& output_attrs,
    void* context) {
    
    auto left = execute_impl(plan, join.left, context);
    auto right = execute_impl(plan, join.right, context);
    
    ExecuteResult results;
    
    auto& left_types = plan.nodes[join.left].output_attrs;
    auto& right_types = plan.nodes[join.right].output_attrs;
    
    JoinAlgorithm join_algorithm{
        .build_left = join.build_left,
        .left = left,
        .right = right,
        .results = results,
        .left_col = join.left_attr,
        .right_col = join.right_attr,
        .output_attrs = output_attrs,
        .plan = plan,
        .context = context
    };
    
    if (join.build_left) {
        switch (std::get<1>(left_types[join.left_attr])) {
        case DataType::INT32:   join_algorithm.run<int32_t>(); break;
        case DataType::INT64:   join_algorithm.run<int64_t>(); break;
        case DataType::FP64:    join_algorithm.run<double>(); break;
        case DataType::VARCHAR: break;
        }
    } else {
        switch (std::get<1>(right_types[join.right_attr])) {
        case DataType::INT32:   join_algorithm.run<int32_t>(); break;
        case DataType::INT64:   join_algorithm.run<int64_t>(); break;
        case DataType::FP64:    join_algorithm.run<double>(); break;
        case DataType::VARCHAR: break;
        }
    }
    
    return results;
}

ExecuteResult execute_scan(const Plan& plan,
    const ScanNode& scan,
    const std::vector<std::tuple<size_t, DataType>>& output_attrs) {
    
    auto table_id = scan.base_table_id;
    auto& input = plan.inputs[table_id];
    
    ExecuteResult results;
    for (auto [in_col_idx, col_type] : output_attrs) {
        results.emplace_back(col_type);
    }
    
    // for INT32 columns without NULLs, reference pages directly
    for (size_t out_col_idx = 0; out_col_idx < output_attrs.size(); ++out_col_idx) {
        auto [in_col_idx, col_type] = output_attrs[out_col_idx];
        auto& input_column = input.columns[in_col_idx];
        auto& result_column = results[out_col_idx];
        
        // check if it is an INT32 column without nuls and has mapped_memory
        if (col_type == DataType::INT32 && 
            !column_has_nulls(input_column) && 
            input_column.mapped_memory != nullptr) {
            
            // direct reference, no copying
            for (auto* page : input_column.pages) {
                result_column.pages.push_back(page);
                
                auto* page_data = page->data;
                uint16_t num_rows = *reinterpret_cast<const uint16_t*>(page_data);
                result_column.num_rows += num_rows;
            }
            
            result_column.mapped_memory = input_column.mapped_memory;
            result_column.mapped_memory->refs++;
            
        } else {
            // copy the data
            ColumnWriter writer(result_column);
            
            if (col_type == DataType::INT32) {
                for (auto* page : input_column.pages) {
                    auto* page_data = page->data;
                    uint16_t num_rows = *reinterpret_cast<const uint16_t*>(page_data);
                    
                    size_t bitmap_size = (num_rows + 7) / 8;
                    auto* bitmap = reinterpret_cast<const uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
                    auto* data_begin = reinterpret_cast<const int32_t*>(page_data + 4);
                    
                    uint16_t data_idx = 0;
                    for (uint16_t row = 0; row < num_rows; ++row) {
                        bool has_value = (bitmap[row / 8] >> (row % 8)) & 1;
                        if (has_value) {
                            writer.write_int32(data_begin[data_idx++], false);
                        } else {
                            writer.write_int32(0, true);
                        }
                    }
                }
            } else if (col_type == DataType::VARCHAR) { 
                for (size_t page_idx = 0; page_idx < input_column.pages.size(); ++page_idx) {
                    auto* page = input_column.pages[page_idx];
                    auto* page_data = page->data;
                    uint16_t num_rows = *reinterpret_cast<const uint16_t*>(page_data);
                    
                    if (num_rows == 0xffff) {
                        VarcharRef ref = make_varchar_ref(
                            static_cast<uint16_t>(table_id),
                            static_cast<uint16_t>(in_col_idx),
                            static_cast<uint32_t>(page_idx),
                            0
                        );
                        writer.write_varchar(value_t(ref), plan);
                    } else if (num_rows == 0xfffe) {
                        continue;  
                    } else {
                        size_t bitmap_size = (num_rows + 7) / 8;
                        auto* bitmap = reinterpret_cast<const uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
                        
                        uint16_t data_idx = 0;
                        for (uint16_t row = 0; row < num_rows; ++row) {
                            bool has_value = (bitmap[row / 8] >> (row % 8)) & 1;
                            if (has_value) {
                                VarcharRef ref = make_varchar_ref(
                                    static_cast<uint16_t>(table_id),
                                    static_cast<uint16_t>(in_col_idx),
                                    static_cast<uint32_t>(page_idx),
                                    static_cast<uint16_t>(data_idx)
                                );
                                writer.write_varchar(value_t(ref), plan);
                                data_idx++;
                            } else {
                                writer.write_varchar(value_t(NULL_VARCHAR_REF), plan);
                            }
                        }
                    }
                }
            }
            
            writer.finalize();
        }
    }
    
    return results;
}

ExecuteResult execute_impl(const Plan& plan, size_t node_idx, void* context) {
    auto& node = plan.nodes[node_idx];
    return std::visit(
        [&](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, JoinNode>) {
                return execute_hash_join(plan, value, node.output_attrs, context);
            } else {
                return execute_scan(plan, value, node.output_attrs);
            }
        },
        node.data);
}

ColumnarTable execute(const Plan& plan, void* context) {
    auto result_columns = execute_impl(plan, plan.root, context);
    
    ColumnarTable result;
    result.num_rows = result_columns.empty() ? 0 : result_columns[0].num_rows;
    
    for (auto& col : result_columns) {
        if (col.type == DataType::VARCHAR) {
            Column output_col(col.type);
            
            std::vector<std::pair<VarcharRef, bool>> refs;
            refs.reserve(col.num_rows);
            
            for (auto* page : col.pages) {
                auto* page_data = page->data;
                uint16_t num_rows = *reinterpret_cast<uint16_t*>(page_data);
                
                size_t bitmap_size = (num_rows + 7) / 8;
                auto* bitmap = reinterpret_cast<uint8_t*>(page_data + PAGE_SIZE - bitmap_size);
                auto* data = reinterpret_cast<uint64_t*>(page_data + 8);
                
                uint16_t data_idx = 0;
                for (uint16_t row = 0; row < num_rows; ++row) {
                    bool has_value = (bitmap[row / 8] >> (row % 8)) & 1;
                    if (has_value) {
                        refs.emplace_back(data[data_idx++], false);
                    } else {
                        refs.emplace_back(NULL_VARCHAR_REF, true);
                    }
                }
            }
            
            std::vector<std::pair<std::string, bool>> page_strings;
            page_strings.reserve(500);
            
            for (const auto& [ref, is_null] : refs) {
                std::string value;
                if (!is_null) {
                    value = materialize_varchar(ref, plan);
                }
                
                if (!is_null && value.size() > (PAGE_SIZE - 100)) {
                    if (!page_strings.empty()) {
                        Page* page = new Page();
                        write_varchar_page(page, page_strings);
                        output_col.pages.push_back(page);
                        page_strings.clear();
                    }
                    write_long_varchar(output_col.pages, value);
                    continue;
                }
                
                page_strings.emplace_back(value, is_null);
                
                uint16_t total_offset = 0, total_non_null = 0;
                for (const auto& [s, n] : page_strings) {
                    if (!n) {
                        total_offset += s.size();
                        ++total_non_null;
                    }
                }
                
                uint16_t bitmap_size = (page_strings.size() + 7) / 8;
                bool is_full = (4 + total_non_null * 2 + total_offset + bitmap_size > PAGE_SIZE) ||
                              (page_strings.size() >= 65535) || (total_non_null >= 32767);
                
                if (is_full && page_strings.size() > 1) {
                    page_strings.pop_back();
                    Page* page = new Page();
                    write_varchar_page(page, page_strings);
                    output_col.pages.push_back(page);
                    page_strings.clear();
                    page_strings.emplace_back(value, is_null);
                }
            }
            
            if (!page_strings.empty()) {
                Page* page = new Page();
                write_varchar_page(page, page_strings);
                output_col.pages.push_back(page);
            }
            
            result.columns.push_back(std::move(output_col));
        } else {
            
            Column output_col(col.type);  // For INT32 columns, transfer pages to Column
            
            for (auto* page : col.pages) {    // Transfer pages and mapped_memory
                output_col.pages.push_back(page);
            }
            
            if (col.mapped_memory != nullptr) {
                output_col.mapped_memory = col.mapped_memory;
                col.mapped_memory = nullptr;
            }
            
            col.pages.clear();
            result.columns.push_back(std::move(output_col));
        }
    }
    
    return result;
}

void* build_context() {
    return new ExecutionContext();
}

void destroy_context(void* context) {
    if (context) {
        delete static_cast<ExecutionContext*>(context);
    }
}

} // namespace Contest
