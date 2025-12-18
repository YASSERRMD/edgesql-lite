#pragma once

/**
 * @file wal.hpp
 * @brief Write-Ahead Log for crash recovery
 */

#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace edgesql {
namespace storage {

// WAL magic number
constexpr uint32_t WAL_MAGIC = 0x57414C45; // "WALE"

/**
 * @brief WAL record types
 */
enum class WalRecordType : uint8_t {
  INVALID = 0,
  INSERT = 1,
  UPDATE = 2,
  DELETE = 3,
  CREATE_TABLE = 4,
  DROP_TABLE = 5,
  CHECKPOINT = 6,
  COMMIT = 7,
  ROLLBACK = 8
};

/**
 * @brief WAL record header
 */
struct WalRecordHeader {
  uint64_t lsn;        // Log sequence number
  uint32_t length;     // Total record length including header
  uint32_t crc32;      // CRC32 checksum of payload
  WalRecordType type;  // Record type
  uint8_t reserved[3]; // Reserved for alignment
  uint32_t table_id;   // Table identifier
  uint32_t page_id;    // Page identifier
  uint16_t slot_id;    // Slot identifier
  uint16_t padding;    // Padding for alignment
};

static_assert(sizeof(WalRecordHeader) == 32,
              "WalRecordHeader must be 32 bytes");

/**
 * @brief WAL record
 */
struct WalRecord {
  WalRecordHeader header;
  std::vector<uint8_t> payload;

  /**
   * @brief Calculate CRC32 of payload
   */
  uint32_t calculate_crc32() const;

  /**
   * @brief Validate the record
   */
  bool is_valid() const;

  /**
   * @brief Serialize to buffer
   */
  size_t serialize(uint8_t *buffer, size_t buffer_size) const;

  /**
   * @brief Get serialized size
   */
  size_t serialized_size() const {
    return sizeof(WalRecordHeader) + payload.size();
  }

  /**
   * @brief Deserialize from buffer
   */
  bool deserialize(const uint8_t *data, size_t length);
};

/**
 * @brief Write-Ahead Log
 *
 * Provides durability through logging all changes before applying them.
 */
class Wal {
public:
  /**
   * @brief Constructor
   * @param path Path to WAL file
   */
  explicit Wal(const std::string &path);

  /**
   * @brief Destructor - flushes and closes
   */
  ~Wal();

  // Non-copyable
  Wal(const Wal &) = delete;
  Wal &operator=(const Wal &) = delete;

  /**
   * @brief Open the WAL file
   * @return true on success
   */
  bool open();

  /**
   * @brief Close the WAL file
   */
  void close();

  /**
   * @brief Append a record to the WAL
   * @param record Record to append
   * @return LSN of the record, or 0 on failure
   */
  uint64_t append(const WalRecord &record);

  /**
   * @brief Sync WAL to disk
   * @return true on success
   */
  bool sync();

  /**
   * @brief Get current LSN
   */
  uint64_t current_lsn() const { return current_lsn_; }

  /**
   * @brief Read all records from the WAL
   * @param records Output vector of records
   * @return true on success
   */
  bool read_all(std::vector<WalRecord> &records);

  /**
   * @brief Read records starting from a specific LSN
   * @param start_lsn Starting LSN
   * @param records Output vector of records
   * @return true on success
   */
  bool read_from(uint64_t start_lsn, std::vector<WalRecord> &records);

  /**
   * @brief Create a checkpoint
   * @return LSN of the checkpoint record
   */
  uint64_t checkpoint();

  /**
   * @brief Truncate WAL up to specified LSN
   * @param lsn LSN up to which to truncate
   * @return true on success
   */
  bool truncate(uint64_t lsn);

  /**
   * @brief Get WAL file size
   */
  size_t file_size() const;

  /**
   * @brief Check if WAL is open
   */
  bool is_open() const { return is_open_; }

private:
  bool write_header();
  bool read_header();

  std::string path_;
  std::fstream file_;
  std::mutex mutex_;

  uint64_t current_lsn_{1};
  bool is_open_{false};
  std::vector<uint8_t> write_buffer_;
};

/**
 * @brief WAL file header
 */
struct WalFileHeader {
  uint32_t magic;
  uint32_t version;
  uint64_t first_lsn;
  uint64_t last_checkpoint_lsn;

  static constexpr uint32_t CURRENT_VERSION = 1;

  bool is_valid() const {
    return magic == WAL_MAGIC && version == CURRENT_VERSION;
  }
};

static_assert(sizeof(WalFileHeader) == 24, "WalFileHeader must be 24 bytes");

} // namespace storage
} // namespace edgesql
