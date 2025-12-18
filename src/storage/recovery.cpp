/**
 * @file recovery.cpp
 * @brief Recovery implementation
 */

#include "recovery.hpp"
#include <cstring>
#include <iostream>

namespace edgesql {
namespace storage {

// RecoveryManager implementation

RecoveryManager::RecoveryManager(Wal &wal, PageManager &page_manager)
    : wal_(wal), page_manager_(page_manager) {}

bool RecoveryManager::recover() {
  return recover(
      [this](const WalRecord &record) { return apply_record(record); });
}

bool RecoveryManager::recover(RecordCallback callback) {
  std::cout << "Starting recovery...\n";

  stats_ = RecoveryStats{};

  // Find last checkpoint
  uint64_t checkpoint_lsn = find_last_checkpoint();
  stats_.start_lsn = checkpoint_lsn > 0 ? checkpoint_lsn : 1;

  std::cout << "Recovering from LSN: " << stats_.start_lsn << "\n";

  // Read WAL records from checkpoint
  std::vector<WalRecord> records;
  if (!wal_.read_from(stats_.start_lsn, records)) {
    std::cerr << "Failed to read WAL records\n";
    return false;
  }

  std::cout << "Found " << records.size() << " WAL records to replay\n";

  // Apply each record
  for (const auto &record : records) {
    stats_.records_processed++;

    if (record.header.type == WalRecordType::CHECKPOINT) {
      // Skip checkpoints
      stats_.records_skipped++;
      continue;
    }

    if (!callback(record)) {
      std::cerr << "Recovery aborted at LSN " << record.header.lsn << "\n";
      return false;
    }

    stats_.end_lsn = record.header.lsn;
  }

  std::cout << "Recovery complete. Processed: " << stats_.records_processed
            << ", Applied: " << stats_.records_applied
            << ", Skipped: " << stats_.records_skipped
            << ", Errors: " << stats_.errors << "\n";

  return stats_.errors == 0;
}

bool RecoveryManager::needs_recovery() const {
  // Check if there are any WAL records after the last checkpoint
  std::vector<WalRecord> records;
  uint64_t checkpoint_lsn =
      const_cast<RecoveryManager *>(this)->find_last_checkpoint();

  if (!const_cast<Wal &>(wal_).read_from(checkpoint_lsn, records)) {
    return false;
  }

  // If there are records other than the checkpoint itself, recovery is needed
  return records.size() > 1;
}

uint64_t RecoveryManager::find_last_checkpoint() {
  std::vector<WalRecord> records;
  if (!wal_.read_all(records)) {
    return 0;
  }

  uint64_t last_checkpoint = 0;
  for (const auto &record : records) {
    if (record.header.type == WalRecordType::CHECKPOINT) {
      last_checkpoint = record.header.lsn;
    }
  }

  return last_checkpoint;
}

bool RecoveryManager::apply_record(const WalRecord &record) {
  bool success = false;

  switch (record.header.type) {
  case WalRecordType::INSERT:
    success = apply_insert(record);
    break;
  case WalRecordType::UPDATE:
    success = apply_update(record);
    break;
  case WalRecordType::DELETE:
    success = apply_delete(record);
    break;
  case WalRecordType::CREATE_TABLE:
    // Table creation is handled by metadata
    success = true;
    break;
  case WalRecordType::DROP_TABLE:
    // Table deletion is handled by metadata
    success = true;
    break;
  case WalRecordType::COMMIT:
  case WalRecordType::ROLLBACK:
    // Transaction markers don't need page changes
    success = true;
    break;
  default:
    std::cerr << "Unknown WAL record type: "
              << static_cast<int>(record.header.type) << "\n";
    stats_.errors++;
    return true; // Continue despite unknown type
  }

  if (success) {
    stats_.records_applied++;
  } else {
    stats_.errors++;
  }

  return true; // Continue recovery even on errors
}

bool RecoveryManager::apply_insert(const WalRecord &record) {
  // Get the page
  Page *page =
      page_manager_.get_page(record.header.table_id, record.header.page_id);

  if (!page) {
    // Page might not exist yet, need to allocate
    uint32_t new_page_id = page_manager_.allocate_page(record.header.table_id);
    if (new_page_id == UINT32_MAX) {
      std::cerr << "Failed to allocate page for recovery\n";
      return false;
    }
    page = page_manager_.get_page(record.header.table_id, new_page_id);
  }

  if (!page) {
    return false;
  }

  // Check if record already exists at this slot (idempotency)
  const uint8_t *existing_data = nullptr;
  uint16_t existing_length = 0;

  if (record.header.slot_id < page->slot_count() &&
      page->get_record(record.header.slot_id, &existing_data,
                       &existing_length)) {
    // Record already exists, skip
    stats_.records_skipped++;
    return true;
  }

  // Insert the record
  uint16_t slot_id;
  if (!page->insert_record(record.payload.data(),
                           static_cast<uint16_t>(record.payload.size()),
                           &slot_id)) {
    std::cerr << "Failed to insert record during recovery\n";
    return false;
  }

  // Update LSN
  page->header().lsn = record.header.lsn;
  page_manager_.mark_dirty(record.header.table_id, record.header.page_id);

  return true;
}

bool RecoveryManager::apply_update(const WalRecord &record) {
  Page *page =
      page_manager_.get_page(record.header.table_id, record.header.page_id);
  if (!page) {
    std::cerr << "Page not found for update recovery\n";
    return false;
  }

  // Check if page already has this update (LSN check)
  if (page->header().lsn >= record.header.lsn) {
    stats_.records_skipped++;
    return true;
  }

  if (!page->update_record(record.header.slot_id, record.payload.data(),
                           static_cast<uint16_t>(record.payload.size()))) {
    std::cerr << "Failed to update record during recovery\n";
    return false;
  }

  page->header().lsn = record.header.lsn;
  page_manager_.mark_dirty(record.header.table_id, record.header.page_id);

  return true;
}

bool RecoveryManager::apply_delete(const WalRecord &record) {
  Page *page =
      page_manager_.get_page(record.header.table_id, record.header.page_id);
  if (!page) {
    std::cerr << "Page not found for delete recovery\n";
    return false;
  }

  // Check if page already has this delete (LSN check)
  if (page->header().lsn >= record.header.lsn) {
    stats_.records_skipped++;
    return true;
  }

  if (!page->delete_record(record.header.slot_id)) {
    // Might already be deleted
    stats_.records_skipped++;
    return true;
  }

  page->header().lsn = record.header.lsn;
  page_manager_.mark_dirty(record.header.table_id, record.header.page_id);

  return true;
}

// CheckpointManager implementation

CheckpointManager::CheckpointManager(Wal &wal, PageManager &page_manager)
    : wal_(wal), page_manager_(page_manager) {}

uint64_t CheckpointManager::checkpoint() {
  std::cout << "Starting checkpoint...\n";

  // Flush all dirty pages
  size_t flushed = page_manager_.flush_all();
  std::cout << "Flushed " << flushed << " dirty pages\n";

  // Write checkpoint record to WAL
  uint64_t lsn = wal_.checkpoint();

  if (lsn > 0) {
    // Sync WAL
    wal_.sync();
    last_checkpoint_lsn_ = lsn;
    std::cout << "Checkpoint complete at LSN " << lsn << "\n";
  } else {
    std::cerr << "Failed to write checkpoint record\n";
  }

  return lsn;
}

bool CheckpointManager::should_checkpoint(size_t wal_size_threshold) const {
  return wal_.file_size() > wal_size_threshold;
}

} // namespace storage
} // namespace edgesql
