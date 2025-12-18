/**
 * @file rw_lock.cpp
 * @brief Read-write lock implementation
 */

#include "rw_lock.hpp"

namespace edgesql {
namespace concurrency {

void RWLock::lock_read() {
  std::unique_lock<std::mutex> lock(mutex_);

  // Wait while there's a writer or waiting writers
  read_cv_.wait(lock, [this]() { return !writer_ && waiting_writers_ == 0; });

  readers_++;
}

void RWLock::unlock_read() {
  std::unique_lock<std::mutex> lock(mutex_);

  readers_--;

  if (readers_ == 0) {
    // Wake up waiting writer if any
    write_cv_.notify_one();
  }
}

void RWLock::lock_write() {
  std::unique_lock<std::mutex> lock(mutex_);

  waiting_writers_++;

  // Wait while there are readers or a writer
  write_cv_.wait(lock, [this]() { return readers_ == 0 && !writer_; });

  waiting_writers_--;
  writer_ = true;
}

void RWLock::unlock_write() {
  std::unique_lock<std::mutex> lock(mutex_);

  writer_ = false;

  // Prefer waiting writers over readers
  if (waiting_writers_ > 0) {
    write_cv_.notify_one();
  } else {
    read_cv_.notify_all();
  }
}

bool RWLock::try_lock_read() {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }

  if (writer_ || waiting_writers_ > 0) {
    return false;
  }

  readers_++;
  return true;
}

bool RWLock::try_lock_write() {
  std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }

  if (readers_ > 0 || writer_) {
    return false;
  }

  writer_ = true;
  return true;
}

} // namespace concurrency
} // namespace edgesql
