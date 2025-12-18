#pragma once

/**
 * @file transaction.hpp
 * @brief Transaction context for single-writer/multiple-reader model
 */

#include "rw_lock.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

namespace edgesql {
namespace concurrency {

/**
 * @brief Transaction isolation level
 */
enum class IsolationLevel {
  READ_UNCOMMITTED,
  READ_COMMITTED,
  REPEATABLE_READ,
  SERIALIZABLE
};

/**
 * @brief Transaction state
 */
enum class TransactionState { ACTIVE, COMMITTED, ABORTED };

/**
 * @brief Transaction context
 */
class Transaction {
public:
  /**
   * @brief Constructor
   * @param id Transaction ID
   * @param read_only Whether this is a read-only transaction
   */
  Transaction(uint64_t id, bool read_only);

  /**
   * @brief Get transaction ID
   */
  uint64_t id() const { return id_; }

  /**
   * @brief Check if read-only
   */
  bool is_read_only() const { return read_only_; }

  /**
   * @brief Get current state
   */
  TransactionState state() const { return state_; }

  /**
   * @brief Mark as committed
   */
  void commit();

  /**
   * @brief Mark as aborted
   */
  void abort();

  /**
   * @brief Get start time
   */
  std::chrono::steady_clock::time_point start_time() const {
    return start_time_;
  }

  /**
   * @brief Get elapsed time
   */
  std::chrono::milliseconds elapsed() const;

private:
  uint64_t id_;
  bool read_only_;
  TransactionState state_{TransactionState::ACTIVE};
  std::chrono::steady_clock::time_point start_time_;
};

/**
 * @brief Transaction manager
 *
 * Manages transactions with single-writer/multiple-reader model.
 */
class TransactionManager {
public:
  /**
   * @brief Get singleton instance
   */
  static TransactionManager &instance();

  /**
   * @brief Begin a read-only transaction
   */
  std::unique_ptr<Transaction> begin_read();

  /**
   * @brief Begin a read-write transaction
   *
   * Blocks until write lock is acquired.
   */
  std::unique_ptr<Transaction> begin_write();

  /**
   * @brief Try to begin a read-write transaction
   * @return Transaction, or nullptr if couldn't acquire lock
   */
  std::unique_ptr<Transaction> try_begin_write();

  /**
   * @brief Commit a transaction
   */
  void commit(Transaction &txn);

  /**
   * @brief Abort a transaction
   */
  void abort(Transaction &txn);

  /**
   * @brief Get current transaction count
   */
  uint64_t active_transactions() const {
    return active_count_.load(std::memory_order_acquire);
  }

  /**
   * @brief Get next transaction ID (for testing)
   */
  uint64_t next_id() const { return next_id_.load(std::memory_order_acquire); }

private:
  TransactionManager() = default;

  void end_transaction(Transaction &txn);

  RWLock lock_;
  std::atomic<uint64_t> next_id_{1};
  std::atomic<uint64_t> active_count_{0};
};

/**
 * @brief RAII transaction guard
 */
class TransactionGuard {
public:
  /**
   * @brief Constructor with existing transaction
   */
  explicit TransactionGuard(std::unique_ptr<Transaction> txn);

  /**
   * @brief Destructor - aborts if not committed
   */
  ~TransactionGuard();

  // Non-copyable
  TransactionGuard(const TransactionGuard &) = delete;
  TransactionGuard &operator=(const TransactionGuard &) = delete;

  // Movable
  TransactionGuard(TransactionGuard &&other) noexcept;
  TransactionGuard &operator=(TransactionGuard &&other) noexcept;

  /**
   * @brief Commit the transaction
   */
  void commit();

  /**
   * @brief Abort the transaction
   */
  void abort();

  /**
   * @brief Get the transaction
   */
  Transaction *get() { return txn_.get(); }
  Transaction *operator->() { return txn_.get(); }

  /**
   * @brief Check if valid
   */
  explicit operator bool() const { return txn_ != nullptr; }

private:
  std::unique_ptr<Transaction> txn_;
};

} // namespace concurrency
} // namespace edgesql
