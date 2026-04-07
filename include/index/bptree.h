#ifndef FLEXQL_INDEX_BPTREE_H
#define FLEXQL_INDEX_BPTREE_H

#include "../common/types.h"
#include "../storage/pager.h"
#include "../cache/lru_cache.h"
#include <vector>
#include <mutex>

namespace flexql {

// Location of a record: which data page and which slot
struct RecordPtr {
    PageId page_id;
    uint16_t slot_idx;
    
    RecordPtr() : page_id(INVALID_PAGE_ID), slot_idx(0) {}
    RecordPtr(PageId p, uint16_t s) : page_id(p), slot_idx(s) {}
};

/*
 * B+ Tree Node Layout (fits in one page):
 *
 * Header (12 bytes):
 *   [0..3]  page_id
 *   [4]     node_type: 0=internal, 1=leaf
 *   [5..6]  num_keys
 *   [7..10] parent_page_id
 *   [11]    reserved
 *
 * For LEAF nodes (after header):
 *   [12..15] next_leaf_page_id
 *   [16..19] prev_leaf_page_id 
 *   Keys + RecordPtrs interleaved:
 *   Each entry: [8-byte key (double)] [4-byte page_id] [2-byte slot_idx] = 14 bytes
 *   Max entries per leaf: (4096 - 20) / 14 = ~291
 *
 * For INTERNAL nodes (after header):
 *   [12..15] first child page_id (leftmost ptr)
 *   Keys + child page ptrs:
 *   Each entry: [8-byte key (double)] [4-byte child_page_id] = 12 bytes
 *   Max entries: (4096 - 16) / 12 = ~340
 */

constexpr uint16_t BPTREE_HEADER_SIZE = 12;
constexpr uint16_t LEAF_ENTRY_SIZE = 14;    // 8 (key) + 4 (page_id) + 2 (slot_idx)
constexpr uint16_t INTERNAL_ENTRY_SIZE = 12; // 8 (key) + 4 (child_page_id)
constexpr uint16_t MAX_LEAF_ENTRIES = (PAGE_SIZE - 20) / LEAF_ENTRY_SIZE;
constexpr uint16_t MAX_INTERNAL_ENTRIES = (PAGE_SIZE - 16) / INTERNAL_ENTRY_SIZE;

class BPlusTree {
public:
    BPlusTree(Pager* pager, BufferPool* pool);
    ~BPlusTree();
    
    // Initialize a new tree (creates root)
    void init();
    
    // Load existing tree from pager (root is page 0)
    void load();
    
    // Insert a key-record mapping
    bool insert(double key, RecordPtr ptr);
    
    // Batch insert without locking (caller must hold lock)
    bool insert_unlocked(double key, RecordPtr ptr);
    
    // Lock/unlock for batch operations
    void lock() { mutex_.lock(); }
    void unlock() { mutex_.unlock(); }
    
    // Search for exact key
    bool search(double key, RecordPtr& out_ptr);
    
    // Range scan: find all keys in [low, high]
    std::vector<RecordPtr> range_scan(double low, double high);
    
    // Get all entries (full scan)
    std::vector<std::pair<double, RecordPtr>> scan_all();
    
    // Delete a key
    bool remove(double key);
    
    PageId get_root_page_id() const { return root_page_id_; }
    
private:
    Pager* pager_;
    BufferPool* pool_;
    PageId root_page_id_;
    std::recursive_mutex mutex_;
    
    // Node access helpers
    bool is_leaf(Page* page) const;
    uint16_t get_num_keys(Page* page) const;
    void set_num_keys(Page* page, uint16_t n);
    PageId get_parent(Page* page) const;
    void set_parent(Page* page, PageId parent);
    
    // Leaf node helpers
    PageId get_next_leaf(Page* page) const;
    void set_next_leaf(Page* page, PageId next);
    double get_leaf_key(Page* page, uint16_t idx) const;
    RecordPtr get_leaf_ptr(Page* page, uint16_t idx) const;
    void set_leaf_entry(Page* page, uint16_t idx, double key, RecordPtr ptr);
    
    // Internal node helpers
    PageId get_first_child(Page* page) const;
    void set_first_child(Page* page, PageId child);
    double get_internal_key(Page* page, uint16_t idx) const;
    PageId get_internal_child(Page* page, uint16_t idx) const;
    void set_internal_entry(Page* page, uint16_t idx, double key, PageId child);
    
    // Tree operations
    PageId find_leaf(double key);
    void insert_into_leaf(PageId leaf_id, double key, RecordPtr ptr);
    void split_leaf(PageId leaf_id);
    void insert_into_internal(PageId node_id, double key, PageId right_child);
    void split_internal(PageId node_id);
    
    Page* get_page(PageId id);
    void release_page(PageId id, bool dirty = false);
};

} // namespace flexql

#endif
