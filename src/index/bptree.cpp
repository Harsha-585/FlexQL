#include "../../include/index/bptree.h"
#include <algorithm>
#include <cstring>
#include <iostream>

namespace flexql {

BPlusTree::BPlusTree(Pager* pager, BufferPool* pool)
    : pager_(pager), pool_(pool), root_page_id_(INVALID_PAGE_ID) {}

BPlusTree::~BPlusTree() {}

void BPlusTree::init() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    // Allocate root page as a leaf
    root_page_id_ = pager_->allocate_page();
    Page* root = get_page(root_page_id_);
    root->init(root_page_id_, PageType::BPTREE_LEAF);
    
    // Set leaf flag
    root->data[4] = 1; // leaf
    set_num_keys(root, 0);
    set_parent(root, INVALID_PAGE_ID);
    
    // next/prev leaf
    uint32_t inv = INVALID_PAGE_ID;
    memcpy(root->data + 12, &inv, 4); // next
    memcpy(root->data + 16, &inv, 4); // prev
    
    release_page(root_page_id_, true);
}

void BPlusTree::load() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    // Root is always page 0 of the index file
    root_page_id_ = 0;
}

Page* BPlusTree::get_page(PageId id) {
    return pool_->fetch_page(pager_, id);
}

void BPlusTree::release_page(PageId id, bool dirty) {
    if (dirty) {
        pool_->mark_dirty(pager_, id);
    }
    pool_->unpin(pager_, id);
}

bool BPlusTree::is_leaf(Page* page) const {
    return page->data[4] == 1;
}

uint16_t BPlusTree::get_num_keys(Page* page) const {
    uint16_t n;
    memcpy(&n, page->data + 5, 2);
    return n;
}

void BPlusTree::set_num_keys(Page* page, uint16_t n) {
    memcpy(page->data + 5, &n, 2);
}

PageId BPlusTree::get_parent(Page* page) const {
    PageId id;
    memcpy(&id, page->data + 7, 4);
    return id;
}

void BPlusTree::set_parent(Page* page, PageId parent) {
    memcpy(page->data + 7, &parent, 4);
}

// Leaf helpers
PageId BPlusTree::get_next_leaf(Page* page) const {
    PageId id;
    memcpy(&id, page->data + 12, 4);
    return id;
}

void BPlusTree::set_next_leaf(Page* page, PageId next) {
    memcpy(page->data + 12, &next, 4);
}

double BPlusTree::get_leaf_key(Page* page, uint16_t idx) const {
    double key;
    uint16_t offset = 20 + idx * LEAF_ENTRY_SIZE;
    memcpy(&key, page->data + offset, 8);
    return key;
}

RecordPtr BPlusTree::get_leaf_ptr(Page* page, uint16_t idx) const {
    uint16_t offset = 20 + idx * LEAF_ENTRY_SIZE + 8;
    RecordPtr ptr;
    memcpy(&ptr.page_id, page->data + offset, 4);
    memcpy(&ptr.slot_idx, page->data + offset + 4, 2);
    return ptr;
}

void BPlusTree::set_leaf_entry(Page* page, uint16_t idx, double key, RecordPtr ptr) {
    uint16_t offset = 20 + idx * LEAF_ENTRY_SIZE;
    memcpy(page->data + offset, &key, 8);
    memcpy(page->data + offset + 8, &ptr.page_id, 4);
    memcpy(page->data + offset + 12, &ptr.slot_idx, 2);
}

// Internal helpers
PageId BPlusTree::get_first_child(Page* page) const {
    PageId id;
    memcpy(&id, page->data + 12, 4);
    return id;
}

void BPlusTree::set_first_child(Page* page, PageId child) {
    memcpy(page->data + 12, &child, 4);
}

double BPlusTree::get_internal_key(Page* page, uint16_t idx) const {
    double key;
    uint16_t offset = 16 + idx * INTERNAL_ENTRY_SIZE;
    memcpy(&key, page->data + offset, 8);
    return key;
}

PageId BPlusTree::get_internal_child(Page* page, uint16_t idx) const {
    PageId child;
    uint16_t offset = 16 + idx * INTERNAL_ENTRY_SIZE + 8;
    memcpy(&child, page->data + offset, 4);
    return child;
}

void BPlusTree::set_internal_entry(Page* page, uint16_t idx, double key, PageId child) {
    uint16_t offset = 16 + idx * INTERNAL_ENTRY_SIZE;
    memcpy(page->data + offset, &key, 8);
    memcpy(page->data + offset + 8, &child, 4);
}

PageId BPlusTree::find_leaf(double key) {
    PageId current = root_page_id_;

    while (true) {
        Page* page = get_page(current);
        if (is_leaf(page)) {
            release_page(current, false);
            return current;
        }

        uint16_t num_keys = get_num_keys(page);
        PageId next;

        if (num_keys == 0) {
            next = get_first_child(page);
        } else {
            // Binary search for child to follow
            uint16_t lo = 0, hi = num_keys;
            while (lo < hi) {
                uint16_t mid = lo + (hi - lo) / 2;
                if (get_internal_key(page, mid) <= key) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            // lo is now the index of first key > key, or num_keys if all keys <= key
            if (lo == 0) {
                next = get_first_child(page);
            } else {
                next = get_internal_child(page, lo - 1);
            }
        }

        release_page(current, false);
        current = next;
    }
}

bool BPlusTree::insert(double key, RecordPtr ptr) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return insert_unlocked(key, ptr);
}

bool BPlusTree::insert_unlocked(double key, RecordPtr ptr) {
    if (root_page_id_ == INVALID_PAGE_ID) {
        init();
    }
    
    PageId leaf_id = find_leaf(key);
    insert_into_leaf(leaf_id, key, ptr);
    return true;
}

void BPlusTree::insert_into_leaf(PageId leaf_id, double key, RecordPtr ptr) {
    Page* leaf = get_page(leaf_id);
    uint16_t num_keys = get_num_keys(leaf);

    // Binary search for insertion position
    uint16_t lo = 0, hi = num_keys;
    while (lo < hi) {
        uint16_t mid = lo + (hi - lo) / 2;
        if (get_leaf_key(leaf, mid) < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    uint16_t pos = lo;

    // Check if key exists (update)
    if (pos < num_keys && get_leaf_key(leaf, pos) == key) {
        set_leaf_entry(leaf, pos, key, ptr);
        release_page(leaf_id, true);
        return;
    }

    // Check if leaf has space
    if (num_keys < MAX_LEAF_ENTRIES) {
        // Shift entries right
        for (uint16_t i = num_keys; i > pos; i--) {
            double k = get_leaf_key(leaf, i - 1);
            RecordPtr p = get_leaf_ptr(leaf, i - 1);
            set_leaf_entry(leaf, i, k, p);
        }
        set_leaf_entry(leaf, pos, key, ptr);
        set_num_keys(leaf, num_keys + 1);
        release_page(leaf_id, true);
    } else {
        // Need to split
        
        // Collect all entries + new entry
        std::vector<std::pair<double, RecordPtr>> entries;
        for (uint16_t i = 0; i < num_keys; i++) {
            entries.push_back({get_leaf_key(leaf, i), get_leaf_ptr(leaf, i)});
        }
        entries.insert(entries.begin() + pos, {key, ptr});
        
        // Create new leaf
        PageId new_leaf_id = pager_->allocate_page();
        Page* new_leaf = get_page(new_leaf_id);
        new_leaf->init(new_leaf_id, PageType::BPTREE_LEAF);
        new_leaf->data[4] = 1; // leaf
        
        // Split entries
        uint16_t split = (uint16_t)(entries.size() / 2);
        
        // Refill old leaf
        Page* old_leaf = leaf; // Already pinned
        set_num_keys(old_leaf, 0);
        for (uint16_t i = 0; i < split; i++) {
            set_leaf_entry(old_leaf, i, entries[i].first, entries[i].second);
        }
        set_num_keys(old_leaf, split);
        
        // Fill new leaf
        uint16_t new_count = (uint16_t)(entries.size() - split);
        for (uint16_t i = 0; i < new_count; i++) {
            set_leaf_entry(new_leaf, i, entries[split + i].first, entries[split + i].second);
        }
        set_num_keys(new_leaf, new_count);
        
        // Update linked list pointers
        PageId old_next = get_next_leaf(old_leaf);
        set_next_leaf(old_leaf, new_leaf_id);
        set_next_leaf(new_leaf, old_next);
        
        // Set parent
        PageId parent = get_parent(old_leaf);
        set_parent(new_leaf, parent);
        
        double split_key = entries[split].first;
        
        release_page(new_leaf_id, true);
        release_page(leaf_id, true);
        
        // Insert into parent
        if (parent == INVALID_PAGE_ID) {
            // Create new root
            PageId new_root_id = pager_->allocate_page();
            Page* new_root = get_page(new_root_id);
            new_root->init(new_root_id, PageType::BPTREE_INTERNAL);
            new_root->data[4] = 0; // internal
            set_num_keys(new_root, 1);
            set_parent(new_root, INVALID_PAGE_ID);
            set_first_child(new_root, leaf_id);
            set_internal_entry(new_root, 0, split_key, new_leaf_id);
            
            // Update children's parent
            Page* child1_page = get_page(leaf_id);
            set_parent(child1_page, new_root_id);
            release_page(leaf_id, true);
            
            Page* child2_page = get_page(new_leaf_id);
            set_parent(child2_page, new_root_id);
            release_page(new_leaf_id, true);
            
            release_page(new_root_id, true);
            root_page_id_ = new_root_id;
        } else {
            insert_into_internal(parent, split_key, new_leaf_id);
        }
    }
}

void BPlusTree::insert_into_internal(PageId node_id, double key, PageId right_child) {
    Page* node = get_page(node_id);
    uint16_t num_keys = get_num_keys(node);
    
    if (num_keys < MAX_INTERNAL_ENTRIES) {
        // Find position
        uint16_t pos = 0;
        while (pos < num_keys && get_internal_key(node, pos) < key) {
            pos++;
        }
        
        // Shift right
        for (uint16_t i = num_keys; i > pos; i--) {
            double k = get_internal_key(node, i - 1);
            PageId c = get_internal_child(node, i - 1);
            set_internal_entry(node, i, k, c);
        }
        
        set_internal_entry(node, pos, key, right_child);
        set_num_keys(node, num_keys + 1);
        release_page(node_id, true);
        
        // Update child's parent
        Page* child = get_page(right_child);
        set_parent(child, node_id);
        release_page(right_child, true);
    } else {
        // Split internal node
        std::vector<std::pair<double, PageId>> entries;
        for (uint16_t i = 0; i < num_keys; i++) {
            entries.push_back({get_internal_key(node, i), get_internal_child(node, i)});
        }
        
        // Insert new entry
        uint16_t pos = 0;
        while (pos < entries.size() && entries[pos].first < key) pos++;
        entries.insert(entries.begin() + pos, {key, right_child});
        
        uint16_t split = (uint16_t)(entries.size() / 2);
        double split_key = entries[split].first;
        
        // Keep left half in old node
        PageId first_child = get_first_child(node);
        set_num_keys(node, split);
        for (uint16_t i = 0; i < split; i++) {
            set_internal_entry(node, i, entries[i].first, entries[i].second);
        }
        
        // Create new internal node with right half
        PageId new_node_id = pager_->allocate_page();
        Page* new_node = get_page(new_node_id);
        new_node->init(new_node_id, PageType::BPTREE_INTERNAL);
        new_node->data[4] = 0; // internal
        
        // The split key's right child becomes new node's first child
        set_first_child(new_node, entries[split].second);
        
        uint16_t new_count = (uint16_t)(entries.size() - split - 1);
        for (uint16_t i = 0; i < new_count; i++) {
            set_internal_entry(new_node, i, entries[split + 1 + i].first, entries[split + 1 + i].second);
        }
        set_num_keys(new_node, new_count);
        
        PageId parent = get_parent(node);
        set_parent(new_node, parent);
        
        release_page(node_id, true);
        release_page(new_node_id, true);
        
        // Update children's parent pointers for new node
        Page* nn_page = get_page(new_node_id);
        PageId fc = get_first_child(nn_page);
        uint16_t nk = get_num_keys(nn_page);
        
        Page* fc_page = get_page(fc);
        set_parent(fc_page, new_node_id);
        release_page(fc, true);
        
        for (uint16_t i = 0; i < nk; i++) {
            PageId cid = get_internal_child(nn_page, i);
            Page* cp = get_page(cid);
            set_parent(cp, new_node_id);
            release_page(cid, true);
        }
        release_page(new_node_id, true);
        
        // Update child's parent
        Page* rc = get_page(right_child);
        set_parent(rc, (pos <= split) ? node_id : new_node_id);
        release_page(right_child, true);
        
        if (parent == INVALID_PAGE_ID) {
            // Create new root
            PageId new_root_id = pager_->allocate_page();
            Page* new_root = get_page(new_root_id);
            new_root->init(new_root_id, PageType::BPTREE_INTERNAL);
            new_root->data[4] = 0;
            set_num_keys(new_root, 1);
            set_parent(new_root, INVALID_PAGE_ID);
            set_first_child(new_root, node_id);
            set_internal_entry(new_root, 0, split_key, new_node_id);
            
            // Update children's parent
            Page* c1 = get_page(node_id);
            set_parent(c1, new_root_id);
            release_page(node_id, true);
            
            Page* c2 = get_page(new_node_id);
            set_parent(c2, new_root_id);
            release_page(new_node_id, true);
            
            release_page(new_root_id, true);
            root_page_id_ = new_root_id;
        } else {
            insert_into_internal(parent, split_key, new_node_id);
        }
    }
}

bool BPlusTree::search(double key, RecordPtr& out_ptr) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (root_page_id_ == INVALID_PAGE_ID) return false;
    
    PageId leaf_id = find_leaf(key);
    Page* leaf = get_page(leaf_id);
    uint16_t num_keys = get_num_keys(leaf);
    
    for (uint16_t i = 0; i < num_keys; i++) {
        if (get_leaf_key(leaf, i) == key) {
            out_ptr = get_leaf_ptr(leaf, i);
            release_page(leaf_id, false);
            return true;
        }
    }
    
    release_page(leaf_id, false);
    return false;
}

std::vector<RecordPtr> BPlusTree::range_scan(double low, double high) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    std::vector<RecordPtr> results;
    if (root_page_id_ == INVALID_PAGE_ID) return results;
    
    PageId leaf_id = find_leaf(low);
    
    while (leaf_id != INVALID_PAGE_ID) {
        Page* leaf = get_page(leaf_id);
        uint16_t num_keys = get_num_keys(leaf);
        
        for (uint16_t i = 0; i < num_keys; i++) {
            double k = get_leaf_key(leaf, i);
            if (k > high) {
                release_page(leaf_id, false);
                return results;
            }
            if (k >= low) {
                results.push_back(get_leaf_ptr(leaf, i));
            }
        }
        
        PageId next = get_next_leaf(leaf);
        release_page(leaf_id, false);
        leaf_id = next;
    }
    
    return results;
}

std::vector<std::pair<double, RecordPtr>> BPlusTree::scan_all() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    std::vector<std::pair<double, RecordPtr>> results;
    if (root_page_id_ == INVALID_PAGE_ID) return results;
    
    // Find the leftmost leaf
    PageId current = root_page_id_;
    while (true) {
        Page* page = get_page(current);
        if (is_leaf(page)) {
            release_page(current, false);
            break;
        }
        PageId next = get_first_child(page);
        release_page(current, false);
        current = next;
    }
    
    // Scan all leaves
    while (current != INVALID_PAGE_ID) {
        Page* leaf = get_page(current);
        uint16_t num_keys = get_num_keys(leaf);
        
        for (uint16_t i = 0; i < num_keys; i++) {
            results.push_back({get_leaf_key(leaf, i), get_leaf_ptr(leaf, i)});
        }
        
        PageId next = get_next_leaf(leaf);
        release_page(current, false);
        current = next;
    }
    
    return results;
}

bool BPlusTree::remove(double key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (root_page_id_ == INVALID_PAGE_ID) return false;
    
    PageId leaf_id = find_leaf(key);
    Page* leaf = get_page(leaf_id);
    uint16_t num_keys = get_num_keys(leaf);
    
    // Find and remove
    int found = -1;
    for (uint16_t i = 0; i < num_keys; i++) {
        if (get_leaf_key(leaf, i) == key) {
            found = i;
            break;
        }
    }
    
    if (found < 0) {
        release_page(leaf_id, false);
        return false;
    }
    
    // Shift left
    for (uint16_t i = (uint16_t)found; i < num_keys - 1; i++) {
        set_leaf_entry(leaf, i, get_leaf_key(leaf, i + 1), get_leaf_ptr(leaf, i + 1));
    }
    set_num_keys(leaf, num_keys - 1);
    release_page(leaf_id, true);
    
    return true;
}

} // namespace flexql
