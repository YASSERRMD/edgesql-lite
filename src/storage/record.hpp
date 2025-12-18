#pragma once

/**
 * @file record.hpp
 * @brief Record format definitions for EdgeSQL Lite storage
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

namespace edgesql {
namespace storage {

/**
 * @brief Column data types
 */
enum class ColumnType : uint8_t {
  NULLTYPE = 0,
  INTEGER = 1,
  FLOAT = 2,
  TEXT = 3,
  BLOB = 4,
  BOOLEAN = 5
};

/**
 * @brief Record header
 *
 * Stored at the beginning of each record.
 */
struct RecordHeader {
  uint32_t size;         // Total size of record including header
  uint16_t column_count; // Number of columns
  uint16_t flags;        // Record flags

  static constexpr uint16_t FLAG_NONE = 0x0000;
  static constexpr uint16_t FLAG_DELETED = 0x0001;
  static constexpr uint16_t FLAG_OVERFLOW = 0x0002;

  bool is_deleted() const { return flags & FLAG_DELETED; }
  bool is_overflow() const { return flags & FLAG_OVERFLOW; }
};

static_assert(sizeof(RecordHeader) == 8, "RecordHeader must be 8 bytes");

/**
 * @brief Column value type
 */
using ColumnValue = std::variant<std::monostate,       // NULL
                                 int64_t,              // INTEGER
                                 double,               // FLOAT
                                 std::string,          // TEXT
                                 std::vector<uint8_t>, // BLOB
                                 bool                  // BOOLEAN
                                 >;

/**
 * @brief Row record
 *
 * Represents a database row with multiple columns.
 */
class Record {
public:
  Record() = default;

  /**
   * @brief Create a record with specified column count
   */
  explicit Record(size_t column_count);

  /**
   * @brief Get column count
   */
  size_t column_count() const { return values_.size(); }

  /**
   * @brief Set a column value
   */
  void set_null(size_t index);
  void set_integer(size_t index, int64_t value);
  void set_float(size_t index, double value);
  void set_text(size_t index, const std::string &value);
  void set_blob(size_t index, const std::vector<uint8_t> &value);
  void set_boolean(size_t index, bool value);

  /**
   * @brief Get a column value
   */
  bool is_null(size_t index) const;
  int64_t get_integer(size_t index) const;
  double get_float(size_t index) const;
  const std::string &get_text(size_t index) const;
  const std::vector<uint8_t> &get_blob(size_t index) const;
  bool get_boolean(size_t index) const;

  /**
   * @brief Get the type of a column
   */
  ColumnType get_type(size_t index) const;

  /**
   * @brief Get the raw value
   */
  const ColumnValue &get_value(size_t index) const { return values_[index]; }
  ColumnValue &get_value(size_t index) { return values_[index]; }

  /**
   * @brief Serialize record to binary format
   * @param buffer Output buffer
   * @return Number of bytes written
   */
  size_t serialize(uint8_t *buffer, size_t buffer_size) const;

  /**
   * @brief Calculate serialized size
   */
  size_t serialized_size() const;

  /**
   * @brief Deserialize record from binary format
   * @param data Input data
   * @param length Data length
   * @return true if successful
   */
  bool deserialize(const uint8_t *data, size_t length);

private:
  std::vector<ColumnValue> values_;
};

/**
 * @brief Row ID type
 */
struct RowId {
  uint32_t page_id;
  uint16_t slot_id;

  bool operator==(const RowId &other) const {
    return page_id == other.page_id && slot_id == other.slot_id;
  }

  bool operator<(const RowId &other) const {
    if (page_id != other.page_id)
      return page_id < other.page_id;
    return slot_id < other.slot_id;
  }

  static RowId invalid() { return {0xFFFFFFFF, 0xFFFF}; }
  bool is_valid() const { return page_id != 0xFFFFFFFF; }
};

} // namespace storage
} // namespace edgesql
