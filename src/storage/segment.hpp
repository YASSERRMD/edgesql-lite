#pragma once

/**
 * @file segment.hpp
 * @brief Segment management for append-only storage
 */

#include "page.hpp"
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace edgesql {
namespace storage {

/**
 * @brief Segment header
 */
struct SegmentHeader {
  uint32_t magic;
  uint32_t segment_id;
  uint32_t table_id;
  uint32_t page_count;
  uint64_t created_lsn;
  uint64_t max_lsn;

  static constexpr uint32_t SEGMENT_MAGIC = 0x53454745; // "SEGE"

  bool is_valid() const { return magic == SEGMENT_MAGIC; }
};

/**
 * @brief Segment configuration
 */
struct SegmentConfig {
  size_t max_pages = 1024;                    // Max pages per segment
  size_t target_size_bytes = 8 * 1024 * 1024; // 8MB target size
};

/**
 * @brief Segment file
 *
 * Represents a single segment file containing multiple pages.
 */
class Segment {
public:
  /**
   * @brief Constructor
   * @param path Segment file path
   * @param table_id Table identifier
   * @param segment_id Segment identifier
   */
  Segment(const std::string &path, uint32_t table_id, uint32_t segment_id);

  /**
   * @brief Destructor
   */
  ~Segment();

  // Non-copyable
  Segment(const Segment &) = delete;
  Segment &operator=(const Segment &) = delete;

  /**
   * @brief Create a new segment file
   */
  bool create();

  /**
   * @brief Open an existing segment file
   */
  bool open();

  /**
   * @brief Close the segment file
   */
  void close();

  /**
   * @brief Read a page from the segment
   * @param page_offset Page offset within segment
   * @param page Output page
   * @return true on success
   */
  bool read_page(uint32_t page_offset, Page *page);

  /**
   * @brief Write a page to the segment
   * @param page_offset Page offset within segment
   * @param page Page to write
   * @return true on success
   */
  bool write_page(uint32_t page_offset, const Page *page);

  /**
   * @brief Append a new page to the segment
   * @param page Page to append
   * @return Page offset, or UINT32_MAX on failure
   */
  uint32_t append_page(const Page *page);

  /**
   * @brief Sync segment to disk
   */
  bool sync();

  /**
   * @brief Get segment ID
   */
  uint32_t segment_id() const { return segment_id_; }

  /**
   * @brief Get table ID
   */
  uint32_t table_id() const { return table_id_; }

  /**
   * @brief Get page count
   */
  uint32_t page_count() const { return page_count_; }

  /**
   * @brief Check if segment is full
   */
  bool is_full(const SegmentConfig &config) const {
    return page_count_ >= config.max_pages;
  }

  /**
   * @brief Get file path
   */
  const std::string &path() const { return path_; }

private:
  bool write_header();
  bool read_header();

  std::string path_;
  uint32_t table_id_;
  uint32_t segment_id_;
  uint32_t page_count_{0};
  uint64_t created_lsn_{0};
  uint64_t max_lsn_{0};

  int fd_{-1};
  std::mutex mutex_;
};

/**
 * @brief Segment manager
 *
 * Manages multiple segments for a table.
 */
class SegmentManager {
public:
  /**
   * @brief Constructor
   * @param data_dir Data directory path
   * @param config Segment configuration
   */
  SegmentManager(const std::string &data_dir, const SegmentConfig &config = {});

  /**
   * @brief Destructor
   */
  ~SegmentManager();

  /**
   * @brief Initialize the segment manager
   */
  bool init();

  /**
   * @brief Create segments for a new table
   * @param table_id Table identifier
   */
  bool create_table(uint32_t table_id);

  /**
   * @brief Drop a table and its segments
   * @param table_id Table identifier
   */
  bool drop_table(uint32_t table_id);

  /**
   * @brief Get the active segment for a table (for writing)
   * @param table_id Table identifier
   * @return Pointer to segment, or nullptr
   */
  Segment *get_active_segment(uint32_t table_id);

  /**
   * @brief Get a specific segment
   * @param table_id Table identifier
   * @param segment_id Segment identifier
   * @return Pointer to segment, or nullptr
   */
  Segment *get_segment(uint32_t table_id, uint32_t segment_id);

  /**
   * @brief Rotate to a new segment
   * @param table_id Table identifier
   * @return New segment, or nullptr on failure
   */
  Segment *rotate_segment(uint32_t table_id);

  /**
   * @brief Flush all segments
   */
  void flush_all();

private:
  std::string segment_path(uint32_t table_id, uint32_t segment_id) const;
  bool load_table_segments(uint32_t table_id);

  std::string data_dir_;
  SegmentConfig config_;

  std::mutex mutex_;
  std::unordered_map<uint32_t, std::vector<std::unique_ptr<Segment>>> segments_;
  std::unordered_map<uint32_t, uint32_t>
      active_segment_; // table_id -> segment_id
};

} // namespace storage
} // namespace edgesql
