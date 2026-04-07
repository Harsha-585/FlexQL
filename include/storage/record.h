#ifndef FLEXQL_STORAGE_RECORD_H
#define FLEXQL_STORAGE_RECORD_H

#include "../common/types.h"
#include <cstring>
#include <vector>

namespace flexql {

class RecordSerializer {
public:
    // Serialize a record to a pre-allocated binary buffer
    static void serialize(const Record& record, const TableSchema& schema, std::vector<uint8_t>& buf);
    
    // Serialize a record to binary format (allocates a new vector)
    static std::vector<uint8_t> serialize(const Record& record, const TableSchema& schema);
    
    // Deserialize binary data to a record
    static Record deserialize(const uint8_t* data, uint16_t len, const TableSchema& schema);
    
    // Estimate serialized size
    static uint16_t estimate_size(const Record& record, const TableSchema& schema);
};

} // namespace flexql

#endif
