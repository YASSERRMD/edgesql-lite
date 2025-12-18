/**
 * @file shutdown.cpp
 * @brief Shutdown coordinator implementation
 */

#include "shutdown.hpp"
#include "signal_handler.hpp"
#include <iostream>

namespace edgesql {
namespace core {

// Static members for ActiveOperationGuard
std::atomic<size_t> ActiveOperationGuard::active_count_{0};
std::mutex ActiveOperationGuard::wait_mutex_;
std::condition_variable ActiveOperationGuard::wait_cv_;

ShutdownCoordinator &ShutdownCoordinator::instance() {
  static ShutdownCoordinator instance;
  return instance;
}

void ShutdownCoordinator::register_callback(Phase phase,
                                            PhaseCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  phase_callbacks_[static_cast<size_t>(phase)].push_back(std::move(callback));
}

bool ShutdownCoordinator::initiate(std::chrono::seconds timeout) {
  // Mark shutdown as started
  if (shutdown_started_.exchange(true, std::memory_order_acq_rel)) {
    // Already in progress
    return wait_for_phase(Phase::DONE, timeout);
  }

  auto deadline = std::chrono::steady_clock::now() + timeout;

  std::cout << "Initiating graceful shutdown...\n";

  // Execute each phase in order
  for (int i = 0; i <= static_cast<int>(Phase::DONE); ++i) {
    Phase phase = static_cast<Phase>(i);

    // Check timeout
    if (std::chrono::steady_clock::now() > deadline) {
      std::cerr << "Shutdown timeout during phase: " << phase_name(phase)
                << "\n";
      return false;
    }

    execute_phase(phase);

    // Special handling for DRAIN_CONNECTIONS
    if (phase == Phase::DRAIN_CONNECTIONS) {
      auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
          deadline - std::chrono::steady_clock::now());
      if (remaining.count() > 0) {
        if (!ActiveOperationGuard::wait_all_complete(remaining)) {
          std::cerr << "Timeout waiting for active operations to complete\n";
        }
      }
    }
  }

  shutdown_complete_.store(true, std::memory_order_release);
  phase_cv_.notify_all();

  std::cout << "Shutdown complete\n";
  return true;
}

void ShutdownCoordinator::execute_phase(Phase phase) {
  std::cout << "Shutdown phase: " << phase_name(phase) << "\n";

  std::lock_guard<std::mutex> lock(mutex_);
  current_phase_ = phase;

  for (const auto &callback : phase_callbacks_[static_cast<size_t>(phase)]) {
    try {
      callback();
    } catch (const std::exception &e) {
      std::cerr << "Error in shutdown callback (" << phase_name(phase)
                << "): " << e.what() << "\n";
    } catch (...) {
      std::cerr << "Unknown error in shutdown callback (" << phase_name(phase)
                << ")\n";
    }
  }

  phase_cv_.notify_all();
}

bool ShutdownCoordinator::wait_for_phase(Phase phase,
                                         std::chrono::seconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  return phase_cv_.wait_for(lock, timeout, [this, phase]() {
    return current_phase_ >= phase ||
           shutdown_complete_.load(std::memory_order_acquire);
  });
}

const char *ShutdownCoordinator::phase_name(Phase phase) {
  switch (phase) {
  case Phase::STOP_ACCEPTING:
    return "STOP_ACCEPTING";
  case Phase::DRAIN_CONNECTIONS:
    return "DRAIN_CONNECTIONS";
  case Phase::FLUSH_WAL:
    return "FLUSH_WAL";
  case Phase::CLOSE_FILES:
    return "CLOSE_FILES";
  case Phase::CLEANUP:
    return "CLEANUP";
  case Phase::DONE:
    return "DONE";
  default:
    return "UNKNOWN";
  }
}

// ActiveOperationGuard implementation

ActiveOperationGuard::ActiveOperationGuard() {
  // Don't allow new operations during shutdown
  if (SignalHandler::shutdown_requested()) {
    valid_ = false;
    return;
  }

  active_count_.fetch_add(1, std::memory_order_acq_rel);
}

ActiveOperationGuard::~ActiveOperationGuard() {
  if (valid_) {
    if (active_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      // Was the last operation
      std::lock_guard<std::mutex> lock(wait_mutex_);
      wait_cv_.notify_all();
    }
  }
}

ActiveOperationGuard::ActiveOperationGuard(
    ActiveOperationGuard &&other) noexcept
    : valid_(other.valid_) {
  other.valid_ = false;
}

ActiveOperationGuard &
ActiveOperationGuard::operator=(ActiveOperationGuard &&other) noexcept {
  if (this != &other) {
    if (valid_) {
      active_count_.fetch_sub(1, std::memory_order_acq_rel);
    }
    valid_ = other.valid_;
    other.valid_ = false;
  }
  return *this;
}

size_t ActiveOperationGuard::active_count() {
  return active_count_.load(std::memory_order_acquire);
}

bool ActiveOperationGuard::wait_all_complete(std::chrono::seconds timeout) {
  std::unique_lock<std::mutex> lock(wait_mutex_);
  return wait_cv_.wait_for(lock, timeout, []() {
    return active_count_.load(std::memory_order_acquire) == 0;
  });
}

} // namespace core
} // namespace edgesql
