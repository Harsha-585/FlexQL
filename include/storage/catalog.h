#ifndef FLEXQL_STORAGE_CATALOG_H
#define FLEXQL_STORAGE_CATALOG_H

#include "../common/types.h"
#include "pager.h"
#include "../index/bptree.h"
#include "../cache/lru_cache.h"
#include <unordered_map>
#include <shared_mutex>
#include <memory>

namespace flexql {

struct TableInfo {
    uint32_t magic_start;
    TableSchema schema;
    std::unique_ptr<Pager> data_pager;
    std::unique_ptr<Pager> index_pager;
    std::unique_ptr<BPlusTree> index;
    PageId first_data_page;
    PageId last_data_page;
    uint64_t row_count;
    mutable std::mutex rw_mutex;
    uint32_t magic_end;
    
    TableInfo() : magic_start(0xDEADBEEF), first_data_page(INVALID_PAGE_ID), last_data_page(INVALID_PAGE_ID), row_count(0), magic_end(0xCAFEBABE) {}
};

class Catalog {
public:
    Catalog(const std::string& data_dir, BufferPool* pool);
    ~Catalog();
    
    // Create a new table
    bool create_table(const TableSchema& schema, bool if_not_exists, std::string& error);
    
    // Check if table exists
    bool table_exists(const std::string& name) const;
    
    // Get table info
    TableInfo* get_table(const std::string& name);
    
    // Save catalog metadata to disk
    void save();
    
    // Load catalog metadata from disk
    void load();
    
    // Get all table names
    std::vector<std::string> get_table_names() const;
    
private:
    std::string data_dir_;
    BufferPool* pool_;
    std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_;
    mutable std::mutex catalog_mutex_;
    
    std::string to_upper(const std::string& s) const;
};

} // namespace flexql

#endif
