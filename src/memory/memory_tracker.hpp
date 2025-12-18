#pragma once

/**
 * @file memory_tracker.hpp
 * @brief Global memory tracking and limits
 */

#include <atomic>
#include <cstddef>
#include <mutex>

namespace edgesql {
namespace memory {

/**
 * @brief Global memory tracker
 *
 * Tracks total memory usage across all queries and enforces global limits.
 */
class MemoryTracker {
public:
  /**
   * @brief Get the singleton instance
   */
  static MemoryTracker &instance();

  /**
   * @brief Set the global memory limit
   * @param limit Maximum bytes to use
   */
  void set_limit(size_t limit);

  /**
   * @brief Get the global memory limit
   */
  size_t limit() const { return limit_.load(std::memory_order_acquire); }

  /**
   * @brief Get current memory usage
   */
  size_t used() const { return used_.load(std::memory_order_acquire); }

  /**
   * @brief Get peak memory usage
   */
  size_t peak() const { return peak_.load(std::memory_order_acquire); }

  /**
   * @brief Check if allocation would exceed limit
   */
  bool would_exceed(size_t size) const {
    return used_.load(std::memory_order_acquire) + size >
           limit_.load(std::memory_order_acquire);
  }

  /**
   * @brief Try to reserve memory
   * @param size Bytes to reserve
   * @return true if reservation succeeded
   */
  bool try_reserve(size_t size);

  /**
   * @brief Release reserved memory
   * @param size Bytes to release
   */
  void release(size_t size);

  /**
   * @brief Reset statistics
   */
  void reset_stats();

  /**
   * @brief Get number of allocations
   */
  uint64_t allocation_count() const {
    return allocation_count_.load(std::memory_order_acquire);
  }

  /**
   * @brief Get number of failed allocations
   */
  uint64_t failed_allocation_count() const {
    return failed_count_.load(std::memory_order_acquire);
  }

private:
  MemoryTracker();

  std::atomic<size_t> limit_{512 * 1024 * 1024}; // 512MB default
  std::atomic<size_t> used_{0};
  std::atomic<size_t> peak_{0};
  std::atomic<uint64_t> allocation_count_{0};
  std::atomic<uint64_t> failed_count_{0};
};

/**
 * @brief RAII memory reservation
 *
 * Automatically releases memory when destroyed.
 */
class MemoryReservation {
public:
  /**
   * @brief Constructor - reserves memory
   * @param size Bytes to reserve
   * @throws std::bad_alloc if reservation fails
   */
  explicit MemoryReservation(size_t size);

  /**
   * @brief Constructor - try to reserve without throwing
   * @param size Bytes to reserve
   * @param nothrow Tag for non-throwing version
   */
  MemoryReservation(size_t size, std::nothrow_t) noexcept;

  /**
   * @brief Destructor - releases memory
   */
  ~MemoryReservation();

  // Non-copyable but movable
  MemoryReservation(const MemoryReservation &) = delete;
  MemoryReservation &operator=(const MemoryReservation &) = delete;
  MemoryReservation(MemoryReservation &&other) noexcept;
  MemoryReservation &operator=(MemoryReservation &&other) noexcept;

  /**
   * @brief Check if reservation is valid
   */
  bool valid() const { return valid_; }

  /**
   * @brief Get reserved size
   */
  size_t size() const { return size_; }

  /**
   * @brief Release the reservation manually
   */
  void release();

private:
  size_t size_{0};
  bool valid_{false};
};

} // namespace memory
} // namespace edgesql
