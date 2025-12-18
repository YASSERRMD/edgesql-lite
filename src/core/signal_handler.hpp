#pragma once

/**
 * @file signal_handler.hpp
 * @brief Signal handling for graceful shutdown
 */

#include <atomic>
#include <csignal>
#include <functional>

namespace edgesql::core {

/**
 * @brief Global shutdown flag
 *
 * This atomic flag is set when a shutdown signal is received.
 * All components should check this flag periodically and exit gracefully.
 */
extern std::atomic<bool> g_shutdown_requested;

/**
 * @brief Signal handler for graceful shutdown
 */
class SignalHandler {
public:
  /**
   * @brief Install signal handlers
   *
   * Installs handlers for SIGTERM and SIGINT that set the shutdown flag.
   */
  static void install();

  /**
   * @brief Check if shutdown was requested
   */
  static bool shutdown_requested();

  /**
   * @brief Request shutdown programmatically
   */
  static void request_shutdown();

  /**
   * @brief Register a callback to be called on shutdown
   *
   * Callbacks are called in reverse order of registration.
   */
  static void on_shutdown(std::function<void()> callback);

  /**
   * @brief Execute all registered shutdown callbacks
   */
  static void execute_shutdown_callbacks();

private:
  static void signal_handler(int signal);
};

} // namespace edgesql::core
