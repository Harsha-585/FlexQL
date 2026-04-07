#ifndef FLEXQL_STORAGE_PAGER_H
#define FLEXQL_STORAGE_PAGER_H

#include "page.h"
#include <string>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace flexql {

class Pager {
public:
    Pager(const std::string& filename);
    ~Pager();
    
    // Read a page from disk
    bool read_page(PageId page_id, Page& page);
    
    // Write a page to disk
    bool write_page(PageId page_id, const Page& page);
    
    // Allocate a new page, returns its ID
    PageId allocate_page();
    
    // Get total number of pages
    uint32_t get_page_count() const { return page_count_; }
    
    // Sync to disk
    void sync();
    
    const std::string& get_filename() const { return filename_; }
    
private:
    std::string filename_;
    std::fstream file_;
    uint32_t page_count_;       // Allocated pages (logical)
    uint32_t file_page_count_;  // Pages actually on disk (physical)
    std::mutex mutex_;

    void open_file();
};

} // namespace flexql

#endif
