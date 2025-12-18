/**
 * @file query_allocator.cpp
 * @brief Query allocator implementation
 */

#include "query_allocator.hpp"
#include <cstring>

namespace edgesql {
namespace memory {

QueryAllocator::QueryAllocator(size_t memory_limit, Arena &arena)
    : memory_limit_(memory_limit), arena_(arena) {}

void *QueryAllocator::allocate(size_t size, size_t alignment) {
  if (would_exceed(size)) {
    throw MemoryBudgetExceeded(size, bytes_used_, memory_limit_);
  }

  void *ptr = arena_.allocate(size, alignment);
  if (ptr) {
    bytes_used_ += size;
  }
  return ptr;
}

void *QueryAllocator::allocate_zeroed(size_t size, size_t alignment) {
  void *ptr = allocate(size, alignment);
  if (ptr) {
    std::memset(ptr, 0, size);
  }
  return ptr;
}

} // namespace memory
} // namespace edgesql
