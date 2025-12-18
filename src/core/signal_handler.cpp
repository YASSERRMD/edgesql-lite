/**
 * @file signal_handler.cpp
 * @brief Signal handling implementation
 */

#include "signal_handler.hpp"
#include <iostream>
#include <mutex>
#include <vector>

namespace edgesql::core {

// Global shutdown flag
std::atomic<bool> g_shutdown_requested{false};

// Static storage for shutdown callbacks
namespace {
std::mutex g_callbacks_mutex;
std::vector<std::function<void()>> g_shutdown_callbacks;
} // namespace

void SignalHandler::install() {
  struct sigaction action{};
  action.sa_handler = signal_handler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;

  // Install handler for SIGTERM
  if (sigaction(SIGTERM, &action, nullptr) != 0) {
    std::cerr << "Failed to install SIGTERM handler\n";
  }

  // Install handler for SIGINT
  if (sigaction(SIGINT, &action, nullptr) != 0) {
    std::cerr << "Failed to install SIGINT handler\n";
  }

  // Ignore SIGPIPE (commonly occurs with broken connections)
  signal(SIGPIPE, SIG_IGN);
}

bool SignalHandler::shutdown_requested() {
  return g_shutdown_requested.load(std::memory_order_acquire);
}

void SignalHandler::request_shutdown() {
  g_shutdown_requested.store(true, std::memory_order_release);
}

void SignalHandler::on_shutdown(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(g_callbacks_mutex);
  g_shutdown_callbacks.push_back(std::move(callback));
}

void SignalHandler::execute_shutdown_callbacks() {
  std::lock_guard<std::mutex> lock(g_callbacks_mutex);

  // Execute callbacks in reverse order (LIFO)
  for (auto it = g_shutdown_callbacks.rbegin();
       it != g_shutdown_callbacks.rend(); ++it) {
    try {
      (*it)();
    } catch (const std::exception &e) {
      std::cerr << "Shutdown callback failed: " << e.what() << "\n";
    } catch (...) {
      std::cerr << "Shutdown callback failed with unknown exception\n";
    }
  }

  g_shutdown_callbacks.clear();
}

void SignalHandler::signal_handler(int signal) {
  // Signal-safe: only set atomic flag
  // Use write() instead of cout for signal safety
  const char *msg = nullptr;

  switch (signal) {
  case SIGTERM:
    msg = "\nReceived SIGTERM, initiating shutdown...\n";
    break;
  case SIGINT:
    msg = "\nReceived SIGINT, initiating shutdown...\n";
    break;
  default:
    msg = "\nReceived signal, initiating shutdown...\n";
    break;
  }

  // Signal-safe write
  [[maybe_unused]] auto result = write(STDERR_FILENO, msg, strlen(msg));

  g_shutdown_requested.store(true, std::memory_order_release);
}

} // namespace edgesql::core
