#ifndef FLEXQL_COMMON_TYPES_H
#define FLEXQL_COMMON_TYPES_H

#include <cstdint>
#include <string>
#include <variant>
#include <vector>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace flexql {

// Page configuration
constexpr uint32_t PAGE_SIZE = 4096;
constexpr uint32_t MAX_PAGES_IN_CACHE = 262144; // 1GB buffer pool
constexpr uint16_t BPTREE_ORDER = 128;        // B+ tree order
constexpr int DEFAULT_PORT = 9000;
constexpr int THREAD_POOL_SIZE = 8;

// Page types
enum class PageType : uint8_t {
    FREE = 0,
    TABLE_DATA = 1,
    BPTREE_INTERNAL = 2,
    BPTREE_LEAF = 3,
    CATALOG = 4,
    PAGE_OVERFLOW = 5
};

// Column types
enum class ColumnType : uint8_t {
    INT = 1,
    DECIMAL = 2,
    VARCHAR = 3,
    DATETIME = 4,
    TEXT = 5     // alias for VARCHAR(255)
};

// A value in a record
struct Value {
    ColumnType type;
    bool is_null;
    
    union {
        int64_t int_val;
        double decimal_val;
        int64_t datetime_val;
    };
    std::string str_val;  // Only used for VARCHAR/TEXT
    
    Value() : type(ColumnType::INT), is_null(true), int_val(0) {}
    
    // Copy/move for proper string handling
    Value(const Value& other) : type(other.type), is_null(other.is_null), str_val(other.str_val) {
        if (!is_null) {
            switch (type) {
                case ColumnType::INT: int_val = other.int_val; break;
                case ColumnType::DECIMAL: decimal_val = other.decimal_val; break;
                case ColumnType::DATETIME: datetime_val = other.datetime_val; break;
                default: break;
            }
        }
    }
    
    Value(Value&& other) noexcept : type(other.type), is_null(other.is_null), str_val(std::move(other.str_val)) {
        if (!is_null) {
            switch (type) {
                case ColumnType::INT: int_val = other.int_val; break;
                case ColumnType::DECIMAL: decimal_val = other.decimal_val; break;
                case ColumnType::DATETIME: datetime_val = other.datetime_val; break;
                default: break;
            }
        }
    }
    
    Value& operator=(const Value& other) {
        if (this != &other) {
            type = other.type;
            is_null = other.is_null;
            str_val = other.str_val;
            if (!is_null) {
                switch (type) {
                    case ColumnType::INT: int_val = other.int_val; break;
                    case ColumnType::DECIMAL: decimal_val = other.decimal_val; break;
                    case ColumnType::DATETIME: datetime_val = other.datetime_val; break;
                    default: break;
                }
            }
        }
        return *this;
    }
    
    Value& operator=(Value&& other) noexcept {
        if (this != &other) {
            type = other.type;
            is_null = other.is_null;
            str_val = std::move(other.str_val);
            if (!is_null) {
                switch (type) {
                    case ColumnType::INT: int_val = other.int_val; break;
                    case ColumnType::DECIMAL: decimal_val = other.decimal_val; break;
                    case ColumnType::DATETIME: datetime_val = other.datetime_val; break;
                    default: break;
                }
            }
        }
        return *this;
    }
    
    static Value make_int(int64_t v) {
        Value val;
        val.type = ColumnType::INT;
        val.is_null = false;
        val.int_val = v;
        return val;
    }
    static Value make_decimal(double v) {
        Value val;
        val.type = ColumnType::DECIMAL;
        val.is_null = false;
        val.decimal_val = v;
        return val;
    }
    static Value make_varchar(const std::string& v) {
        Value val;
        val.type = ColumnType::VARCHAR;
        val.is_null = false;
        val.int_val = 0;  // Initialize union member
        val.str_val = v;
        return val;
    }
    static Value make_varchar(std::string&& v) {
        Value val;
        val.type = ColumnType::VARCHAR;
        val.is_null = false;
        val.int_val = 0;  // Initialize union member
        val.str_val = std::move(v);
        return val;
    }
    static Value make_datetime(int64_t v) {
        Value val;
        val.type = ColumnType::DATETIME;
        val.is_null = false;
        val.datetime_val = v;
        return val;
    }
    static Value make_null() {
        Value val;
        val.is_null = true;
        return val;
    }
    
    // Convert value to string for display
    std::string to_string() const {
        if (is_null) return "NULL";
        switch (type) {
            case ColumnType::INT:
                return std::to_string(int_val);
            case ColumnType::DECIMAL: {
                char buf[32];
                int n = snprintf(buf, sizeof(buf), "%g", decimal_val);
                return std::string(buf, n > 0 ? n : 0);
            }
            case ColumnType::VARCHAR:
            case ColumnType::TEXT:
                return str_val;
            case ColumnType::DATETIME:
                return std::to_string(datetime_val);
        }
        return "NULL";
    }
    
    // Compare values for WHERE clause - optimized to avoid temporary string allocations
    int compare(const Value& other) const {
        if (is_null && other.is_null) return 0;
        if (is_null) return -1;
        if (other.is_null) return 1;

        // handle cross-type numeric comparisons
        double lhs_num = 0, rhs_num = 0;
        bool lhs_is_num = false, rhs_is_num = false;
        
        if (type == ColumnType::INT || type == ColumnType::DATETIME) {
            lhs_num = (type == ColumnType::INT) ? (double)int_val : (double)datetime_val;
            lhs_is_num = true;
        } else if (type == ColumnType::DECIMAL) {
            lhs_num = decimal_val;
            lhs_is_num = true;
        }
        
        if (other.type == ColumnType::INT || other.type == ColumnType::DATETIME) {
            rhs_num = (other.type == ColumnType::INT) ? (double)other.int_val : (double)other.datetime_val;
            rhs_is_num = true;
        } else if (other.type == ColumnType::DECIMAL) {
            rhs_num = other.decimal_val;
            rhs_is_num = true;
        }
        
        if (lhs_is_num && rhs_is_num) {
            if (lhs_num < rhs_num) return -1;
            if (lhs_num > rhs_num) return 1;
            return 0;
        }
        
        // Optimized string comparison - compare directly without allocation
        if ((type == ColumnType::VARCHAR || type == ColumnType::TEXT) &&
            (other.type == ColumnType::VARCHAR || other.type == ColumnType::TEXT)) {
            return str_val.compare(other.str_val);
        }
        
        // Fallback for mixed types (rare case)
        return to_string().compare(other.to_string());
    }
    
    // Get numeric value for sorting
    double to_double() const {
        switch (type) {
            case ColumnType::INT: return (double)int_val;
            case ColumnType::DECIMAL: return decimal_val;
            case ColumnType::DATETIME: return (double)datetime_val;
            default: return 0.0;
        }
    }
};

// Column definition
struct ColumnDef {
    std::string name;
    ColumnType type;
    uint16_t max_length; // for VARCHAR
    bool not_null;
    bool is_primary_key;
    
    ColumnDef() : type(ColumnType::INT), max_length(255), not_null(false), is_primary_key(false) {}
    ColumnDef(const std::string& n, ColumnType t, uint16_t ml = 255, bool nn = false, bool pk = false)
        : name(n), type(t), max_length(ml), not_null(nn), is_primary_key(pk) {}
};

// A row / record - optimized with move semantics and reserve hints
struct Record {
    std::vector<Value> values;
    int64_t expiration_ts; // 0 means no expiration
    
    Record() : expiration_ts(0) {}
    
    // Move constructor for efficient transfers
    Record(Record&& other) noexcept 
        : values(std::move(other.values)), expiration_ts(other.expiration_ts) {}
    
    // Move assignment
    Record& operator=(Record&& other) noexcept {
        if (this != &other) {
            values = std::move(other.values);
            expiration_ts = other.expiration_ts;
        }
        return *this;
    }
    
    // Copy constructor
    Record(const Record&) = default;
    Record& operator=(const Record&) = default;
    
    // Pre-allocate for expected column count
    void reserve(size_t num_cols) { values.reserve(num_cols); }
    
    bool is_expired() const {
        if (expiration_ts <= 0) return false;
        return std::time(nullptr) > expiration_ts;
    }
};

// Table schema
struct TableSchema {
    std::string name;
    std::vector<ColumnDef> columns;
    int primary_key_index; // -1 if no PK
    
    TableSchema() : primary_key_index(-1) {}
    
    int find_column(const std::string& col_name) const {
        // Handle qualified names like TABLE.COLUMN
        std::string search_name = col_name;
        size_t dot_pos = col_name.find('.');
        if (dot_pos != std::string::npos) {
            std::string table_part = col_name.substr(0, dot_pos);
            // Case-insensitive table name match
            std::string upper_table = table_part;
            std::string upper_name = name;
            for (auto& c : upper_table) c = toupper(c);
            for (auto& c : upper_name) c = toupper(c);
            if (upper_table != upper_name) return -1;
            search_name = col_name.substr(dot_pos + 1);
        }
        
        for (size_t i = 0; i < columns.size(); i++) {
            std::string upper_col = columns[i].name;
            std::string upper_search = search_name;
            for (auto& c : upper_col) c = toupper(c);
            for (auto& c : upper_search) c = toupper(c);
            if (upper_col == upper_search) return (int)i;
        }
        return -1;
    }
};

// Comparison operators for WHERE clause
enum class CompOp {
    EQ,  // =
    GT,  // >
    LT,  // <
    GE,  // >=
    LE   // <=
};

using PageId = uint32_t;
constexpr PageId INVALID_PAGE_ID = 0xFFFFFFFF;

} // namespace flexql

#endif
