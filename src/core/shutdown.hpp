#pragma once

/**
 * @file shutdown.hpp
 * @brief Graceful shutdown coordination
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

namespace edgesql {
namespace core {

/**
 * @brief Shutdown coordinator
 *
 * Manages graceful shutdown with configurable timeout and phase callbacks.
 */
class ShutdownCoordinator {
public:
  /**
   * @brief Shutdown phases
   */
  enum class Phase {
    STOP_ACCEPTING,    // Stop accepting new connections
    DRAIN_CONNECTIONS, // Wait for active connections to complete
    FLUSH_WAL,         // Flush write-ahead log
    CLOSE_FILES,       // Close all file handles
    CLEANUP,           // Final cleanup
    DONE               // Shutdown complete
  };

  /**
   * @brief Callback for shutdown phase
   */
  using PhaseCallback = std::function<void()>;

  /**
   * @brief Get the singleton instance
   */
  static ShutdownCoordinator &instance();

  /**
   * @brief Register a callback for a shutdown phase
   * @param phase Phase to register for
   * @param callback Callback to execute
   */
  void register_callback(Phase phase, PhaseCallback callback);

  /**
   * @brief Initiate graceful shutdown
   * @param timeout Maximum time to wait for graceful shutdown
   * @return true if shutdown completed gracefully, false if timed out
   */
  bool initiate(std::chrono::seconds timeout = std::chrono::seconds(30));

  /**
   * @brief Check if shutdown is in progress
   */
  bool in_progress() const {
    return shutdown_started_.load(std::memory_order_acquire);
  }

  /**
   * @brief Get current shutdown phase
   */
  Phase current_phase() const { return current_phase_; }

  /**
   * @brief Wait for a specific phase to complete
   * @param phase Phase to wait for
   * @param timeout Maximum time to wait
   * @return true if phase completed, false if timed out
   */
  bool wait_for_phase(Phase phase, std::chrono::seconds timeout);

private:
  ShutdownCoordinator() = default;

  void execute_phase(Phase phase);
  static const char *phase_name(Phase phase);

  std::atomic<bool> shutdown_started_{false};
  std::atomic<bool> shutdown_complete_{false};
  Phase current_phase_{Phase::STOP_ACCEPTING};

  mutable std::mutex mutex_;
  std::condition_variable phase_cv_;

  std::vector<PhaseCallback> phase_callbacks_[6]; // One per phase
};

/**
 * @brief RAII guard for tracking active operations
 *
 * Used to prevent shutdown until all active operations complete.
 */
class ActiveOperationGuard {
public:
  ActiveOperationGuard();
  ~ActiveOperationGuard();

  // Non-copyable but movable
  ActiveOperationGuard(const ActiveOperationGuard &) = delete;
  ActiveOperationGuard &operator=(const ActiveOperationGuard &) = delete;
  ActiveOperationGuard(ActiveOperationGuard &&other) noexcept;
  ActiveOperationGuard &operator=(ActiveOperationGuard &&other) noexcept;

  /**
   * @brief Check if the guard is valid (operation is tracked)
   */
  bool valid() const { return valid_; }

  /**
   * @brief Get the count of active operations
   */
  static size_t active_count();

  /**
   * @brief Wait until all active operations complete
   * @param timeout Maximum time to wait
   * @return true if all completed, false if timed out
   */
  static bool wait_all_complete(std::chrono::seconds timeout);

private:
  bool valid_{true};

  static std::atomic<size_t> active_count_;
  static std::mutex wait_mutex_;
  static std::condition_variable wait_cv_;
};

} // namespace core
} // namespace edgesql
