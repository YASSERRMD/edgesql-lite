/**
 * @file transaction.cpp
 * @brief Transaction implementation
 */

#include "transaction.hpp"

namespace edgesql {
namespace concurrency {

// Transaction implementation

Transaction::Transaction(uint64_t id, bool read_only)
    : id_(id), read_only_(read_only),
      start_time_(std::chrono::steady_clock::now()) {}

void Transaction::commit() { state_ = TransactionState::COMMITTED; }

void Transaction::abort() { state_ = TransactionState::ABORTED; }

std::chrono::milliseconds Transaction::elapsed() const {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now -
                                                               start_time_);
}

// TransactionManager implementation

TransactionManager &TransactionManager::instance() {
  static TransactionManager instance;
  return instance;
}

std::unique_ptr<Transaction> TransactionManager::begin_read() {
  lock_.lock_read();

  uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
  active_count_.fetch_add(1, std::memory_order_relaxed);

  return std::make_unique<Transaction>(id, true);
}

std::unique_ptr<Transaction> TransactionManager::begin_write() {
  lock_.lock_write();

  uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
  active_count_.fetch_add(1, std::memory_order_relaxed);

  return std::make_unique<Transaction>(id, false);
}

std::unique_ptr<Transaction> TransactionManager::try_begin_write() {
  if (!lock_.try_lock_write()) {
    return nullptr;
  }

  uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);
  active_count_.fetch_add(1, std::memory_order_relaxed);

  return std::make_unique<Transaction>(id, false);
}

void TransactionManager::commit(Transaction &txn) {
  txn.commit();
  end_transaction(txn);
}

void TransactionManager::abort(Transaction &txn) {
  txn.abort();
  end_transaction(txn);
}

void TransactionManager::end_transaction(Transaction &txn) {
  active_count_.fetch_sub(1, std::memory_order_relaxed);

  if (txn.is_read_only()) {
    lock_.unlock_read();
  } else {
    lock_.unlock_write();
  }
}

// TransactionGuard implementation

TransactionGuard::TransactionGuard(std::unique_ptr<Transaction> txn)
    : txn_(std::move(txn)) {}

TransactionGuard::~TransactionGuard() {
  if (txn_ && txn_->state() == TransactionState::ACTIVE) {
    abort();
  }
}

TransactionGuard::TransactionGuard(TransactionGuard &&other) noexcept
    : txn_(std::move(other.txn_)) {}

TransactionGuard &
TransactionGuard::operator=(TransactionGuard &&other) noexcept {
  if (this != &other) {
    if (txn_ && txn_->state() == TransactionState::ACTIVE) {
      abort();
    }
    txn_ = std::move(other.txn_);
  }
  return *this;
}

void TransactionGuard::commit() {
  if (txn_) {
    TransactionManager::instance().commit(*txn_);
  }
}

void TransactionGuard::abort() {
  if (txn_) {
    TransactionManager::instance().abort(*txn_);
  }
}

} // namespace concurrency
} // namespace edgesql
