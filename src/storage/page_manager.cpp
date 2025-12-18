/**
 * @file page_manager.cpp
 * @brief Page manager implementation
 */

#include "page_manager.hpp"
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace edgesql {
namespace storage {

PageManager::PageManager(const std::string &data_dir, size_t max_pages)
    : data_dir_(data_dir), max_pages_(max_pages) {}

PageManager::~PageManager() { close(); }

bool PageManager::init() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Create data directory if it doesn't exist
  std::error_code ec;
  if (!std::filesystem::exists(data_dir_)) {
    if (!std::filesystem::create_directories(data_dir_, ec)) {
      std::cerr << "Failed to create data directory: " << data_dir_ << " - "
                << ec.message() << "\n";
      return false;
    }
  }

  return true;
}

void PageManager::close() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Flush all dirty pages
  for (auto &[key, entry] : buffer_pool_) {
    if (entry.dirty) {
      write_page(entry.table_id, entry.page_id, entry.page.get());
    }
  }

  buffer_pool_.clear();
  lru_list_.clear();
  lru_map_.clear();
}

Page *PageManager::get_page(uint32_t table_id, uint32_t page_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  PageKey key{table_id, page_id};

  auto it = buffer_pool_.find(key);
  if (it != buffer_pool_.end()) {
    // Move to front of LRU list
    auto lru_it = lru_map_.find(key);
    if (lru_it != lru_map_.end()) {
      lru_list_.erase(lru_it->second);
      lru_list_.push_front(key);
      lru_map_[key] = lru_list_.begin();
    }
    return it->second.page.get();
  }

  // Load from disk
  if (!load_page(table_id, page_id)) {
    return nullptr;
  }

  return buffer_pool_[key].page.get();
}

uint32_t PageManager::allocate_page(uint32_t table_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Get next page ID for this table
  uint32_t page_id = next_page_id_[table_id]++;

  // Evict if necessary
  while (buffer_pool_.size() >= max_pages_) {
    evict_page();
  }

  // Create new page
  PageKey key{table_id, page_id};
  BufferEntry entry;
  entry.table_id = table_id;
  entry.page_id = page_id;
  entry.page = std::make_unique<Page>();
  entry.page->init(page_id);
  entry.dirty = true;

  buffer_pool_[key] = std::move(entry);
  lru_list_.push_front(key);
  lru_map_[key] = lru_list_.begin();

  return page_id;
}

void PageManager::mark_dirty(uint32_t table_id, uint32_t page_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  PageKey key{table_id, page_id};
  auto it = buffer_pool_.find(key);
  if (it != buffer_pool_.end()) {
    it->second.dirty = true;
    it->second.page->header().set_dirty(true);
  }
}

bool PageManager::flush_page(uint32_t table_id, uint32_t page_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  PageKey key{table_id, page_id};
  auto it = buffer_pool_.find(key);
  if (it == buffer_pool_.end() || !it->second.dirty) {
    return true; // Nothing to flush
  }

  if (!write_page(table_id, page_id, it->second.page.get())) {
    return false;
  }

  it->second.dirty = false;
  it->second.page->header().set_dirty(false);
  return true;
}

size_t PageManager::flush_all() {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t count = 0;
  for (auto &[key, entry] : buffer_pool_) {
    if (entry.dirty) {
      if (write_page(entry.table_id, entry.page_id, entry.page.get())) {
        entry.dirty = false;
        entry.page->header().set_dirty(false);
        count++;
      }
    }
  }

  return count;
}

size_t PageManager::page_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return buffer_pool_.size();
}

size_t PageManager::dirty_count() const {
  std::lock_guard<std::mutex> lock(mutex_);

  size_t count = 0;
  for (const auto &[key, entry] : buffer_pool_) {
    if (entry.dirty)
      count++;
  }
  return count;
}

bool PageManager::create_table_file(uint32_t table_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  std::string path = table_file_path(table_id);
  std::ofstream file(path, std::ios::binary);

  if (!file.is_open()) {
    std::cerr << "Failed to create table file: " << path << "\n";
    return false;
  }

  next_page_id_[table_id] = 0;
  return true;
}

bool PageManager::delete_table_file(uint32_t table_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Remove all pages for this table from buffer pool
  std::vector<PageKey> to_remove;
  for (const auto &[key, entry] : buffer_pool_) {
    if (key.first == table_id) {
      to_remove.push_back(key);
    }
  }

  for (const auto &key : to_remove) {
    auto lru_it = lru_map_.find(key);
    if (lru_it != lru_map_.end()) {
      lru_list_.erase(lru_it->second);
      lru_map_.erase(lru_it);
    }
    buffer_pool_.erase(key);
  }

  // Delete the file
  std::string path = table_file_path(table_id);
  std::error_code ec;
  std::filesystem::remove(path, ec);

  next_page_id_.erase(table_id);

  return !ec;
}

bool PageManager::load_page(uint32_t table_id, uint32_t page_id) {
  // Evict if necessary
  while (buffer_pool_.size() >= max_pages_) {
    evict_page();
  }

  std::string path = table_file_path(table_id);
  std::ifstream file(path, std::ios::binary);

  if (!file.is_open()) {
    return false;
  }

  // Seek to page
  file.seekg(static_cast<std::streamoff>(page_id * PAGE_SIZE));

  if (!file.good()) {
    return false;
  }

  // Read page
  auto page = std::make_unique<Page>();
  file.read(reinterpret_cast<char *>(page->data()), PAGE_SIZE);

  if (file.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
    return false;
  }

  // Validate page
  if (!page->header().is_valid()) {
    return false;
  }

  // Add to buffer pool
  PageKey key{table_id, page_id};
  BufferEntry entry;
  entry.table_id = table_id;
  entry.page_id = page_id;
  entry.page = std::move(page);
  entry.dirty = false;

  buffer_pool_[key] = std::move(entry);
  lru_list_.push_front(key);
  lru_map_[key] = lru_list_.begin();

  return true;
}

bool PageManager::write_page(uint32_t table_id, uint32_t page_id,
                             const Page *page) {
  std::string path = table_file_path(table_id);
  std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);

  if (!file.is_open()) {
    // Create file if it doesn't exist
    std::ofstream new_file(path, std::ios::binary);
    if (!new_file.is_open()) {
      std::cerr << "Failed to create table file: " << path << "\n";
      return false;
    }
    new_file.close();

    file.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
      return false;
    }
  }

  // Seek to page position
  file.seekp(static_cast<std::streamoff>(page_id * PAGE_SIZE));

  // Write page
  file.write(reinterpret_cast<const char *>(page->data()), PAGE_SIZE);

  if (!file.good()) {
    return false;
  }

  file.flush();
  return true;
}

void PageManager::evict_page() {
  if (lru_list_.empty()) {
    return;
  }

  // Find a non-dirty page to evict from the back of LRU list
  for (auto it = lru_list_.rbegin(); it != lru_list_.rend(); ++it) {
    const PageKey &key = *it;
    auto buf_it = buffer_pool_.find(key);

    if (buf_it != buffer_pool_.end()) {
      if (buf_it->second.dirty) {
        // Write dirty page before evicting
        write_page(key.first, key.second, buf_it->second.page.get());
      }

      // Remove from buffer pool and LRU
      buffer_pool_.erase(buf_it);
      lru_map_.erase(key);
      lru_list_.erase(std::next(it).base());
      return;
    }
  }
}

std::string PageManager::table_file_path(uint32_t table_id) const {
  return data_dir_ + "/table_" + std::to_string(table_id) + ".dat";
}

// Page implementation

void Page::init(uint32_t page_id, uint16_t flags) {
  std::memset(data_.data(), 0, PAGE_SIZE);

  auto &hdr = header();
  hdr.magic = PAGE_MAGIC;
  hdr.page_id = page_id;
  hdr.lsn = 0;
  hdr.slot_count = 0;
  hdr.free_space = static_cast<uint16_t>(PAGE_SIZE - sizeof(PageHeader));
  hdr.data_start = static_cast<uint16_t>(PAGE_SIZE);
  hdr.flags = flags;
}

SlotEntry *Page::get_slot(uint16_t slot_index) {
  if (slot_index >= header().slot_count) {
    return nullptr;
  }

  size_t offset = sizeof(PageHeader) + slot_index * sizeof(SlotEntry);
  return reinterpret_cast<SlotEntry *>(data_.data() + offset);
}

const SlotEntry *Page::get_slot(uint16_t slot_index) const {
  if (slot_index >= header().slot_count) {
    return nullptr;
  }

  size_t offset = sizeof(PageHeader) + slot_index * sizeof(SlotEntry);
  return reinterpret_cast<const SlotEntry *>(data_.data() + offset);
}

bool Page::get_record(uint16_t slot_index, const uint8_t **out_data,
                      uint16_t *out_length) const {
  const SlotEntry *slot = get_slot(slot_index);
  if (!slot || slot->is_empty() || slot->is_deleted()) {
    return false;
  }

  *out_data = data_.data() + slot->offset;
  *out_length = slot->length;
  return true;
}

bool Page::insert_record(const uint8_t *data, uint16_t length,
                         uint16_t *out_slot_index) {
  // Check if we have enough space
  uint16_t required_space = length + sizeof(SlotEntry);
  if (header().free_space < required_space) {
    return false;
  }

  // Allocate space for record (grows upward from end)
  uint16_t record_offset = header().data_start - length;

  // Check that record doesn't overlap with slot directory
  uint16_t slot_dir_end = slot_directory_end() + sizeof(SlotEntry);
  if (record_offset < slot_dir_end) {
    return false;
  }

  // Copy record data
  std::memcpy(data_.data() + record_offset, data, length);

  // Add slot entry
  uint16_t slot_index = header().slot_count;
  SlotEntry *slot = reinterpret_cast<SlotEntry *>(
      data_.data() + sizeof(PageHeader) + slot_index * sizeof(SlotEntry));
  slot->offset = record_offset;
  slot->length = length;

  // Update header
  header().slot_count++;
  header().data_start = record_offset;
  header().free_space -= required_space;
  header().set_dirty(true);

  *out_slot_index = slot_index;
  return true;
}

bool Page::delete_record(uint16_t slot_index) {
  SlotEntry *slot = get_slot(slot_index);
  if (!slot || slot->is_empty() || slot->is_deleted()) {
    return false;
  }

  // Mark slot as deleted
  // Note: We don't reclaim space here - that would require compaction
  slot->mark_deleted();
  header().set_dirty(true);

  return true;
}

bool Page::update_record(uint16_t slot_index, const uint8_t *data,
                         uint16_t length) {
  SlotEntry *slot = get_slot(slot_index);
  if (!slot || slot->is_empty() || slot->is_deleted()) {
    return false;
  }

  // Only allow in-place update if new data fits in existing slot
  if (length > slot->length) {
    return false;
  }

  // Copy new data
  std::memcpy(data_.data() + slot->offset, data, length);

  // Update slot length (may leave some wasted space)
  slot->length = length;
  header().set_dirty(true);

  return true;
}

} // namespace storage
} // namespace edgesql
