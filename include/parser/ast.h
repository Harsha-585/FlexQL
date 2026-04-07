#ifndef FLEXQL_PARSER_AST_H
#define FLEXQL_PARSER_AST_H

#include "../common/types.h"
#include <string>
#include <vector>
#include <memory>

namespace flexql {

enum class StmtType {
    CREATE_TABLE,
    INSERT,
    SELECT,
    DELETE_FROM
};

enum class OrderDir {
    ASC,
    DESC
};

struct Condition {
    std::string column;     // may be table.column
    CompOp op;
    std::string value;      // string representation
    bool value_is_string;   // true if value was quoted
    
    Condition() : op(CompOp::EQ), value_is_string(false) {}
};

struct JoinClause {
    std::string join_table;
    std::string left_column;   // table1.col
    std::string right_column;  // table2.col
};

struct OrderByClause {
    std::string column;
    OrderDir direction;
    
    OrderByClause() : direction(OrderDir::ASC) {}
};

struct CreateTableStmt {
    std::string table_name;
    std::vector<ColumnDef> columns;
    bool if_not_exists;
    
    CreateTableStmt() : if_not_exists(false) {}
};

struct InsertStmt {
    std::string table_name;
    std::vector<std::vector<std::string>> value_rows; // batch of value tuples
    std::vector<bool> is_string_flags;                // per-value: was it quoted?
};

struct SelectStmt {
    std::vector<std::string> columns;   // empty = *, may contain table.column
    bool select_all;                     // SELECT *
    std::string table_name;
    bool has_where;
    Condition where_cond;
    bool has_join;
    JoinClause join;
    bool has_order_by;
    OrderByClause order_by;
    
    SelectStmt() : select_all(false), has_where(false), has_join(false), has_order_by(false) {}
};

struct DeleteStmt {
    std::string table_name;
    bool has_where;
    Condition where_cond;
    
    DeleteStmt() : has_where(false) {}
};

struct Statement {
    StmtType type;
    CreateTableStmt create_table;
    InsertStmt insert;
    SelectStmt select;
    DeleteStmt delete_from;
};

} // namespace flexql

#endif
