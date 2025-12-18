/**
 * @file memory_tracker.cpp
 * @brief Memory tracker implementation
 */

#include "memory_tracker.hpp"
#include <algorithm>
#include <new>

namespace edgesql {
namespace memory {

MemoryTracker::MemoryTracker() = default;

MemoryTracker &MemoryTracker::instance() {
  static MemoryTracker instance;
  return instance;
}

void MemoryTracker::set_limit(size_t limit) {
  limit_.store(limit, std::memory_order_release);
}

bool MemoryTracker::try_reserve(size_t size) {
  size_t current = used_.load(std::memory_order_acquire);
  size_t limit = limit_.load(std::memory_order_acquire);

  while (true) {
    if (current + size > limit) {
      failed_count_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    if (used_.compare_exchange_weak(current, current + size,
                                    std::memory_order_acq_rel,
                                    std::memory_order_acquire)) {
      allocation_count_.fetch_add(1, std::memory_order_relaxed);

      // Update peak
      size_t new_used = current + size;
      size_t old_peak = peak_.load(std::memory_order_acquire);
      while (new_used > old_peak) {
        if (peak_.compare_exchange_weak(old_peak, new_used,
                                        std::memory_order_acq_rel,
                                        std::memory_order_acquire)) {
          break;
        }
      }

      return true;
    }
    // current is updated by compare_exchange_weak on failure
  }
}

void MemoryTracker::release(size_t size) {
  size_t current = used_.load(std::memory_order_acquire);
  while (!used_.compare_exchange_weak(
      current, current > size ? current - size : 0, std::memory_order_acq_rel,
      std::memory_order_acquire)) {
    // current is updated on failure
  }
}

void MemoryTracker::reset_stats() {
  used_.store(0, std::memory_order_release);
  peak_.store(0, std::memory_order_release);
  allocation_count_.store(0, std::memory_order_release);
  failed_count_.store(0, std::memory_order_release);
}

// MemoryReservation implementation

MemoryReservation::MemoryReservation(size_t size) : size_(size) {
  if (!MemoryTracker::instance().try_reserve(size)) {
    throw std::bad_alloc();
  }
  valid_ = true;
}

MemoryReservation::MemoryReservation(size_t size, std::nothrow_t) noexcept
    : size_(size) {
  valid_ = MemoryTracker::instance().try_reserve(size);
}

MemoryReservation::~MemoryReservation() { release(); }

MemoryReservation::MemoryReservation(MemoryReservation &&other) noexcept
    : size_(other.size_), valid_(other.valid_) {
  other.valid_ = false;
  other.size_ = 0;
}

MemoryReservation &
MemoryReservation::operator=(MemoryReservation &&other) noexcept {
  if (this != &other) {
    release();
    size_ = other.size_;
    valid_ = other.valid_;
    other.valid_ = false;
    other.size_ = 0;
  }
  return *this;
}

void MemoryReservation::release() {
  if (valid_) {
    MemoryTracker::instance().release(size_);
    valid_ = false;
    size_ = 0;
  }
}

} // namespace memory
} // namespace edgesql
