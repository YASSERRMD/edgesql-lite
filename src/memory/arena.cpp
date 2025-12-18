/**
 * @file arena.cpp
 * @brief Arena allocator implementation
 */

#include "arena.hpp"
#include <cstring>
#include <stdexcept>

namespace edgesql {
namespace memory {

Arena::Arena(size_t block_size) : block_size_(block_size) {
  // Pre-allocate first block
  add_block();
}

Arena::~Arena() {
  // Blocks are automatically cleaned up by unique_ptr
}

void *Arena::allocate(size_t size, size_t alignment) {
  if (size == 0) {
    return nullptr;
  }

  // Try to allocate from current block
  while (current_block_ < blocks_.size()) {
    Block &block = blocks_[current_block_];

    // Calculate aligned offset
    size_t aligned_offset = align_up(block.used, alignment);

    if (aligned_offset + size <= block.size) {
      // Fits in current block
      void *ptr = block.data.get() + aligned_offset;
      block.used = aligned_offset + size;
      bytes_allocated_ += size;
      return ptr;
    }

    // Move to next block
    current_block_++;
  }

  // Need a new block
  // Make sure block is big enough for this allocation
  size_t min_block_size = align_up(size, alignment) + alignment;
  if (min_block_size > block_size_) {
    // Create an oversized block just for this allocation
    Block block;
    block.size = min_block_size;
    block.data = std::make_unique<uint8_t[]>(block.size);
    block.used = 0;

    size_t aligned_offset = align_up(block.used, alignment);
    void *ptr = block.data.get() + aligned_offset;
    block.used = aligned_offset + size;

    blocks_.push_back(std::move(block));
    current_block_ = blocks_.size() - 1;
    capacity_ += min_block_size;
    bytes_allocated_ += size;

    return ptr;
  }

  add_block();
  return allocate(size, alignment); // Retry with new block
}

void *Arena::allocate_zeroed(size_t size, size_t alignment) {
  void *ptr = allocate(size, alignment);
  if (ptr) {
    std::memset(ptr, 0, size);
  }
  return ptr;
}

void Arena::reset() {
  // Keep the blocks allocated, just reset usage
  for (auto &block : blocks_) {
    block.used = 0;
  }
  current_block_ = 0;
  bytes_allocated_ = 0;
}

void Arena::add_block() {
  Block block;
  block.size = block_size_;
  block.data = std::make_unique<uint8_t[]>(block.size);
  block.used = 0;

  blocks_.push_back(std::move(block));
  capacity_ += block_size_;
}

size_t Arena::align_up(size_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace memory
} // namespace edgesql
