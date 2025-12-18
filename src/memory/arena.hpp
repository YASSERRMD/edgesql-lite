#pragma once

/**
 * @file arena.hpp
 * @brief Linear memory arena allocator
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace edgesql {
namespace memory {

/**
 * @brief Linear arena allocator
 *
 * Provides fast bump-pointer allocation with O(1) reset.
 * No individual deallocation - all memory is freed at once with reset().
 */
class Arena {
public:
  /**
   * @brief Constructor
   * @param block_size Size of each memory block
   */
  explicit Arena(size_t block_size = 64 * 1024); // 64KB default

  /**
   * @brief Destructor
   */
  ~Arena();

  // Non-copyable, non-movable
  Arena(const Arena &) = delete;
  Arena &operator=(const Arena &) = delete;
  Arena(Arena &&) = delete;
  Arena &operator=(Arena &&) = delete;

  /**
   * @brief Allocate memory
   * @param size Number of bytes to allocate
   * @param alignment Alignment requirement (default 8)
   * @return Pointer to allocated memory, or nullptr on failure
   */
  void *allocate(size_t size, size_t alignment = 8);

  /**
   * @brief Allocate and zero-initialize memory
   * @param size Number of bytes to allocate
   * @param alignment Alignment requirement
   * @return Pointer to allocated memory, or nullptr on failure
   */
  void *allocate_zeroed(size_t size, size_t alignment = 8);

  /**
   * @brief Allocate memory for a type
   * @tparam T Type to allocate
   * @return Pointer to allocated memory
   */
  template <typename T> T *allocate() {
    return static_cast<T *>(allocate(sizeof(T), alignof(T)));
  }

  /**
   * @brief Allocate array of type
   * @tparam T Type to allocate
   * @param count Number of elements
   * @return Pointer to allocated array
   */
  template <typename T> T *allocate_array(size_t count) {
    return static_cast<T *>(allocate(sizeof(T) * count, alignof(T)));
  }

  /**
   * @brief Reset the arena, freeing all allocations
   *
   * After reset, all pointers returned by allocate() are invalid.
   */
  void reset();

  /**
   * @brief Get total bytes allocated
   */
  size_t bytes_allocated() const { return bytes_allocated_; }

  /**
   * @brief Get total capacity
   */
  size_t capacity() const { return capacity_; }

  /**
   * @brief Get number of blocks
   */
  size_t block_count() const { return blocks_.size(); }

  /**
   * @brief Get the block size
   */
  size_t block_size() const { return block_size_; }

private:
  void add_block();
  static size_t align_up(size_t value, size_t alignment);

  struct Block {
    std::unique_ptr<uint8_t[]> data;
    size_t size;
    size_t used;
  };

  size_t block_size_;
  std::vector<Block> blocks_;
  size_t current_block_{0};
  size_t bytes_allocated_{0};
  size_t capacity_{0};
};

/**
 * @brief Scoped arena reset
 *
 * RAII wrapper that resets an arena when it goes out of scope.
 */
class ScopedArenaReset {
public:
  explicit ScopedArenaReset(Arena &arena) : arena_(arena) {}
  ~ScopedArenaReset() { arena_.reset(); }

  // Non-copyable
  ScopedArenaReset(const ScopedArenaReset &) = delete;
  ScopedArenaReset &operator=(const ScopedArenaReset &) = delete;

private:
  Arena &arena_;
};

} // namespace memory
} // namespace edgesql
