/**
 * @file thread_pool.cpp
 * @brief Thread pool implementation
 */

#include "thread_pool.hpp"
#include "signal_handler.hpp"
#include <iostream>

namespace edgesql::core {

ThreadPool::ThreadPool(size_t num_threads) {
  if (num_threads == 0) {
    num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) {
      num_threads = 4; // Fallback
    }
  }

  workers_.reserve(num_threads);

  for (size_t i = 0; i < num_threads; ++i) {
    workers_.emplace_back([this]() { worker_loop(); });
  }
}

ThreadPool::~ThreadPool() { shutdown(); }

void ThreadPool::submit(Task task) {
  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (stop_.load(std::memory_order_acquire)) {
      throw std::runtime_error("Cannot submit task to stopped thread pool");
    }

    tasks_.push(std::move(task));
  }

  condition_.notify_one();
}

size_t ThreadPool::pending() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return tasks_.size();
}

void ThreadPool::shutdown() {
  // Signal stop
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stop_.exchange(true, std::memory_order_acq_rel)) {
      return; // Already stopped
    }
  }

  // Wake up all workers
  condition_.notify_all();

  // Wait for workers to finish
  for (auto &worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void ThreadPool::worker_loop() {
  while (true) {
    Task task;

    {
      std::unique_lock<std::mutex> lock(mutex_);

      // Wait for a task or stop signal
      condition_.wait(lock, [this]() {
        return stop_.load(std::memory_order_acquire) || !tasks_.empty();
      });

      // Exit if stopped and no more tasks
      if (stop_.load(std::memory_order_acquire) && tasks_.empty()) {
        return;
      }

      // Get the next task
      if (!tasks_.empty()) {
        task = std::move(tasks_.front());
        tasks_.pop();
      }
    }

    // Execute task outside the lock
    if (task) {
      try {
        task();
      } catch (const std::exception &e) {
        std::cerr << "Task exception: " << e.what() << "\n";
      } catch (...) {
        std::cerr << "Task threw unknown exception\n";
      }
    }
  }
}

} // namespace edgesql::core
