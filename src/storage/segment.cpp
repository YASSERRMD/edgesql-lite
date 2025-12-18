/**
 * @file segment.cpp
 * @brief Segment implementation
 */

#include "segment.hpp"
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

namespace edgesql {
namespace storage {

// Segment implementation

Segment::Segment(const std::string &path, uint32_t table_id,
                 uint32_t segment_id)
    : path_(path), table_id_(table_id), segment_id_(segment_id) {}

Segment::~Segment() { close(); }

bool Segment::create() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (fd_ >= 0) {
    return true; // Already open
  }

  fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd_ < 0) {
    std::cerr << "Failed to create segment file: " << path_ << "\n";
    return false;
  }

  page_count_ = 0;
  created_lsn_ = 0;
  max_lsn_ = 0;

  if (!write_header()) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  return true;
}

bool Segment::open() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (fd_ >= 0) {
    return true; // Already open
  }

  fd_ = ::open(path_.c_str(), O_RDWR);
  if (fd_ < 0) {
    return false;
  }

  if (!read_header()) {
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  return true;
}

void Segment::close() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (fd_ >= 0) {
    fsync(fd_);
    ::close(fd_);
    fd_ = -1;
  }
}

bool Segment::read_page(uint32_t page_offset, Page *page) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (fd_ < 0 || page_offset >= page_count_) {
    return false;
  }

  off_t offset =
      sizeof(SegmentHeader) + static_cast<off_t>(page_offset) * PAGE_SIZE;
  ssize_t bytes_read = pread(fd_, page->data(), PAGE_SIZE, offset);

  return bytes_read == static_cast<ssize_t>(PAGE_SIZE);
}

bool Segment::write_page(uint32_t page_offset, const Page *page) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (fd_ < 0 || page_offset > page_count_) {
    return false;
  }

  off_t offset =
      sizeof(SegmentHeader) + static_cast<off_t>(page_offset) * PAGE_SIZE;
  ssize_t bytes_written = pwrite(fd_, page->data(), PAGE_SIZE, offset);

  if (bytes_written != static_cast<ssize_t>(PAGE_SIZE)) {
    return false;
  }

  // Update max LSN
  if (page->header().lsn > max_lsn_) {
    max_lsn_ = page->header().lsn;
  }

  return true;
}

uint32_t Segment::append_page(const Page *page) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (fd_ < 0) {
    return UINT32_MAX;
  }

  uint32_t page_offset = page_count_;
  off_t offset =
      sizeof(SegmentHeader) + static_cast<off_t>(page_offset) * PAGE_SIZE;
  ssize_t bytes_written = pwrite(fd_, page->data(), PAGE_SIZE, offset);

  if (bytes_written != static_cast<ssize_t>(PAGE_SIZE)) {
    return UINT32_MAX;
  }

  page_count_++;

  // Update max LSN
  if (page->header().lsn > max_lsn_) {
    max_lsn_ = page->header().lsn;
  }

  // Update header
  write_header();

  return page_offset;
}

bool Segment::sync() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (fd_ < 0) {
    return false;
  }

  return fsync(fd_) == 0;
}

bool Segment::write_header() {
  SegmentHeader header{};
  header.magic = SegmentHeader::SEGMENT_MAGIC;
  header.segment_id = segment_id_;
  header.table_id = table_id_;
  header.page_count = page_count_;
  header.created_lsn = created_lsn_;
  header.max_lsn = max_lsn_;

  ssize_t bytes_written = pwrite(fd_, &header, sizeof(header), 0);
  return bytes_written == static_cast<ssize_t>(sizeof(header));
}

bool Segment::read_header() {
  SegmentHeader header{};
  ssize_t bytes_read = pread(fd_, &header, sizeof(header), 0);

  if (bytes_read != static_cast<ssize_t>(sizeof(header))) {
    return false;
  }

  if (!header.is_valid()) {
    return false;
  }

  if (header.table_id != table_id_ || header.segment_id != segment_id_) {
    return false;
  }

  page_count_ = header.page_count;
  created_lsn_ = header.created_lsn;
  max_lsn_ = header.max_lsn;

  return true;
}

// SegmentManager implementation

SegmentManager::SegmentManager(const std::string &data_dir,
                               const SegmentConfig &config)
    : data_dir_(data_dir), config_(config) {}

SegmentManager::~SegmentManager() { flush_all(); }

bool SegmentManager::init() {
  std::lock_guard<std::mutex> lock(mutex_);

  // Create data directory if it doesn't exist
  std::error_code ec;
  if (!std::filesystem::exists(data_dir_)) {
    if (!std::filesystem::create_directories(data_dir_, ec)) {
      std::cerr << "Failed to create data directory: " << data_dir_ << "\n";
      return false;
    }
  }

  // Scan for existing segment files
  for (const auto &entry : std::filesystem::directory_iterator(data_dir_)) {
    if (entry.is_regular_file()) {
      std::string filename = entry.path().filename().string();
      // Parse segment filename: segment_<table_id>_<segment_id>.seg
      if (filename.substr(0, 8) == "segment_" &&
          filename.substr(filename.length() - 4) == ".seg") {
        // Extract table_id and segment_id
        size_t first_underscore = filename.find('_');
        size_t second_underscore = filename.find('_', first_underscore + 1);

        if (first_underscore != std::string::npos &&
            second_underscore != std::string::npos) {
          uint32_t table_id = std::stoul(filename.substr(
              first_underscore + 1, second_underscore - first_underscore - 1));
          uint32_t segment_id = std::stoul(
              filename.substr(second_underscore + 1,
                              filename.length() - second_underscore - 5));

          auto segment = std::make_unique<Segment>(entry.path().string(),
                                                   table_id, segment_id);

          if (segment->open()) {
            segments_[table_id].push_back(std::move(segment));

            // Track active segment
            if (active_segment_.find(table_id) == active_segment_.end() ||
                segment_id > active_segment_[table_id]) {
              active_segment_[table_id] = segment_id;
            }
          }
        }
      }
    }
  }

  return true;
}

bool SegmentManager::create_table(uint32_t table_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (segments_.find(table_id) != segments_.end()) {
    return true; // Already exists
  }

  // Create first segment
  auto segment =
      std::make_unique<Segment>(segment_path(table_id, 0), table_id, 0);

  if (!segment->create()) {
    return false;
  }

  segments_[table_id].push_back(std::move(segment));
  active_segment_[table_id] = 0;

  return true;
}

bool SegmentManager::drop_table(uint32_t table_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = segments_.find(table_id);
  if (it == segments_.end()) {
    return true; // Doesn't exist
  }

  // Close and delete all segments
  for (auto &segment : it->second) {
    std::string path = segment->path();
    segment->close();
    std::filesystem::remove(path);
  }

  segments_.erase(it);
  active_segment_.erase(table_id);

  return true;
}

Segment *SegmentManager::get_active_segment(uint32_t table_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = active_segment_.find(table_id);
  if (it == active_segment_.end()) {
    return nullptr;
  }

  auto seg_it = segments_.find(table_id);
  if (seg_it == segments_.end() || it->second >= seg_it->second.size()) {
    return nullptr;
  }

  Segment *segment = seg_it->second[it->second].get();

  // Check if segment is full and needs rotation
  if (segment->is_full(config_)) {
    return rotate_segment(table_id);
  }

  return segment;
}

Segment *SegmentManager::get_segment(uint32_t table_id, uint32_t segment_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = segments_.find(table_id);
  if (it == segments_.end() || segment_id >= it->second.size()) {
    return nullptr;
  }

  return it->second[segment_id].get();
}

Segment *SegmentManager::rotate_segment(uint32_t table_id) {
  // Note: mutex already held by caller

  auto it = active_segment_.find(table_id);
  if (it == active_segment_.end()) {
    return nullptr;
  }

  uint32_t new_segment_id = it->second + 1;

  auto segment = std::make_unique<Segment>(
      segment_path(table_id, new_segment_id), table_id, new_segment_id);

  if (!segment->create()) {
    return nullptr;
  }

  Segment *result = segment.get();
  segments_[table_id].push_back(std::move(segment));
  active_segment_[table_id] = new_segment_id;

  return result;
}

void SegmentManager::flush_all() {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto &[table_id, segs] : segments_) {
    for (auto &segment : segs) {
      segment->sync();
    }
  }
}

std::string SegmentManager::segment_path(uint32_t table_id,
                                         uint32_t segment_id) const {
  return data_dir_ + "/segment_" + std::to_string(table_id) + "_" +
         std::to_string(segment_id) + ".seg";
}

} // namespace storage
} // namespace edgesql
