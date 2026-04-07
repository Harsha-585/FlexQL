#ifndef FLEXQL_STORAGE_PAGE_H
#define FLEXQL_STORAGE_PAGE_H

#include "../common/types.h"
#include <cstring>
#include <memory>

namespace flexql {

/*
 * Page Layout (4096 bytes):
 * 
 * Header (16 bytes):
 *   [0..3]   page_id (uint32_t)
 *   [4]      page_type (uint8_t)
 *   [5..6]   num_records (uint16_t)
 *   [7..8]   free_space_offset (uint16_t) - where free space starts
 *   [9..12]  next_page_id (uint32_t) - for overflow/linked pages
 *   [13..14] slot_directory_offset (uint16_t) - where slot dir starts (from end)
 *   [15]     reserved
 *
 * Data area: [16 .. free_space_offset)
 * Free space: [free_space_offset .. PAGE_SIZE - slot_dir_size)
 * Slot directory: grows backward from end of page
 *   Each slot: [offset (uint16_t), length (uint16_t)] = 4 bytes
 */

constexpr uint16_t PAGE_HEADER_SIZE = 16;
constexpr uint16_t SLOT_SIZE = 4; // offset + length

struct PageHeader {
    uint32_t page_id;
    uint8_t page_type;
    uint16_t num_records;
    uint16_t free_space_offset;
    uint32_t next_page_id;
    uint16_t slot_directory_size; // number of slots
    uint8_t reserved;
};

struct SlotEntry {
    uint16_t offset;
    uint16_t length;
};

class Page {
public:
    uint8_t data[PAGE_SIZE];
    bool dirty;
    int pin_count;
    
    Page() : dirty(false), pin_count(0) {
        memset(data, 0, PAGE_SIZE);
    }
    
    // Header access
    PageId get_page_id() const {
        PageId id;
        memcpy(&id, data, sizeof(PageId));
        return id;
    }
    void set_page_id(PageId id) {
        memcpy(data, &id, sizeof(PageId));
    }
    
    PageType get_page_type() const {
        return static_cast<PageType>(data[4]);
    }
    void set_page_type(PageType type) {
        data[4] = static_cast<uint8_t>(type);
    }
    
    uint16_t get_num_records() const {
        uint16_t n;
        memcpy(&n, data + 5, sizeof(uint16_t));
        return n;
    }
    void set_num_records(uint16_t n) {
        memcpy(data + 5, &n, sizeof(uint16_t));
    }
    
    uint16_t get_free_space_offset() const {
        uint16_t off;
        memcpy(&off, data + 7, sizeof(uint16_t));
        return off;
    }
    void set_free_space_offset(uint16_t off) {
        memcpy(data + 7, &off, sizeof(uint16_t));
    }
    
    PageId get_next_page_id() const {
        PageId id;
        memcpy(&id, data + 9, sizeof(PageId));
        return id;
    }
    void set_next_page_id(PageId id) {
        memcpy(data + 9, &id, sizeof(PageId));
    }
    
    uint16_t get_slot_count() const {
        uint16_t n;
        memcpy(&n, data + 13, sizeof(uint16_t));
        return n;
    }
    void set_slot_count(uint16_t n) {
        memcpy(data + 13, &n, sizeof(uint16_t));
    }
    
    // Initialize a new data page
    void init(PageId id, PageType type) {
        memset(data, 0, PAGE_SIZE);
        set_page_id(id);
        set_page_type(type);
        set_num_records(0);
        set_free_space_offset(PAGE_HEADER_SIZE);
        set_next_page_id(INVALID_PAGE_ID);
        set_slot_count(0);
        dirty = true;
    }
    
    // Get free space available
    uint16_t get_free_space() const {
        uint16_t data_end = get_free_space_offset();
        uint16_t slot_start = PAGE_SIZE - get_slot_count() * SLOT_SIZE;
        if (slot_start <= data_end) return 0;
        return slot_start - data_end;
    }
    
    // Get slot entry
    SlotEntry get_slot(uint16_t slot_idx) const {
        SlotEntry entry;
        uint16_t slot_pos = PAGE_SIZE - (slot_idx + 1) * SLOT_SIZE;
        memcpy(&entry.offset, data + slot_pos, sizeof(uint16_t));
        memcpy(&entry.length, data + slot_pos + 2, sizeof(uint16_t));
        return entry;
    }
    
    // Set slot entry
    void set_slot(uint16_t slot_idx, SlotEntry entry) {
        uint16_t slot_pos = PAGE_SIZE - (slot_idx + 1) * SLOT_SIZE;
        memcpy(data + slot_pos, &entry.offset, sizeof(uint16_t));
        memcpy(data + slot_pos + 2, &entry.length, sizeof(uint16_t));
    }
    
    // Insert a record into the page, returns slot index or -1 on failure
    int insert_record(const uint8_t* record_data, uint16_t record_len) {
        uint16_t needed = record_len + SLOT_SIZE;
        if (get_free_space() < needed) return -1;
        
        uint16_t offset = get_free_space_offset();
        memcpy(data + offset, record_data, record_len);
        
        uint16_t slot_idx = get_slot_count();
        SlotEntry entry;
        entry.offset = offset;
        entry.length = record_len;
        
        set_slot_count(slot_idx + 1);
        set_slot(slot_idx, entry);
        set_free_space_offset(offset + record_len);
        set_num_records(get_num_records() + 1);
        dirty = true;
        
        return slot_idx;
    }
    
    // Get record data
    const uint8_t* get_record(uint16_t slot_idx, uint16_t& out_len) const {
        if (slot_idx >= get_slot_count()) return nullptr;
        SlotEntry entry = get_slot(slot_idx);
        if (entry.length == 0) return nullptr; // deleted
        out_len = entry.length;
        return data + entry.offset;
    }
    
    // Mark record as deleted (set length to 0)
    void delete_record(uint16_t slot_idx) {
        if (slot_idx >= get_slot_count()) return;
        SlotEntry entry = get_slot(slot_idx);
        entry.length = 0;
        set_slot(slot_idx, entry);
        set_num_records(get_num_records() - 1);
        dirty = true;
    }
};

} // namespace flexql

#endif
