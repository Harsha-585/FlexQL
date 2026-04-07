#include "../../include/storage/pager.h"
#include <iostream>
#include <filesystem>

namespace flexql {

Pager::Pager(const std::string& filename) : filename_(filename), page_count_(0), file_page_count_(0) {
    open_file();
}

Pager::~Pager() {
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

void Pager::open_file() {
    // Create directories if they don't exist
    // std::filesystem workaround
    if (filename_.find("/") != std::string::npos) {
        system(("mkdir -p $(dirname " + filename_ + ")").c_str());
    }

    // Try to open existing file
    file_.open(filename_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        // Create new file
        std::ofstream create(filename_, std::ios::binary);
        create.close();
        file_.open(filename_, std::ios::in | std::ios::out | std::ios::binary);
    }

    if (file_.is_open()) {
        file_.seekg(0, std::ios::end);
        auto size = file_.tellg();
        page_count_ = (uint32_t)(size / PAGE_SIZE);
        file_page_count_ = page_count_;
        file_.seekg(0, std::ios::beg);
    }
}

bool Pager::read_page(PageId page_id, Page& page) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (page_id >= file_page_count_) {
        return false;
    }

    file_.seekg((std::streamoff)page_id * PAGE_SIZE, std::ios::beg);
    file_.read(reinterpret_cast<char*>(page.data), PAGE_SIZE);

    return file_.good() || file_.eof();
}

bool Pager::write_page(PageId page_id, const Page& page) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Pre-extend file in larger chunks to reduce syscalls
    if (page_id >= file_page_count_) {
        // Extend by at least 64 pages at a time to reduce extension overhead
        PageId target = ((page_id / 64) + 1) * 64;
        file_.seekp(0, std::ios::end);
        static const char ZEROS[PAGE_SIZE] = {0};
        while (file_page_count_ < target) {
            file_.write(ZEROS, PAGE_SIZE);
            file_page_count_++;
        }
    }

    file_.seekp((std::streamoff)page_id * PAGE_SIZE, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(page.data), PAGE_SIZE);

    return file_.good();
}

PageId Pager::allocate_page() {
    std::lock_guard<std::mutex> lock(mutex_);

    PageId new_id = page_count_;
    page_count_++;

    return new_id;
}

void Pager::sync() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
    }
}

} // namespace flexql
