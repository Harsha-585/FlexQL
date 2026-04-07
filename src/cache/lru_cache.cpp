#include "../../include/cache/lru_cache.h"
#include <iostream>

namespace flexql {

BufferPool::BufferPool(uint32_t capacity) : capacity_(capacity) {
    pages_.reserve(capacity / 2);  // Pre-allocate hash map buckets
}

BufferPool::~BufferPool() {
    // Don't flush here - might not have valid pagers
}

uint64_t BufferPool::make_key(Pager* pager, PageId page_id) const {
    // Fast key generation - combine pager pointer with page_id
    // Upper 32 bits from pager address, lower 32 bits from page_id
    return (reinterpret_cast<uintptr_t>(pager) << 32) | page_id;
}

void BufferPool::add_to_unpinned(uint64_t key) {
    // Only add if not already in unpinned list
    if (unpinned_map_.find(key) == unpinned_map_.end()) {
        unpinned_list_.push_front(key);
        unpinned_map_[key] = unpinned_list_.begin();
    }
}

void BufferPool::remove_from_unpinned(uint64_t key) {
    auto it = unpinned_map_.find(key);
    if (it != unpinned_map_.end()) {
        unpinned_list_.erase(it->second);
        unpinned_map_.erase(it);
    }
}

Page* BufferPool::fetch_page(Pager* pager, PageId page_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    uint64_t key = make_key(pager, page_id);

    auto it = pages_.find(key);
    if (it != pages_.end()) {
        // Cache hit - remove from unpinned list if it was there
        if (it->second.pin_count == 0) {
            remove_from_unpinned(key);
        }
        it->second.pin_count++;
        return &it->second.page;
    }

    // Evict if necessary
    while (pages_.size() >= capacity_) {
        evict_if_needed(pager);
    }

    // Read from disk
    CacheEntry entry;
    entry.owner_pager = pager;
    entry.dirty = false;
    entry.pin_count = 1;

    if (!pager->read_page(page_id, entry.page)) {
        // New page, initialize empty
        entry.page.init(page_id, PageType::FREE);
    }

    pages_[key] = std::move(entry);

    return &pages_[key].page;
}

void BufferPool::mark_dirty(Pager* pager, PageId page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t key = make_key(pager, page_id);
    auto it = pages_.find(key);
    if (it != pages_.end()) {
        it->second.dirty = true;
    }
}

void BufferPool::unpin(Pager* pager, PageId page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t key = make_key(pager, page_id);
    auto it = pages_.find(key);
    if (it != pages_.end()) {
        if (it->second.pin_count > 0) {
            it->second.pin_count--;
            if (it->second.pin_count == 0) {
                // Add to front of unpinned list (most recently used)
                add_to_unpinned(key);
            }
        }
    }
}

void BufferPool::flush_all(Pager* pager) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, entry] : pages_) {
        if (entry.dirty && entry.owner_pager == pager) {
            pager->write_page(entry.page.get_page_id(), entry.page);
            entry.dirty = false;
        }
    }
}

void BufferPool::flush_page(Pager* pager, PageId page_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t key = make_key(pager, page_id);
    auto it = pages_.find(key);
    if (it != pages_.end() && it->second.dirty) {
        pager->write_page(page_id, it->second.page);
        it->second.dirty = false;
    }
}

bool BufferPool::evict_one(Pager* pager) {
    // O(1) eviction: pick from back of unpinned list (least recently used)
    if (unpinned_list_.empty()) {
        return false;
    }
    
    uint64_t key = unpinned_list_.back();
    auto it = pages_.find(key);
    if (it != pages_.end() && it->second.pin_count == 0) {
        // Write back if dirty
        if (it->second.dirty) {
            it->second.owner_pager->write_page(
                it->second.page.get_page_id(), it->second.page);
        }
        
        // Remove from unpinned tracking
        unpinned_list_.pop_back();
        unpinned_map_.erase(key);
        
        pages_.erase(it);
        return true;
    }
    
    // Entry was pinned between adding to list and now - remove and retry
    unpinned_list_.pop_back();
    unpinned_map_.erase(key);
    return evict_one(pager);
}

void BufferPool::evict_if_needed(Pager* pager) {
    if (!evict_one(pager)) {
        // All pages are pinned - grow slowly up to a hard limit
        // Hard limit: 256K pages (approx 1GB)
        if (capacity_ < 262144) {
            capacity_ = (uint32_t)(pages_.size() + 128);
        }
    }
}

} // namespace flexql
