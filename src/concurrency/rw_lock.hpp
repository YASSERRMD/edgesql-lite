#pragma once

/**
 * @file rw_lock.hpp
 * @brief Read-write lock for concurrency control
 */

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace edgesql {
namespace concurrency {

/**
 * @brief Read-write lock
 *
 * Allows multiple readers or single writer.
 * Writers have priority to prevent starvation.
 */
class RWLock {
public:
  /**
   * @brief Acquire read lock
   */
  void lock_read();

  /**
   * @brief Release read lock
   */
  void unlock_read();

  /**
   * @brief Acquire write lock
   */
  void lock_write();

  /**
   * @brief Release write lock
   */
  void unlock_write();

  /**
   * @brief Try to acquire read lock
   * @return true if acquired
   */
  bool try_lock_read();

  /**
   * @brief Try to acquire write lock
   * @return true if acquired
   */
  bool try_lock_write();

private:
  std::mutex mutex_;
  std::condition_variable read_cv_;
  std::condition_variable write_cv_;

  int readers_{0};
  bool writer_{false};
  int waiting_writers_{0};
};

/**
 * @brief RAII read lock guard
 */
class ReadLockGuard {
public:
  explicit ReadLockGuard(RWLock &lock) : lock_(lock) { lock_.lock_read(); }

  ~ReadLockGuard() { lock_.unlock_read(); }

  // Non-copyable
  ReadLockGuard(const ReadLockGuard &) = delete;
  ReadLockGuard &operator=(const ReadLockGuard &) = delete;

private:
  RWLock &lock_;
};

/**
 * @brief RAII write lock guard
 */
class WriteLockGuard {
public:
  explicit WriteLockGuard(RWLock &lock) : lock_(lock) { lock_.lock_write(); }

  ~WriteLockGuard() { lock_.unlock_write(); }

  // Non-copyable
  WriteLockGuard(const WriteLockGuard &) = delete;
  WriteLockGuard &operator=(const WriteLockGuard &) = delete;

private:
  RWLock &lock_;
};

} // namespace concurrency
} // namespace edgesql
