#pragma once

/**
 * @file page_manager.hpp
 * @brief Page management for EdgeSQL Lite storage
 */

#include "page.hpp"
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgesql {
namespace storage {

/**
 * @brief Page manager
 *
 * Manages pages in memory with a simple buffer pool.
 */
class PageManager {
public:
  /**
   * @brief Constructor
   * @param data_dir Data directory path
   * @param max_pages Maximum number of pages to cache
   */
  PageManager(const std::string &data_dir, size_t max_pages = 1024);

  /**
   * @brief Destructor - flushes dirty pages
   */
  ~PageManager();

  // Non-copyable
  PageManager(const PageManager &) = delete;
  PageManager &operator=(const PageManager &) = delete;

  /**
   * @brief Initialize the page manager
   * @return true on success
   */
  bool init();

  /**
   * @brief Close the page manager
   */
  void close();

  /**
   * @brief Get a page by ID
   * @param table_id Table identifier
   * @param page_id Page identifier
   * @return Pointer to page, or nullptr if not found
   */
  Page *get_page(uint32_t table_id, uint32_t page_id);

  /**
   * @brief Allocate a new page
   * @param table_id Table identifier
   * @return Page ID of new page, or UINT32_MAX on failure
   */
  uint32_t allocate_page(uint32_t table_id);

  /**
   * @brief Mark a page as dirty
   * @param table_id Table identifier
   * @param page_id Page identifier
   */
  void mark_dirty(uint32_t table_id, uint32_t page_id);

  /**
   * @brief Flush a specific page to disk
   * @param table_id Table identifier
   * @param page_id Page identifier
   * @return true on success
   */
  bool flush_page(uint32_t table_id, uint32_t page_id);

  /**
   * @brief Flush all dirty pages to disk
   * @return Number of pages flushed
   */
  size_t flush_all();

  /**
   * @brief Get the number of pages in the buffer pool
   */
  size_t page_count() const;

  /**
   * @brief Get the number of dirty pages
   */
  size_t dirty_count() const;

  /**
   * @brief Create a new table file
   * @param table_id Table identifier
   * @return true on success
   */
  bool create_table_file(uint32_t table_id);

  /**
   * @brief Delete a table file
   * @param table_id Table identifier
   * @return true on success
   */
  bool delete_table_file(uint32_t table_id);

private:
  struct BufferEntry {
    uint32_t table_id;
    uint32_t page_id;
    std::unique_ptr<Page> page;
    bool dirty;
  };

  using PageKey = std::pair<uint32_t, uint32_t>; // (table_id, page_id)

  struct PageKeyHash {
    size_t operator()(const PageKey &key) const {
      return std::hash<uint64_t>()((static_cast<uint64_t>(key.first) << 32) |
                                   key.second);
    }
  };

  bool load_page(uint32_t table_id, uint32_t page_id);
  bool write_page(uint32_t table_id, uint32_t page_id, const Page *page);
  void evict_page();
  std::string table_file_path(uint32_t table_id) const;

  std::string data_dir_;
  size_t max_pages_;

  mutable std::mutex mutex_;
  std::unordered_map<PageKey, BufferEntry, PageKeyHash> buffer_pool_;
  std::list<PageKey> lru_list_; // For LRU eviction
  std::unordered_map<PageKey, std::list<PageKey>::iterator, PageKeyHash>
      lru_map_;

  std::unordered_map<uint32_t, uint32_t>
      next_page_id_; // Per-table next page ID
};

} // namespace storage
} // namespace edgesql
