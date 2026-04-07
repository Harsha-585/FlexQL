#include "../../include/storage/catalog.h"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace flexql {

Catalog::Catalog(const std::string& data_dir, BufferPool* pool) 
    : data_dir_(data_dir), pool_(pool) {
    system(("mkdir -p " + data_dir_).c_str());
    system(("mkdir -p " + data_dir_ + "/tables").c_str());
    system(("mkdir -p " + data_dir_ + "/indexes").c_str());
    load();
}

Catalog::~Catalog() {
    save();
    for (auto& [name, table] : tables_) {
        if (table->data_pager) pool_->flush_all(table->data_pager.get());
        if (table->index_pager) pool_->flush_all(table->index_pager.get());
    }
}

std::string Catalog::to_upper(const std::string& s) const {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

bool Catalog::create_table(const TableSchema& schema, bool if_not_exists, std::string& error) {
    std::lock_guard<std::mutex> lock(catalog_mutex_);
    std::string upper_name = to_upper(schema.name);
    if (tables_.find(upper_name) != tables_.end()) {
        if (if_not_exists) return true;
        error = "Table " + schema.name + " already exists";
        return false;
    }
    auto table = std::make_unique<TableInfo>();
    table->schema = schema;
    table->schema.name = upper_name;
    for (auto& col : table->schema.columns) {
        std::transform(col.name.begin(), col.name.end(), col.name.begin(), ::toupper);
    }
    table->schema.primary_key_index = -1;
    for (size_t i = 0; i < table->schema.columns.size(); i++) {
        if (table->schema.columns[i].is_primary_key) {
            table->schema.primary_key_index = (int)i;
            break;
        }
    }
    if (table->schema.primary_key_index < 0 && !table->schema.columns.empty()) {
        table->schema.primary_key_index = 0;
    }
    std::string data_file = data_dir_ + "/tables/" + upper_name + ".db";
    table->data_pager = std::make_unique<Pager>(data_file);
    PageId first_page = table->data_pager->allocate_page();
    Page page;
    page.init(first_page, PageType::TABLE_DATA);
    table->data_pager->write_page(first_page, page);
    table->first_data_page = first_page;
    table->last_data_page = first_page;
    table->row_count = 0;
    std::string index_file = data_dir_ + "/indexes/" + upper_name + ".idx";
    table->index_pager = std::make_unique<Pager>(index_file);
    table->index = std::make_unique<BPlusTree>(table->index_pager.get(), pool_);
    table->index->init();
    tables_[upper_name] = std::move(table);
    save();
    return true;
}

bool Catalog::table_exists(const std::string& name) const {
    std::lock_guard<std::mutex> lock(catalog_mutex_);
    return tables_.find(to_upper(name)) != tables_.end();
}

TableInfo* Catalog::get_table(const std::string& name) {
    std::lock_guard<std::mutex> lock(catalog_mutex_);
    auto it = tables_.find(to_upper(name));
    if (it != tables_.end()) {
        TableInfo* t = it->second.get();
        if (t->magic_start != 0xDEADBEEF || t->magic_end != 0xCAFEBABE) {
            std::cerr << "CRITICAL: TableInfo CORRUPTED for " << name << "!" << std::endl;
            return nullptr;
        }
        return t;
    }
    return nullptr;
}

std::vector<std::string> Catalog::get_table_names() const {
    std::lock_guard<std::mutex> lock(catalog_mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : tables_) names.push_back(name);
    return names;
}

void Catalog::save() {
    std::string catalog_file = data_dir_ + "/catalog.meta";
    std::ofstream out(catalog_file, std::ios::binary | std::ios::trunc);
    if (!out) return;
    uint32_t num_tables = (uint32_t)tables_.size();
    out.write(reinterpret_cast<const char*>(&num_tables), 4);
    for (const auto& [name, table] : tables_) {
        uint16_t name_len = (uint16_t)name.size();
        out.write(reinterpret_cast<const char*>(&name_len), 2);
        out.write(name.c_str(), name_len);
        int32_t pk_idx = table->schema.primary_key_index;
        out.write(reinterpret_cast<const char*>(&pk_idx), 4);
        out.write(reinterpret_cast<const char*>(&table->first_data_page), 4);
        out.write(reinterpret_cast<const char*>(&table->last_data_page), 4);
        out.write(reinterpret_cast<const char*>(&table->row_count), 8);
        uint16_t num_cols = (uint16_t)table->schema.columns.size();
        out.write(reinterpret_cast<const char*>(&num_cols), 2);
        for (const auto& col : table->schema.columns) {
            uint16_t col_name_len = (uint16_t)col.name.size();
            out.write(reinterpret_cast<const char*>(&col_name_len), 2);
            out.write(col.name.c_str(), col_name_len);
            uint8_t type = static_cast<uint8_t>(col.type);
            out.write(reinterpret_cast<const char*>(&type), 1);
            out.write(reinterpret_cast<const char*>(&col.max_length), 2);
            uint8_t flags = (col.not_null ? 1 : 0) | (col.is_primary_key ? 2 : 0);
            out.write(reinterpret_cast<const char*>(&flags), 1);
        }
    }
}

void Catalog::load() {
    std::string catalog_file = data_dir_ + "/catalog.meta";
    std::ifstream in(catalog_file, std::ios::binary);
    if (!in) return;
    uint32_t num_tables;
    if (!in.read(reinterpret_cast<char*>(&num_tables), 4)) return;
    for (uint32_t t = 0; t < num_tables; t++) {
        auto table = std::make_unique<TableInfo>();
        uint16_t name_len;
        if (!in.read(reinterpret_cast<char*>(&name_len), 2)) break;
        std::string name(name_len, '\0');
        in.read(&name[0], name_len);
        table->schema.name = name;
        int32_t pk_idx;
        in.read(reinterpret_cast<char*>(&pk_idx), 4);
        table->schema.primary_key_index = pk_idx;
        in.read(reinterpret_cast<char*>(&table->first_data_page), 4);
        in.read(reinterpret_cast<char*>(&table->last_data_page), 4);
        in.read(reinterpret_cast<char*>(&table->row_count), 8);
        uint16_t num_cols;
        in.read(reinterpret_cast<char*>(&num_cols), 2);
        for (uint16_t c = 0; c < num_cols; c++) {
            ColumnDef col;
            uint16_t col_name_len;
            in.read(reinterpret_cast<char*>(&col_name_len), 2);
            col.name.resize(col_name_len);
            in.read(&col.name[0], col_name_len);
            uint8_t type;
            in.read(reinterpret_cast<char*>(&type), 1);
            col.type = static_cast<ColumnType>(type);
            in.read(reinterpret_cast<char*>(&col.max_length), 2);
            uint8_t flags;
            in.read(reinterpret_cast<char*>(&flags), 1);
            col.not_null = (flags & 1) != 0;
            col.is_primary_key = (flags & 2) != 0;
            table->schema.columns.push_back(col);
        }
        std::string data_file = data_dir_ + "/tables/" + name + ".db";
        std::string index_file = data_dir_ + "/indexes/" + name + ".idx";
        table->data_pager = std::make_unique<Pager>(data_file);
        table->index_pager = std::make_unique<Pager>(index_file);
        table->index = std::make_unique<BPlusTree>(table->index_pager.get(), pool_);
        if (table->index_pager->get_page_count() > 0) table->index->load();
        else table->index->init();
        tables_[name] = std::move(table);
    }
}

} // namespace flexql
