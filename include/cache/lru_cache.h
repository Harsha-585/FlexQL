#ifndef FLEXQL_CACHE_LRU_CACHE_H
#define FLEXQL_CACHE_LRU_CACHE_H

#include "../storage/page.h"
#include "../storage/pager.h"
#include <list>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

namespace flexql {

class BufferPool {
public:
    BufferPool(uint32_t capacity = MAX_PAGES_IN_CACHE);
    ~BufferPool();
    
    // Get a page (reads from disk if not cached)
    Page* fetch_page(Pager* pager, PageId page_id);
    
    // Mark page as dirty
    void mark_dirty(Pager* pager, PageId page_id);
    
    // Unpin a page
    void unpin(Pager* pager, PageId page_id);
    
    // Flush all dirty pages
    void flush_all(Pager* pager);
    
    // Flush a specific page
    void flush_page(Pager* pager, PageId page_id);
    
    // Evict a page (for memory pressure)
    bool evict_one(Pager* pager);
    
private:
    struct CacheEntry {
        Page page;
        Pager* owner_pager;
        bool dirty;
        int pin_count;
    };
    
    uint32_t capacity_;
    std::unordered_map<uint64_t, CacheEntry> pages_;
    std::list<uint64_t> unpinned_list_;  // Only unpinned pages for O(1) eviction
    std::unordered_map<uint64_t, std::list<uint64_t>::iterator> unpinned_map_;
    mutable std::mutex mutex_;
    
    uint64_t make_key(Pager* pager, PageId page_id) const;
    void evict_if_needed(Pager* pager);
    void add_to_unpinned(uint64_t key);
    void remove_from_unpinned(uint64_t key);
};

} // namespace flexql

#endif
