#pragma once

/**
 * @file recovery.hpp
 * @brief Startup recovery for crash recovery
 */

#include "page_manager.hpp"
#include "wal.hpp"
#include <cstdint>
#include <functional>
#include <string>

namespace edgesql {
namespace storage {

/**
 * @brief Recovery statistics
 */
struct RecoveryStats {
  uint64_t records_processed = 0;
  uint64_t records_applied = 0;
  uint64_t records_skipped = 0;
  uint64_t errors = 0;
  uint64_t start_lsn = 0;
  uint64_t end_lsn = 0;
};

/**
 * @brief Recovery manager
 *
 * Handles crash recovery by replaying WAL records.
 */
class RecoveryManager {
public:
  /**
   * @brief Record application callback
   *
   * Called for each WAL record during recovery.
   * Return true to continue, false to abort recovery.
   */
  using RecordCallback = std::function<bool(const WalRecord &)>;

  /**
   * @brief Constructor
   * @param wal WAL instance
   * @param page_manager Page manager instance
   */
  RecoveryManager(Wal &wal, PageManager &page_manager);

  /**
   * @brief Perform recovery
   *
   * Replays WAL records from the last checkpoint.
   * @return true if recovery succeeded
   */
  bool recover();

  /**
   * @brief Perform recovery with custom callback
   * @param callback Callback for each record
   * @return true if recovery succeeded
   */
  bool recover(RecordCallback callback);

  /**
   * @brief Get recovery statistics
   */
  const RecoveryStats &stats() const { return stats_; }

  /**
   * @brief Check if recovery is needed
   */
  bool needs_recovery() const;

  /**
   * @brief Find the last valid checkpoint LSN
   */
  uint64_t find_last_checkpoint();

private:
  bool apply_record(const WalRecord &record);
  bool apply_insert(const WalRecord &record);
  bool apply_update(const WalRecord &record);
  bool apply_delete(const WalRecord &record);

  Wal &wal_;
  PageManager &page_manager_;
  RecoveryStats stats_;
};

/**
 * @brief Checkpoint manager
 *
 * Manages checkpoints for faster recovery.
 */
class CheckpointManager {
public:
  /**
   * @brief Constructor
   * @param wal WAL instance
   * @param page_manager Page manager instance
   */
  CheckpointManager(Wal &wal, PageManager &page_manager);

  /**
   * @brief Perform a checkpoint
   *
   * Flushes all dirty pages and writes a checkpoint record.
   * @return Checkpoint LSN
   */
  uint64_t checkpoint();

  /**
   * @brief Check if checkpoint is needed
   * @param wal_size_threshold Trigger checkpoint if WAL exceeds this size
   * @return true if checkpoint should be performed
   */
  bool should_checkpoint(size_t wal_size_threshold = 64 * 1024 * 1024) const;

  /**
   * @brief Get the last checkpoint LSN
   */
  uint64_t last_checkpoint_lsn() const { return last_checkpoint_lsn_; }

private:
  Wal &wal_;
  PageManager &page_manager_;
  uint64_t last_checkpoint_lsn_{0};
};

} // namespace storage
} // namespace edgesql
