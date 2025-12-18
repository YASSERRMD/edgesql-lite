/**
 * @file wal.cpp
 * @brief Write-Ahead Log implementation
 */

#include "wal.hpp"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace edgesql {
namespace storage {

// CRC32 lookup table
namespace {

uint32_t crc32_table[256];
bool crc32_table_initialized = false;

void init_crc32_table() {
  if (crc32_table_initialized)
    return;

  for (uint32_t i = 0; i < 256; i++) {
    uint32_t crc = i;
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
    }
    crc32_table[i] = crc;
  }
  crc32_table_initialized = true;
}

uint32_t compute_crc32(const uint8_t *data, size_t length) {
  init_crc32_table();

  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFF;
}

} // anonymous namespace

// WalRecord implementation

uint32_t WalRecord::calculate_crc32() const {
  return compute_crc32(payload.data(), payload.size());
}

bool WalRecord::is_valid() const { return header.crc32 == calculate_crc32(); }

size_t WalRecord::serialize(uint8_t *buffer, size_t buffer_size) const {
  size_t total_size = serialized_size();
  if (buffer_size < total_size) {
    return 0;
  }

  std::memcpy(buffer, &header, sizeof(WalRecordHeader));
  if (!payload.empty()) {
    std::memcpy(buffer + sizeof(WalRecordHeader), payload.data(),
                payload.size());
  }

  return total_size;
}

bool WalRecord::deserialize(const uint8_t *data, size_t length) {
  if (length < sizeof(WalRecordHeader)) {
    return false;
  }

  std::memcpy(&header, data, sizeof(WalRecordHeader));

  size_t payload_size = header.length - sizeof(WalRecordHeader);
  if (length < header.length) {
    return false;
  }

  payload.resize(payload_size);
  if (payload_size > 0) {
    std::memcpy(payload.data(), data + sizeof(WalRecordHeader), payload_size);
  }

  return is_valid();
}

// Wal implementation

Wal::Wal(const std::string &path) : path_(path) {
  write_buffer_.reserve(64 * 1024); // 64KB write buffer
}

Wal::~Wal() {
  if (is_open_) {
    sync();
    close();
  }
}

bool Wal::open() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (is_open_) {
    return true;
  }

  // Try to open existing file
  file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);

  if (file_.is_open()) {
    // Read existing header
    if (!read_header()) {
      file_.close();
      return false;
    }
  } else {
    // Create new file
    file_.open(path_, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) {
      std::cerr << "Failed to create WAL file: " << path_ << "\n";
      return false;
    }

    if (!write_header()) {
      file_.close();
      return false;
    }

    // Reopen in read/write mode
    file_.close();
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
      return false;
    }
  }

  // Seek to end for appending
  file_.seekp(0, std::ios::end);

  is_open_ = true;
  return true;
}

void Wal::close() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_open_) {
    return;
  }

  file_.close();
  is_open_ = false;
}

uint64_t Wal::append(const WalRecord &record) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_open_) {
    return 0;
  }

  // Create a copy with updated LSN and CRC
  WalRecord rec = record;
  rec.header.lsn = current_lsn_;
  rec.header.length =
      static_cast<uint32_t>(sizeof(WalRecordHeader) + rec.payload.size());
  rec.header.crc32 = rec.calculate_crc32();

  // Serialize
  write_buffer_.resize(rec.serialized_size());
  rec.serialize(write_buffer_.data(), write_buffer_.size());

  // Write to file
  file_.write(reinterpret_cast<const char *>(write_buffer_.data()),
              static_cast<std::streamsize>(write_buffer_.size()));

  if (!file_.good()) {
    std::cerr << "Failed to write WAL record\n";
    return 0;
  }

  return current_lsn_++;
}

bool Wal::sync() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_open_) {
    return false;
  }

  file_.flush();

  // Use fsync for durability
  // Note: fstream doesn't expose file descriptor directly,
  // so we flush and rely on OS buffering for now
  // In production, we'd use native file I/O

  return file_.good();
}

bool Wal::read_all(std::vector<WalRecord> &records) {
  return read_from(1, records);
}

bool Wal::read_from(uint64_t start_lsn, std::vector<WalRecord> &records) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!is_open_) {
    return false;
  }

  records.clear();

  // Seek to beginning (after header)
  file_.seekg(sizeof(WalFileHeader), std::ios::beg);

  std::vector<uint8_t> buffer(64 * 1024); // 64KB read buffer

  while (file_.good() && !file_.eof()) {
    // Read header first
    file_.read(reinterpret_cast<char *>(buffer.data()),
               sizeof(WalRecordHeader));

    if (file_.gcount() <
        static_cast<std::streamsize>(sizeof(WalRecordHeader))) {
      break; // End of file or error
    }

    WalRecordHeader header;
    std::memcpy(&header, buffer.data(), sizeof(WalRecordHeader));

    // Check for valid record
    if (header.length < sizeof(WalRecordHeader) ||
        header.length > buffer.size()) {
      break; // Invalid record
    }

    // Read payload
    size_t payload_size = header.length - sizeof(WalRecordHeader);
    if (payload_size > 0) {
      file_.read(
          reinterpret_cast<char *>(buffer.data() + sizeof(WalRecordHeader)),
          static_cast<std::streamsize>(payload_size));

      if (file_.gcount() < static_cast<std::streamsize>(payload_size)) {
        break; // Incomplete record
      }
    }

    // Deserialize
    WalRecord record;
    if (record.deserialize(buffer.data(), header.length)) {
      if (record.header.lsn >= start_lsn) {
        records.push_back(std::move(record));
      }
    } else {
      break; // Invalid record
    }
  }

  // Seek back to end for appending
  file_.clear();
  file_.seekp(0, std::ios::end);

  return true;
}

uint64_t Wal::checkpoint() {
  WalRecord record;
  record.header.type = WalRecordType::CHECKPOINT;
  record.header.table_id = 0;
  record.header.page_id = 0;
  record.header.slot_id = 0;

  return append(record);
}

bool Wal::truncate(uint64_t /*lsn*/) {
  // TODO: Implement WAL truncation
  // This would create a new WAL file with only records after lsn
  return true;
}

size_t Wal::file_size() const {
  if (!is_open_) {
    return 0;
  }

  auto &f = const_cast<std::fstream &>(file_);
  auto current = f.tellg();
  f.seekg(0, std::ios::end);
  auto size = f.tellg();
  f.seekg(current);

  return static_cast<size_t>(size);
}

bool Wal::write_header() {
  WalFileHeader header{};
  header.magic = WAL_MAGIC;
  header.version = WalFileHeader::CURRENT_VERSION;
  header.first_lsn = 1;
  header.last_checkpoint_lsn = 0;

  file_.seekp(0, std::ios::beg);
  file_.write(reinterpret_cast<const char *>(&header), sizeof(header));

  return file_.good();
}

bool Wal::read_header() {
  WalFileHeader header{};

  file_.seekg(0, std::ios::beg);
  file_.read(reinterpret_cast<char *>(&header), sizeof(header));

  if (!file_.good() || !header.is_valid()) {
    return false;
  }

  // Find the last LSN by reading through the file
  file_.seekg(sizeof(WalFileHeader), std::ios::beg);

  WalRecordHeader rec_header;
  while (
      file_.read(reinterpret_cast<char *>(&rec_header), sizeof(rec_header))) {
    if (rec_header.length < sizeof(WalRecordHeader)) {
      break;
    }

    current_lsn_ = rec_header.lsn + 1;

    // Skip payload
    size_t payload_size = rec_header.length - sizeof(WalRecordHeader);
    file_.seekg(static_cast<std::streamoff>(payload_size), std::ios::cur);
  }

  file_.clear();
  return true;
}

} // namespace storage
} // namespace edgesql
