#pragma once

/**
 * @file query_allocator.hpp
 * @brief Per-query memory allocator with budget enforcement
 */

#include "arena.hpp"
#include <cstdint>
#include <stdexcept>

namespace edgesql {
namespace memory {

/**
 * @brief Memory budget exceeded exception
 */
class MemoryBudgetExceeded : public std::runtime_error {
public:
  MemoryBudgetExceeded(size_t requested, size_t used, size_t limit)
      : std::runtime_error("Memory budget exceeded"), requested_(requested),
        used_(used), limit_(limit) {}

  size_t requested() const { return requested_; }
  size_t used() const { return used_; }
  size_t limit() const { return limit_; }

private:
  size_t requested_;
  size_t used_;
  size_t limit_;
};

/**
 * @brief Per-query memory allocator
 *
 * Wraps an arena allocator with budget enforcement.
 */
class QueryAllocator {
public:
  /**
   * @brief Constructor
   * @param memory_limit Maximum bytes to allocate
   * @param arena Underlying arena allocator
   */
  QueryAllocator(size_t memory_limit, Arena &arena);

  // Non-copyable
  QueryAllocator(const QueryAllocator &) = delete;
  QueryAllocator &operator=(const QueryAllocator &) = delete;

  /**
   * @brief Allocate memory
   * @param size Number of bytes
   * @param alignment Alignment requirement
   * @return Pointer to allocated memory
   * @throws MemoryBudgetExceeded if budget would be exceeded
   */
  void *allocate(size_t size, size_t alignment = 8);

  /**
   * @brief Allocate zeroed memory
   */
  void *allocate_zeroed(size_t size, size_t alignment = 8);

  /**
   * @brief Allocate memory for a type
   */
  template <typename T> T *allocate() {
    return static_cast<T *>(allocate(sizeof(T), alignof(T)));
  }

  /**
   * @brief Allocate array
   */
  template <typename T> T *allocate_array(size_t count) {
    return static_cast<T *>(allocate(sizeof(T) * count, alignof(T)));
  }

  /**
   * @brief Check if allocation would exceed budget
   */
  bool would_exceed(size_t size) const {
    return bytes_used_ + size > memory_limit_;
  }

  /**
   * @brief Get bytes used
   */
  size_t bytes_used() const { return bytes_used_; }

  /**
   * @brief Get memory limit
   */
  size_t memory_limit() const { return memory_limit_; }

  /**
   * @brief Get remaining budget
   */
  size_t remaining() const {
    return memory_limit_ > bytes_used_ ? memory_limit_ - bytes_used_ : 0;
  }

  /**
   * @brief Reset allocation tracking (doesn't actually free memory)
   */
  void reset() { bytes_used_ = 0; }

private:
  size_t memory_limit_;
  size_t bytes_used_{0};
  Arena &arena_;
};

} // namespace memory
} // namespace edgesql
