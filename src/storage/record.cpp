#include "../../include/storage/record.h"

namespace flexql {

void RecordSerializer::serialize(const Record& record, const TableSchema& schema, std::vector<uint8_t>& buf) {
    buf.clear();

    // Pre-calculate exact buffer size to avoid any resize
    size_t total_size = 8; // expiration timestamp
    for (size_t i = 0; i < schema.columns.size() && i < record.values.size(); i++) {
        total_size += 1; // null flag
        if (!record.values[i].is_null) {
            switch (schema.columns[i].type) {
                case ColumnType::INT:
                case ColumnType::DECIMAL:
                case ColumnType::DATETIME:
                    total_size += 8;
                    break;
                case ColumnType::VARCHAR:
                case ColumnType::TEXT:
                    total_size += 2 + record.values[i].str_val.size();
                    break;
            }
        }
    }
    
    // Single allocation - resize to exact size
    buf.resize(total_size);
    uint8_t* ptr = buf.data();

    // Write expiration timestamp
    memcpy(ptr, &record.expiration_ts, 8);
    ptr += 8;

    for (size_t i = 0; i < schema.columns.size() && i < record.values.size(); i++) {
        const Value& val = record.values[i];

        // Null flag
        *ptr++ = val.is_null ? 1 : 0;
        if (val.is_null) continue;

        switch (schema.columns[i].type) {
            case ColumnType::INT:
                memcpy(ptr, &val.int_val, 8);
                ptr += 8;
                break;
            case ColumnType::DECIMAL:
                memcpy(ptr, &val.decimal_val, 8);
                ptr += 8;
                break;
            case ColumnType::VARCHAR:
            case ColumnType::TEXT: {
                uint16_t len = (uint16_t)val.str_val.size();
                memcpy(ptr, &len, 2);
                memcpy(ptr + 2, val.str_val.data(), len);
                ptr += 2 + len;
                break;
            }
            case ColumnType::DATETIME:
                memcpy(ptr, &val.datetime_val, 8);
                ptr += 8;
                break;
        }
    }
}

std::vector<uint8_t> RecordSerializer::serialize(const Record& record, const TableSchema& schema) {
    std::vector<uint8_t> buf;
    serialize(record, schema, buf);
    return buf;
}

Record RecordSerializer::deserialize(const uint8_t* data, uint16_t len, const TableSchema& schema) {
    Record record;
    record.values.reserve(schema.columns.size());
    size_t offset = 0;

    // Read expiration timestamp
    if (offset + 8 <= len) {
        memcpy(&record.expiration_ts, data + offset, 8);
        offset += 8;
    }

    for (size_t i = 0; i < schema.columns.size() && offset < len; i++) {
        // Read null flag
        bool is_null = data[offset++];
        if (is_null) {
            record.values.push_back(Value::make_null());
            continue;
        }

        switch (schema.columns[i].type) {
            case ColumnType::INT: {
                int64_t val;
                memcpy(&val, data + offset, 8);
                offset += 8;
                record.values.push_back(Value::make_int(val));
                break;
            }
            case ColumnType::DECIMAL: {
                double val;
                memcpy(&val, data + offset, 8);
                offset += 8;
                record.values.push_back(Value::make_decimal(val));
                break;
            }
            case ColumnType::VARCHAR:
            case ColumnType::TEXT: {
                uint16_t slen;
                memcpy(&slen, data + offset, 2);
                offset += 2;
                record.values.emplace_back(Value::make_varchar(std::string(reinterpret_cast<const char*>(data + offset), slen)));
                offset += slen;
                break;
            }
            case ColumnType::DATETIME: {
                int64_t val;
                memcpy(&val, data + offset, 8);
                offset += 8;
                record.values.push_back(Value::make_datetime(val));
                break;
            }
        }
    }

    return record;
}

uint16_t RecordSerializer::estimate_size(const Record& record, const TableSchema& schema) {
    uint16_t size = 8; // expiration timestamp
    
    for (size_t i = 0; i < schema.columns.size() && i < record.values.size(); i++) {
        size += 1; // null flag
        if (record.values[i].is_null) continue;
        
        switch (schema.columns[i].type) {
            case ColumnType::INT:
            case ColumnType::DECIMAL:
            case ColumnType::DATETIME:
                size += 8;
                break;
            case ColumnType::VARCHAR:
            case ColumnType::TEXT:
                size += 2 + (uint16_t)record.values[i].str_val.size();
                break;
        }
    }
    
    return size;
}

} // namespace flexql
