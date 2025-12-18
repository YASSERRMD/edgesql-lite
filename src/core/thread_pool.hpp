#pragma once

/**
 * @file thread_pool.hpp
 * @brief Fixed-size thread pool for query execution
 */

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace edgesql {
namespace core {

/**
 * @brief Task for the thread pool
 */
using Task = std::function<void()>;

/**
 * @brief Fixed-size thread pool
 *
 * This thread pool creates a fixed number of worker threads at construction
 * time. No new threads are created for individual queries.
 */
class ThreadPool {
public:
  /**
   * @brief Construct a thread pool with specified number of workers
   * @param num_threads Number of worker threads (defaults to hardware
   * concurrency)
   */
  explicit ThreadPool(size_t num_threads = 0);

  /**
   * @brief Destructor - waits for all tasks to complete
   */
  ~ThreadPool();

  // Non-copyable, non-movable
  ThreadPool(const ThreadPool &) = delete;
  ThreadPool &operator=(const ThreadPool &) = delete;
  ThreadPool(ThreadPool &&) = delete;
  ThreadPool &operator=(ThreadPool &&) = delete;

  /**
   * @brief Submit a task to the pool
   * @param task Task to execute
   */
  void submit(Task task);

  /**
   * @brief Submit a task and get a future for the result
   * @tparam F Function type
   * @tparam Args Argument types
   * @param f Function to execute
   * @param args Arguments to pass
   * @return Future for the result
   */
  template <typename F, typename... Args>
  auto submit_with_result(F &&f, Args &&...args)
      -> std::future<std::invoke_result_t<F, Args...>>;

  /**
   * @brief Get the number of worker threads
   */
  size_t size() const { return workers_.size(); }

  /**
   * @brief Get the number of pending tasks
   */
  size_t pending() const;

  /**
   * @brief Check if the pool is stopping
   */
  bool stopping() const { return stop_.load(std::memory_order_acquire); }

  /**
   * @brief Request shutdown and wait for all tasks to complete
   */
  void shutdown();

private:
  void worker_loop();

  std::vector<std::thread> workers_;
  std::queue<Task> tasks_;

  mutable std::mutex mutex_;
  std::condition_variable condition_;
  std::atomic<bool> stop_{false};
};

// Template implementation
template <typename F, typename... Args>
auto ThreadPool::submit_with_result(F &&f, Args &&...args)
    -> std::future<std::invoke_result_t<F, Args...>> {

  using ReturnType = std::invoke_result_t<F, Args...>;

  auto task = std::make_shared<std::packaged_task<ReturnType()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<ReturnType> result = task->get_future();

  {
    std::lock_guard<std::mutex> lock(mutex_);

    if (stop_.load(std::memory_order_acquire)) {
      throw std::runtime_error("Cannot submit task to stopped thread pool");
    }

    tasks_.emplace([task]() { (*task)(); });
  }

  condition_.notify_one();
  return result;
}

} // namespace core
} // namespace edgesql
