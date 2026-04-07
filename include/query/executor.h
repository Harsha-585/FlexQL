#ifndef FLEXQL_QUERY_EXECUTOR_H
#define FLEXQL_QUERY_EXECUTOR_H

#include "../parser/ast.h"
#include "../storage/catalog.h"
#include "../cache/lru_cache.h"
#include <string>
#include <vector>
#include <functional>

namespace flexql {

// Result row: column names + values as strings
struct ResultRow {
    std::vector<std::string> values;
};

struct QueryResult {
    bool success;
    std::string error;
    std::vector<std::string> column_names;
    std::vector<ResultRow> rows;
    
    QueryResult() : success(true) {}
    
    static QueryResult ok() {
        QueryResult r;
        r.success = true;
        return r;
    }
    static QueryResult err(const std::string& msg) {
        QueryResult r;
        r.success = false;
        r.error = msg;
        return r;
    }
};

class Executor {
public:
    Executor(Catalog* catalog, BufferPool* pool);
    
    QueryResult execute(const Statement& stmt);
    
private:
    Catalog* catalog_;
    BufferPool* pool_;
    
    QueryResult execute_create_table(const CreateTableStmt& stmt);
    QueryResult execute_insert(const InsertStmt& stmt);
    QueryResult execute_select(const SelectStmt& stmt);
    QueryResult execute_delete(const DeleteStmt& stmt);
    
    // Helpers
    Value parse_value(const std::string& str_val, ColumnType type, bool is_string) const;
    bool evaluate_condition(const Record& record, const TableSchema& schema,
                           const Condition& cond) const;
    std::vector<Record> scan_table(TableInfo* table);
    std::vector<Record> scan_table_with_index(TableInfo* table, const Condition& cond);
    bool can_use_index(TableInfo* table, const Condition& cond, int& pk_idx) const;
    void insert_record_into_table(TableInfo* table, const Record& record, std::vector<uint8_t>& serialize_buf, PageId& current_pid, Page*& current_page);
    void insert_record_into_table_unlocked(TableInfo* table, const Record& record, std::vector<uint8_t>& serialize_buf, PageId& current_pid, Page*& current_page, bool has_pk);
};

} // namespace flexql

#endif
