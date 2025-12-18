#pragma once

/**
 * @file catalog.hpp
 * @brief Schema catalog for table metadata
 */

#include "../storage/record.hpp"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgesql {
namespace planner {

/**
 * @brief Column metadata
 */
struct ColumnInfo {
  std::string name;
  storage::ColumnType type;
  bool not_null{false};
  bool primary_key{false};
  uint32_t index; // Column index in table
};

/**
 * @brief Table metadata
 */
struct TableInfo {
  uint32_t id;
  std::string name;
  std::vector<ColumnInfo> columns;
  uint64_t row_count{0}; // Estimate for planning

  /**
   * @brief Find a column by name
   * @return Column index, or -1 if not found
   */
  int find_column(const std::string &name) const;

  /**
   * @brief Get column by index
   */
  const ColumnInfo *get_column(uint32_t index) const;
};

/**
 * @brief Schema catalog
 *
 * Thread-safe catalog for table metadata.
 */
class Catalog {
public:
  /**
   * @brief Get the singleton instance
   */
  static Catalog &instance();

  /**
   * @brief Create a new table
   * @return Table ID, or 0 on failure
   */
  uint32_t create_table(const std::string &name,
                        const std::vector<ColumnInfo> &columns);

  /**
   * @brief Drop a table
   * @return true if table existed and was dropped
   */
  bool drop_table(const std::string &name);

  /**
   * @brief Get table by name
   * @return Table info, or nullptr if not found
   */
  const TableInfo *get_table(const std::string &name) const;

  /**
   * @brief Get table by ID
   * @return Table info, or nullptr if not found
   */
  const TableInfo *get_table_by_id(uint32_t id) const;

  /**
   * @brief Check if table exists
   */
  bool table_exists(const std::string &name) const;

  /**
   * @brief Get all table names
   */
  std::vector<std::string> list_tables() const;

  /**
   * @brief Update row count estimate
   */
  void update_row_count(uint32_t table_id, uint64_t count);

  /**
   * @brief Clear all tables
   */
  void clear();

  /**
   * @brief Save catalog to disk
   */
  bool save(const std::string &path);

  /**
   * @brief Load catalog from disk
   */
  bool load(const std::string &path);

private:
  Catalog() = default;

  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::unique_ptr<TableInfo>> tables_by_name_;
  std::unordered_map<uint32_t, TableInfo *> tables_by_id_;
  uint32_t next_table_id_{1};
};

} // namespace planner
} // namespace edgesql
