#pragma once

/**
 * @file page.hpp
 * @brief Page layout definitions for EdgeSQL Lite storage
 */

#include <array>
#include <cstdint>
#include <cstring>

namespace edgesql {
namespace storage {

// Page size constant (8 KB)
constexpr size_t PAGE_SIZE = 8192;

// Magic number for page validation
constexpr uint32_t PAGE_MAGIC = 0x45444247; // "EDBG"

/**
 * @brief Page header structure
 *
 * Located at the beginning of every page.
 */
struct PageHeader {
  uint32_t magic;   // Magic number for validation
  uint32_t page_id; // Unique page identifier
  uint64_t lsn; // Log sequence number (last WAL entry that modified this page)
  uint16_t slot_count; // Number of slots in the page
  uint16_t free_space; // Bytes of free space available
  uint16_t data_start; // Offset where data area begins (grows upward)
  uint16_t flags;      // Page flags

  // Page flags
  static constexpr uint16_t FLAG_NONE = 0x0000;
  static constexpr uint16_t FLAG_LEAF = 0x0001;
  static constexpr uint16_t FLAG_INTERNAL = 0x0002;
  static constexpr uint16_t FLAG_OVERFLOW = 0x0004;
  static constexpr uint16_t FLAG_DIRTY = 0x0008;

  bool is_valid() const { return magic == PAGE_MAGIC; }
  bool is_leaf() const { return flags & FLAG_LEAF; }
  bool is_internal() const { return flags & FLAG_INTERNAL; }
  bool is_overflow() const { return flags & FLAG_OVERFLOW; }
  bool is_dirty() const { return flags & FLAG_DIRTY; }

  void set_dirty(bool dirty) {
    if (dirty)
      flags |= FLAG_DIRTY;
    else
      flags &= ~FLAG_DIRTY;
  }
};

static_assert(sizeof(PageHeader) == 24, "PageHeader must be 24 bytes");

/**
 * @brief Slot directory entry
 *
 * Points to a record within the page.
 */
struct SlotEntry {
  uint16_t offset; // Offset from page start to record
  uint16_t length; // Length of the record

  bool is_empty() const { return offset == 0 && length == 0; }
  bool is_deleted() const { return offset == 0xFFFF; }

  void mark_deleted() {
    offset = 0xFFFF;
    length = 0;
  }
};

static_assert(sizeof(SlotEntry) == 4, "SlotEntry must be 4 bytes");

/**
 * @brief Page structure
 *
 * +------------------------+
 * | PageHeader (24 bytes)  |
 * +------------------------+
 * | SlotEntry[0]           |
 * | SlotEntry[1]           |
 * | ...                    |
 * +------------------------+
 * | Free Space             |
 * |                        |
 * +------------------------+
 * | Record N               |
 * | ...                    |
 * | Record 1               |
 * | Record 0               |
 * +------------------------+
 *
 * Slot directory grows downward, records grow upward.
 */
class Page {
public:
  /**
   * @brief Initialize a new page
   * @param page_id Page identifier
   * @param flags Initial flags
   */
  void init(uint32_t page_id, uint16_t flags = PageHeader::FLAG_LEAF);

  /**
   * @brief Get the page header
   */
  PageHeader &header() { return *reinterpret_cast<PageHeader *>(data_.data()); }
  const PageHeader &header() const {
    return *reinterpret_cast<const PageHeader *>(data_.data());
  }

  /**
   * @brief Get a slot entry
   * @param slot_index Slot index
   * @return Pointer to slot entry, or nullptr if invalid
   */
  SlotEntry *get_slot(uint16_t slot_index);
  const SlotEntry *get_slot(uint16_t slot_index) const;

  /**
   * @brief Get record data
   * @param slot_index Slot index
   * @param out_data Output pointer to record data
   * @param out_length Output record length
   * @return true if successful
   */
  bool get_record(uint16_t slot_index, const uint8_t **out_data,
                  uint16_t *out_length) const;

  /**
   * @brief Insert a record into the page
   * @param data Record data
   * @param length Record length
   * @param out_slot_index Output slot index
   * @return true if successful, false if no space
   */
  bool insert_record(const uint8_t *data, uint16_t length,
                     uint16_t *out_slot_index);

  /**
   * @brief Delete a record
   * @param slot_index Slot index to delete
   * @return true if successful
   */
  bool delete_record(uint16_t slot_index);

  /**
   * @brief Update a record in place
   * @param slot_index Slot index
   * @param data New record data
   * @param length New record length
   * @return true if successful (must fit in existing space)
   */
  bool update_record(uint16_t slot_index, const uint8_t *data, uint16_t length);

  /**
   * @brief Get available free space
   */
  uint16_t free_space() const { return header().free_space; }

  /**
   * @brief Get number of slots
   */
  uint16_t slot_count() const { return header().slot_count; }

  /**
   * @brief Get raw page data
   */
  uint8_t *data() { return data_.data(); }
  const uint8_t *data() const { return data_.data(); }

  static constexpr size_t size() { return PAGE_SIZE; }

private:
  uint16_t slot_directory_end() const {
    return sizeof(PageHeader) + header().slot_count * sizeof(SlotEntry);
  }

  std::array<uint8_t, PAGE_SIZE> data_;
};

static_assert(sizeof(Page) == PAGE_SIZE, "Page must be PAGE_SIZE bytes");

} // namespace storage
} // namespace edgesql
