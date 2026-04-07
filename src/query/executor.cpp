#include "../../include/query/executor.h"
#include "../../include/storage/record.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <cstdlib>

namespace flexql {

Executor::Executor(Catalog* catalog, BufferPool* pool) : catalog_(catalog), pool_(pool) {}

Value Executor::parse_value(const std::string& raw_val, ColumnType type, bool is_string) const {
    const char* start = raw_val.data();
    size_t len = raw_val.size();

    // Strip quotes if string literal
    if (is_string && len >= 2 && start[0] == '\'' && start[len-1] == '\'') {
        start++;
        len -= 2;
    }

    if (len == 4 && memcmp(start, "NULL", 4) == 0) return Value::make_null();

    try {
        switch (type) {
            case ColumnType::INT: {
                // Fast integer parsing without string allocation
                int64_t result = 0;
                bool negative = false;
                size_t i = 0;
                if (len > 0 && start[0] == '-') {
                    negative = true;
                    i = 1;
                }
                for (; i < len; i++) {
                    char c = start[i];
                    if (c >= '0' && c <= '9') {
                        result = result * 10 + (c - '0');
                    }
                }
                return Value::make_int(negative ? -result : result);
            }
            case ColumnType::DECIMAL: {
                // Fast double parsing
                double result = 0.0;
                double fraction = 0.0;
                double div = 1.0;
                bool negative = false;
                bool in_fraction = false;
                size_t i = 0;
                if (len > 0 && start[0] == '-') {
                    negative = true;
                    i = 1;
                }
                for (; i < len; i++) {
                    char c = start[i];
                    if (c >= '0' && c <= '9') {
                        if (in_fraction) {
                            div *= 10.0;
                            fraction += (c - '0') / div;
                        } else {
                            result = result * 10.0 + (c - '0');
                        }
                    } else if (c == '.') {
                        in_fraction = true;
                    }
                }
                result += fraction;
                return Value::make_decimal(negative ? -result : result);
            }
            case ColumnType::VARCHAR:
            case ColumnType::TEXT: 
                return Value::make_varchar(std::string(start, len));
            case ColumnType::DATETIME: {
                // Fast integer parsing for timestamps
                int64_t result = 0;
                bool negative = false;
                size_t i = 0;
                if (len > 0 && start[0] == '-') {
                    negative = true;
                    i = 1;
                }
                for (; i < len; i++) {
                    char c = start[i];
                    if (c >= '0' && c <= '9') {
                        result = result * 10 + (c - '0');
                    }
                }
                return Value::make_datetime(negative ? -result : result);
            }
            default: return Value::make_null();
        }
    } catch (...) { return Value::make_null(); }
}

QueryResult Executor::execute(const Statement& stmt) {
    switch (stmt.type) {
        case StmtType::CREATE_TABLE: return execute_create_table(stmt.create_table);
        case StmtType::INSERT: return execute_insert(stmt.insert);
        case StmtType::SELECT: return execute_select(stmt.select);
        case StmtType::DELETE_FROM: return execute_delete(stmt.delete_from);
        default: return QueryResult::err("Unsupported statement type");
    }
}

QueryResult Executor::execute_create_table(const CreateTableStmt& stmt) {
    TableSchema schema;
    schema.name = stmt.table_name;
    schema.columns = stmt.columns;
    std::string error;
    if (catalog_->create_table(schema, stmt.if_not_exists, error)) return QueryResult::ok();
    return QueryResult::err(error);
}

QueryResult Executor::execute_insert(const InsertStmt& stmt) {
    TableInfo* table = catalog_->get_table(stmt.table_name);
    if (!table) return QueryResult::err("Table not found or corrupted");
    try {
        std::lock_guard<std::mutex> lock(table->rw_mutex);

        int expires_at_idx = -1;
        const size_t num_cols = table->schema.columns.size();
        
        // Pre-fetch column types for faster parsing
        std::vector<ColumnType> col_types(num_cols);
        for (size_t i = 0; i < num_cols; i++) {
            col_types[i] = table->schema.columns[i].type;
            if (table->schema.columns[i].name == "EXPIRES_AT") {
                expires_at_idx = (int)i;
            }
        }

        std::vector<uint8_t> serialize_buf;
        serialize_buf.reserve(512);  // Larger initial reserve
        PageId current_pid = table->last_data_page;
        Page* current_page = pool_->fetch_page(table->data_pager.get(), current_pid);
        
        // Lock index once for entire batch
        const bool has_pk = table->schema.primary_key_index >= 0;
        if (has_pk) {
            table->index->lock();
        }
        
        // Pre-allocate Record values vector
        Record record;
        record.values.resize(num_cols);  // Pre-size to avoid reallocations

        for (const auto& row : stmt.value_rows) {
            record.expiration_ts = 0;
            const size_t row_size = std::min(row.size(), num_cols);
            for (size_t i = 0; i < row_size; i++) {
                const std::string& val_str = row[i];
                bool is_str = (val_str.size() >= 2 && val_str.front() == '\'' && val_str.back() == '\'');
                record.values[i] = parse_value(val_str, col_types[i], is_str);
                if ((int)i == expires_at_idx) record.expiration_ts = (int64_t)record.values[i].to_double();
            }
            insert_record_into_table_unlocked(table, record, serialize_buf, current_pid, current_page, has_pk);
        }
        
        if (has_pk) {
            table->index->unlock();
        }

        // Final unpin of whatever page was left active
        if (current_page != nullptr) {
            pool_->mark_dirty(table->data_pager.get(), current_pid);
            pool_->unpin(table->data_pager.get(), current_pid);
        }

        // Durability is ensured by:
        // 1. LRU eviction: dirty pages are flushed to disk when evicted
        // 2. Server shutdown: Catalog destructor flushes all dirty pages
        // No explicit flush here for maximum throughput
    } catch (const std::exception& e) {
        return QueryResult::err(std::string("Insert failed: ") + e.what());
    }
    return QueryResult::ok();
}

void Executor::insert_record_into_table(TableInfo* table, const Record& record, std::vector<uint8_t>& serialize_buf, PageId& current_pid, Page*& current_page) {
    insert_record_into_table_unlocked(table, record, serialize_buf, current_pid, current_page, table->schema.primary_key_index >= 0);
}

void Executor::insert_record_into_table_unlocked(TableInfo* table, const Record& record, std::vector<uint8_t>& serialize_buf, PageId& current_pid, Page*& current_page, bool has_pk) {
    RecordSerializer::serialize(record, table->schema, serialize_buf);

    if (current_page->get_free_space() < (uint16_t)serialize_buf.size() + 4) {
        PageId new_pid = table->data_pager->allocate_page();
        Page* new_page = pool_->fetch_page(table->data_pager.get(), new_pid);
        new_page->init(new_pid, PageType::TABLE_DATA);

        current_page->set_next_page_id(new_pid);
        pool_->mark_dirty(table->data_pager.get(), current_pid);
        pool_->unpin(table->data_pager.get(), current_pid);

        table->last_data_page = new_pid;
        current_pid = new_pid;
        current_page = new_page;
    }

    int slot_idx = current_page->insert_record(serialize_buf.data(), (uint16_t)serialize_buf.size());
    table->row_count++;

    if (has_pk && (size_t)table->schema.primary_key_index < record.values.size() && slot_idx >= 0) {
        table->index->insert_unlocked(record.values[table->schema.primary_key_index].to_double(), {current_pid, (uint16_t)slot_idx});
    }
}





std::vector<Record> Executor::scan_table(TableInfo* table) {
    std::vector<Record> results;
    results.reserve(table->row_count > 0 ? table->row_count : 1000);
    PageId cur = table->first_data_page;
    while (cur != INVALID_PAGE_ID) {
        Page* page = pool_->fetch_page(table->data_pager.get(), cur);
        uint16_t num = page->get_num_records();
        for (uint16_t i = 0; i < num; i++) {
            uint16_t rlen;
            const uint8_t* rdata = page->get_record(i, rlen);
            if (!rdata) continue;
            Record rec = RecordSerializer::deserialize(rdata, rlen, table->schema);
            results.push_back(std::move(rec));
        }
        PageId next = page->get_next_page_id();
        pool_->unpin(table->data_pager.get(), cur);
        cur = next;
    }
    return results;
}

bool Executor::can_use_index(TableInfo* table, const Condition& cond, int& pk_idx) const {
    if (table->schema.primary_key_index < 0) return false;
    pk_idx = table->schema.primary_key_index;
    std::string target_col = cond.column;
    size_t dot_pos = target_col.find('.');
    if (dot_pos != std::string::npos) {
        target_col = target_col.substr(dot_pos + 1);
    }
    return table->schema.columns[pk_idx].name == target_col;
}

std::vector<Record> Executor::scan_table_with_index(TableInfo* table, const Condition& cond) {
    std::vector<Record> results;
    int pk_idx = table->schema.primary_key_index;
    Value cond_val = parse_value(cond.value, table->schema.columns[pk_idx].type, cond.value_is_string);
    double key = cond_val.to_double();

    if (cond.op == CompOp::EQ) {
        RecordPtr ptr;
        if (table->index->search(key, ptr) && ptr.page_id != INVALID_PAGE_ID) {
            Page* page = pool_->fetch_page(table->data_pager.get(), ptr.page_id);
            uint16_t rlen;
            const uint8_t* rdata = page->get_record(ptr.slot_idx, rlen);
            if (rdata) {
                results.push_back(RecordSerializer::deserialize(rdata, rlen, table->schema));
            }
            pool_->unpin(table->data_pager.get(), ptr.page_id);
        }
    } else {
        double low = (cond.op == CompOp::GT || cond.op == CompOp::GE) ? key : -1e308;
        double high = (cond.op == CompOp::LT || cond.op == CompOp::LE) ? key : 1e308;
        auto ptrs = table->index->range_scan(low, high);
        results.reserve(ptrs.size());
        for (const auto& ptr : ptrs) {
            if (ptr.page_id == INVALID_PAGE_ID) continue;
            Page* page = pool_->fetch_page(table->data_pager.get(), ptr.page_id);
            uint16_t rlen;
            const uint8_t* rdata = page->get_record(ptr.slot_idx, rlen);
            if (rdata) {
                Record rec = RecordSerializer::deserialize(rdata, rlen, table->schema);
                bool include = false;
                switch (cond.op) {
                    case CompOp::GT: include = rec.values[pk_idx].to_double() > key; break;
                    case CompOp::GE: include = rec.values[pk_idx].to_double() >= key; break;
                    case CompOp::LT: include = rec.values[pk_idx].to_double() < key; break;
                    case CompOp::LE: include = rec.values[pk_idx].to_double() <= key; break;
                    default: include = true; break;
                }
                if (include) results.push_back(std::move(rec));
            }
            pool_->unpin(table->data_pager.get(), ptr.page_id);
        }
    }
    return results;
}

bool Executor::evaluate_condition(const Record& record, const TableSchema& schema, const Condition& cond) const {
    std::string target_col = cond.column;
    size_t dot_pos = target_col.find('.');
    if (dot_pos != std::string::npos) {
        target_col = target_col.substr(dot_pos + 1);
    }
    
    int col_idx = -1;
    for (size_t i = 0; i < schema.columns.size(); i++) {
        if (schema.columns[i].name == target_col) {
            col_idx = (int)i;
            break;
        }
    }
    if (col_idx < 0 || col_idx >= (int)record.values.size()) return false;
    
    const Value& lhs = record.values[col_idx];
    Value rhs = parse_value(cond.value, schema.columns[col_idx].type, cond.value_is_string);
    
    int cmp = lhs.compare(rhs);
    switch (cond.op) {
        case CompOp::EQ: return cmp == 0;
        case CompOp::LT: return cmp < 0;
        case CompOp::GT: return cmp > 0;
        case CompOp::LE: return cmp <= 0;
        case CompOp::GE: return cmp >= 0;
        default: return false;
    }
}

QueryResult Executor::execute_select(const SelectStmt& stmt) {
    TableInfo* table = catalog_->get_table(stmt.table_name);
    if (!table) return QueryResult::err("Table not found");

    std::lock_guard<std::mutex> lock(table->rw_mutex);
    QueryResult res;
    res.success = true;

    // We need working state that tracks the combined schema for projection / ordering
    TableSchema combined_schema = table->schema;

    // Optimization: Use index for WHERE on primary key when no JOIN
    int pk_idx = -1;
    bool use_index = !stmt.has_join && stmt.has_where && can_use_index(table, stmt.where_cond, pk_idx);

    std::vector<Record> working_set;
    if (use_index) {
        working_set = scan_table_with_index(table, stmt.where_cond);
    } else {
        working_set = scan_table(table);
    }

    // 1. JOIN - Use hash join for O(n+m) instead of O(n*m)
    if (stmt.has_join) {
        TableInfo* join_table = catalog_->get_table(stmt.join.join_table);
        if (!join_table) return QueryResult::err("Join table not found");

        std::lock_guard<std::mutex> join_lock(join_table->rw_mutex);
        std::vector<Record> join_records = scan_table(join_table);

        // Resolve JOIN column indexes
        std::string l_col = stmt.join.left_column;
        if (l_col.find('.') != std::string::npos) l_col = l_col.substr(l_col.find('.') + 1);
        std::string r_col = stmt.join.right_column;
        if (r_col.find('.') != std::string::npos) r_col = r_col.substr(r_col.find('.') + 1);

        int l_idx = -1, r_idx = -1;
        for (size_t i = 0; i < table->schema.columns.size(); i++) {
            if (table->schema.columns[i].name == l_col) l_idx = (int)i;
        }
        for (size_t i = 0; i < join_table->schema.columns.size(); i++) {
            if (join_table->schema.columns[i].name == r_col) r_idx = (int)i;
        }

        // Hash join: build hash table on smaller relation, probe with larger
        std::vector<Record> joined_set;
        const size_t left_cols = table->schema.columns.size();
        const size_t right_cols = join_table->schema.columns.size();
        const size_t combined_cols = left_cols + right_cols;
        
        if (l_idx >= 0 && r_idx >= 0) {
            bool build_on_right = join_records.size() < working_set.size();
            // Estimate result size for better memory allocation
            size_t estimated_matches = std::min(working_set.size(), join_records.size());
            joined_set.reserve(estimated_matches);

            if (build_on_right) {
                // Build hash table on right (join_records)
                std::unordered_multimap<double, size_t> hash_table;
                hash_table.reserve(join_records.size());
                for (size_t i = 0; i < join_records.size(); i++) {
                    if (r_idx < (int)join_records[i].values.size()) {
                        hash_table.emplace(join_records[i].values[r_idx].to_double(), i);
                    }
                }
                // Probe with left (working_set)
                for (auto& lr : working_set) {
                    if (l_idx < (int)lr.values.size()) {
                        double key = lr.values[l_idx].to_double();
                        auto range = hash_table.equal_range(key);
                        for (auto it = range.first; it != range.second; ++it) {
                            Record combined;
                            combined.values.reserve(combined_cols);
                            combined.values = std::move(lr.values);
                            const auto& jr = join_records[it->second];
                            combined.values.insert(combined.values.end(),
                                jr.values.begin(), jr.values.end());
                            joined_set.push_back(std::move(combined));
                            // Restore lr.values for next match
                            lr.values = joined_set.back().values;
                            lr.values.resize(left_cols);
                        }
                    }
                }
            } else {
                // Build hash table on left (working_set)
                std::unordered_multimap<double, size_t> hash_table;
                hash_table.reserve(working_set.size());
                for (size_t i = 0; i < working_set.size(); i++) {
                    if (l_idx < (int)working_set[i].values.size()) {
                        hash_table.emplace(working_set[i].values[l_idx].to_double(), i);
                    }
                }
                // Probe with right (join_records)
                for (const auto& rr : join_records) {
                    if (r_idx < (int)rr.values.size()) {
                        double key = rr.values[r_idx].to_double();
                        auto range = hash_table.equal_range(key);
                        for (auto it = range.first; it != range.second; ++it) {
                            Record combined;
                            combined.values.reserve(combined_cols);
                            combined.values = working_set[it->second].values;
                            combined.values.insert(combined.values.end(), rr.values.begin(), rr.values.end());
                            joined_set.push_back(std::move(combined));
                        }
                    }
                }
            }
        }
        working_set = std::move(joined_set);
        combined_schema.columns.insert(combined_schema.columns.end(), join_table->schema.columns.begin(), join_table->schema.columns.end());
    }

    // 2. WHERE (skip if already applied via index)
    if (stmt.has_where && !use_index) {
        std::vector<Record> filtered_set;
        filtered_set.reserve(working_set.size() / 2);
        for (auto& row : working_set) {
            if (evaluate_condition(row, combined_schema, stmt.where_cond)) {
                filtered_set.push_back(std::move(row));
            }
        }
        working_set = std::move(filtered_set);
    }

    // 3. ORDER BY
    if (stmt.has_order_by) {
        std::string o_col = stmt.order_by.column;
        if (o_col.find('.') != std::string::npos) o_col = o_col.substr(o_col.find('.') + 1);

        int o_idx = -1;
        for (size_t i = 0; i < combined_schema.columns.size(); i++) {
            if (combined_schema.columns[i].name == o_col) {
                o_idx = (int)i; break;
            }
        }

        if (o_idx >= 0) {
            std::sort(working_set.begin(), working_set.end(), [o_idx, &stmt](const Record& a, const Record& b) {
                if (o_idx >= (int)a.values.size() || o_idx >= (int)b.values.size()) return false;
                int cmp = a.values[o_idx].compare(b.values[o_idx]);
                return stmt.order_by.direction == OrderDir::ASC ? (cmp < 0) : (cmp > 0);
            });
        }
    }


    // 4. PROJECTION
    std::vector<std::string> target_cols = stmt.columns;
    if (stmt.select_all || target_cols.empty()) {
        target_cols.clear();
        for (const auto& c : table->schema.columns) target_cols.push_back(c.name);
    }
    res.column_names = target_cols;

    // Pre-validate columns so we can return error early
    std::vector<int> col_indices;
    col_indices.reserve(target_cols.size());
    for (const auto& tcol : target_cols) {
        std::string fetch_col = tcol;
        if (fetch_col.find('.') != std::string::npos) fetch_col = fetch_col.substr(fetch_col.find('.') + 1);
        int c_idx = -1;
        for (size_t i = 0; i < combined_schema.columns.size(); i++) {
            if (combined_schema.columns[i].name == fetch_col) {
                c_idx = (int)i; break;
            }
        }
        if (c_idx == -1) {
            return QueryResult::err("Unknown column: " + tcol);
        }
        col_indices.push_back(c_idx);
    }

    res.rows.reserve(working_set.size());
    for (const auto& row : working_set) {
        std::vector<std::string> out_row;
        out_row.reserve(col_indices.size());
        for (int c_idx : col_indices) {
            if (c_idx >= 0 && c_idx < (int)row.values.size()) {
                out_row.push_back(row.values[c_idx].to_string());
            } else {
                out_row.push_back("NULL");
            }
        }
        res.rows.push_back({std::move(out_row)});
    }

    return res;
}

QueryResult Executor::execute_delete(const DeleteStmt& stmt) {
    TableInfo* table = catalog_->get_table(stmt.table_name);
    if (!table) return QueryResult::err("Table not found");
    std::lock_guard<std::mutex> lock(table->rw_mutex);
    
    if (!stmt.has_where) {
        pool_->flush_all(table->data_pager.get());
        pool_->flush_all(table->index_pager.get());
        std::string df = table->data_pager->get_filename();
        std::string ifile = table->index_pager->get_filename();
        table->index_pager.reset();
        table->data_pager.reset();
        std::remove(df.c_str());
        std::remove(ifile.c_str());
        table->data_pager = std::make_unique<Pager>(df);
        table->index_pager = std::make_unique<Pager>(ifile);
        table->index = std::make_unique<BPlusTree>(table->index_pager.get(), pool_);
        table->index->init();
        PageId first = table->data_pager->allocate_page();
        Page page; page.init(first, PageType::TABLE_DATA);
        table->data_pager->write_page(first, page);
        table->first_data_page = first;
        table->last_data_page = first;
        table->row_count = 0;
        return QueryResult::ok();
    }
    
    std::vector<Record> records = scan_table(table);
    
    pool_->flush_all(table->data_pager.get());
    pool_->flush_all(table->index_pager.get());
    std::string df = table->data_pager->get_filename();
    std::string ifile = table->index_pager->get_filename();
    table->index_pager.reset();
    table->data_pager.reset();
    std::remove(df.c_str());
    std::remove(ifile.c_str());
    table->data_pager = std::make_unique<Pager>(df);
    table->index_pager = std::make_unique<Pager>(ifile);
    table->index = std::make_unique<BPlusTree>(table->index_pager.get(), pool_);
    table->index->init();
    
    PageId first = table->data_pager->allocate_page();
    Page page; page.init(first, PageType::TABLE_DATA);
    table->data_pager->write_page(first, page);
    table->first_data_page = first;
    table->last_data_page = first;
    table->row_count = 0;
    
    std::vector<uint8_t> serialize_buf;
    PageId current_pid = table->last_data_page;
    Page* current_page = pool_->fetch_page(table->data_pager.get(), current_pid);

    for (const auto& r : records) {
        if (!evaluate_condition(r, table->schema, stmt.where_cond)) {
            insert_record_into_table(table, r, serialize_buf, current_pid, current_page);
        }
    }
    
    if (current_page != nullptr) {
        pool_->mark_dirty(table->data_pager.get(), current_pid);
        pool_->unpin(table->data_pager.get(), current_pid);
    }
    
    return QueryResult::ok();
}

} // namespace flexql
