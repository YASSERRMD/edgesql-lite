/**
 * @file catalog.cpp
 * @brief Catalog implementation
 */

#include "catalog.hpp"
#include <algorithm>
#include <fstream>

namespace edgesql {
namespace planner {

int TableInfo::find_column(const std::string &name) const {
  for (size_t i = 0; i < columns.size(); ++i) {
    if (columns[i].name == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

const ColumnInfo *TableInfo::get_column(uint32_t index) const {
  if (index >= columns.size())
    return nullptr;
  return &columns[index];
}

Catalog &Catalog::instance() {
  static Catalog instance;
  return instance;
}

uint32_t Catalog::create_table(const std::string &name,
                               const std::vector<ColumnInfo> &columns) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Check if table already exists
  if (tables_by_name_.find(name) != tables_by_name_.end()) {
    return 0;
  }

  uint32_t id = next_table_id_++;

  auto table = std::make_unique<TableInfo>();
  table->id = id;
  table->name = name;
  table->columns = columns;

  // Set column indices
  for (size_t i = 0; i < table->columns.size(); ++i) {
    table->columns[i].index = static_cast<uint32_t>(i);
  }

  TableInfo *ptr = table.get();
  tables_by_name_[name] = std::move(table);
  tables_by_id_[id] = ptr;

  return id;
}

bool Catalog::drop_table(const std::string &name) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tables_by_name_.find(name);
  if (it == tables_by_name_.end()) {
    return false;
  }

  uint32_t id = it->second->id;
  tables_by_id_.erase(id);
  tables_by_name_.erase(it);

  return true;
}

const TableInfo *Catalog::get_table(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tables_by_name_.find(name);
  if (it != tables_by_name_.end()) {
    return it->second.get();
  }
  return nullptr;
}

const TableInfo *Catalog::get_table_by_id(uint32_t id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tables_by_id_.find(id);
  if (it != tables_by_id_.end()) {
    return it->second;
  }
  return nullptr;
}

bool Catalog::table_exists(const std::string &name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return tables_by_name_.find(name) != tables_by_name_.end();
}

std::vector<std::string> Catalog::list_tables() const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<std::string> names;
  names.reserve(tables_by_name_.size());

  for (const auto &[name, table] : tables_by_name_) {
    names.push_back(name);
  }

  std::sort(names.begin(), names.end());
  return names;
}

void Catalog::update_row_count(uint32_t table_id, uint64_t count) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tables_by_id_.find(table_id);
  if (it != tables_by_id_.end()) {
    it->second->row_count = count;
  }
}

void Catalog::clear() {
  std::lock_guard<std::mutex> lock(mutex_);

  tables_by_name_.clear();
  tables_by_id_.clear();
  next_table_id_ = 1;
}

bool Catalog::save(const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ofstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  // Write table count
  uint32_t count = static_cast<uint32_t>(tables_by_name_.size());
  file.write(reinterpret_cast<const char *>(&count), sizeof(count));
  file.write(reinterpret_cast<const char *>(&next_table_id_),
             sizeof(next_table_id_));

  // Write each table
  for (const auto &[name, table] : tables_by_name_) {
    // Table ID
    file.write(reinterpret_cast<const char *>(&table->id), sizeof(table->id));

    // Table name
    uint32_t name_len = static_cast<uint32_t>(table->name.size());
    file.write(reinterpret_cast<const char *>(&name_len), sizeof(name_len));
    file.write(table->name.data(), static_cast<std::streamsize>(name_len));

    // Column count
    uint32_t col_count = static_cast<uint32_t>(table->columns.size());
    file.write(reinterpret_cast<const char *>(&col_count), sizeof(col_count));

    // Row count
    file.write(reinterpret_cast<const char *>(&table->row_count),
               sizeof(table->row_count));

    // Each column
    for (const auto &col : table->columns) {
      uint32_t col_name_len = static_cast<uint32_t>(col.name.size());
      file.write(reinterpret_cast<const char *>(&col_name_len),
                 sizeof(col_name_len));
      file.write(col.name.data(), static_cast<std::streamsize>(col_name_len));

      uint8_t type = static_cast<uint8_t>(col.type);
      file.write(reinterpret_cast<const char *>(&type), sizeof(type));

      uint8_t flags = (col.not_null ? 1 : 0) | (col.primary_key ? 2 : 0);
      file.write(reinterpret_cast<const char *>(&flags), sizeof(flags));

      file.write(reinterpret_cast<const char *>(&col.index), sizeof(col.index));
    }
  }

  return file.good();
}

bool Catalog::load(const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  tables_by_name_.clear();
  tables_by_id_.clear();

  // Read table count
  uint32_t count;
  file.read(reinterpret_cast<char *>(&count), sizeof(count));
  file.read(reinterpret_cast<char *>(&next_table_id_), sizeof(next_table_id_));

  // Read each table
  for (uint32_t i = 0; i < count && file.good(); ++i) {
    auto table = std::make_unique<TableInfo>();

    // Table ID
    file.read(reinterpret_cast<char *>(&table->id), sizeof(table->id));

    // Table name
    uint32_t name_len;
    file.read(reinterpret_cast<char *>(&name_len), sizeof(name_len));
    table->name.resize(name_len);
    file.read(table->name.data(), static_cast<std::streamsize>(name_len));

    // Column count
    uint32_t col_count;
    file.read(reinterpret_cast<char *>(&col_count), sizeof(col_count));

    // Row count
    file.read(reinterpret_cast<char *>(&table->row_count),
              sizeof(table->row_count));

    // Each column
    table->columns.resize(col_count);
    for (uint32_t j = 0; j < col_count && file.good(); ++j) {
      ColumnInfo &col = table->columns[j];

      uint32_t col_name_len;
      file.read(reinterpret_cast<char *>(&col_name_len), sizeof(col_name_len));
      col.name.resize(col_name_len);
      file.read(col.name.data(), static_cast<std::streamsize>(col_name_len));

      uint8_t type;
      file.read(reinterpret_cast<char *>(&type), sizeof(type));
      col.type = static_cast<storage::ColumnType>(type);

      uint8_t flags;
      file.read(reinterpret_cast<char *>(&flags), sizeof(flags));
      col.not_null = (flags & 1) != 0;
      col.primary_key = (flags & 2) != 0;

      file.read(reinterpret_cast<char *>(&col.index), sizeof(col.index));
    }

    if (file.good()) {
      TableInfo *ptr = table.get();
      tables_by_id_[table->id] = ptr;
      tables_by_name_[table->name] = std::move(table);
    }
  }

  return file.good();
}

} // namespace planner
} // namespace edgesql
