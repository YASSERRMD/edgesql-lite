/**
 * @file main.cpp
 * @brief EdgeSQL Lite main entry point
 *
 * EdgeSQL Lite - A deterministic, budget-enforced SQL server for edge systems.
 */

#include "core/shutdown.hpp"
#include "core/signal_handler.hpp"
#include "core/thread_pool.hpp"
#include "edgesql/config.hpp"
#include "server/listener.hpp"

#include <cstdlib>
#include <getopt.h>
#include <iostream>
#include <string>

namespace {

/**
 * @brief Print usage information
 */
void print_usage(const char *program_name) {
  std::cout
      << "EdgeSQL Lite v" << edgesql::VERSION << "\n"
      << "A deterministic, budget-enforced SQL server for edge systems\n\n"
      << "Usage: " << program_name << " [OPTIONS]\n\n"
      << "Options:\n"
      << "  -h, --help              Show this help message\n"
      << "  -v, --version           Show version information\n"
      << "  -c, --config FILE       Path to configuration file\n"
      << "  -p, --port PORT         Port to listen on (default: 8080)\n"
      << "  -b, --bind ADDRESS      Address to bind to (default: 0.0.0.0)\n"
      << "  -d, --data-dir DIR      Data directory (default: ./data)\n"
      << "  -w, --workers N         Number of worker threads (default: auto)\n"
      << "\n";
}

/**
 * @brief Print version information
 */
void print_version() {
  std::cout << "EdgeSQL Lite v" << edgesql::VERSION << "\n"
            << "Built: " << edgesql::BUILD_DATE << "\n"
            << "C++ Standard: " << __cplusplus << "\n";
}

/**
 * @brief Parse command line arguments
 */
edgesql::Config parse_args(int argc, char *argv[]) {
  edgesql::Config config = edgesql::Config::defaults();

  static struct option long_options[] = {
      {"help", no_argument, nullptr, 'h'},
      {"version", no_argument, nullptr, 'v'},
      {"config", required_argument, nullptr, 'c'},
      {"port", required_argument, nullptr, 'p'},
      {"bind", required_argument, nullptr, 'b'},
      {"data-dir", required_argument, nullptr, 'd'},
      {"workers", required_argument, nullptr, 'w'},
      {nullptr, 0, nullptr, 0}};

  int opt;
  while ((opt = getopt_long(argc, argv, "hvc:p:b:d:w:", long_options,
                            nullptr)) != -1) {
    switch (opt) {
    case 'h':
      print_usage(argv[0]);
      std::exit(0);
    case 'v':
      print_version();
      std::exit(0);
    case 'c':
      config = edgesql::Config::load(optarg);
      break;
    case 'p':
      config.server.port = static_cast<uint16_t>(std::stoi(optarg));
      break;
    case 'b':
      config.server.bind_address = optarg;
      break;
    case 'd':
      config.storage.data_dir = optarg;
      break;
    case 'w':
      config.server.worker_threads = static_cast<size_t>(std::stoi(optarg));
      break;
    default:
      print_usage(argv[0]);
      std::exit(1);
    }
  }

  return config;
}

} // anonymous namespace

int main(int argc, char *argv[]) {
  std::cout << "EdgeSQL Lite v" << edgesql::VERSION << " starting...\n";

  // Parse command line arguments
  edgesql::Config config = parse_args(argc, argv);

  // Install signal handlers
  edgesql::core::SignalHandler::install();

  // Create thread pool
  edgesql::core::ThreadPool thread_pool(config.server.worker_threads);
  std::cout << "Thread pool initialized with " << thread_pool.size()
            << " workers\n";

  // Create listener
  edgesql::server::Listener listener(
      config.server.bind_address, config.server.port,
      [&thread_pool](edgesql::server::ConnectionInfo info) {
        // Dispatch connection handling to thread pool
        thread_pool.submit([info = std::move(info)]() {
          // TODO: Handle connection with HTTP server
          std::cout << "Connection from " << info.client_address << ":"
                    << info.client_port << "\n";

          // For now, just close the connection
          close(info.socket_fd);
        });
      });

  // Register shutdown callbacks
  edgesql::core::ShutdownCoordinator::instance().register_callback(
      edgesql::core::ShutdownCoordinator::Phase::STOP_ACCEPTING,
      [&listener]() { listener.stop(); });

  edgesql::core::ShutdownCoordinator::instance().register_callback(
      edgesql::core::ShutdownCoordinator::Phase::DRAIN_CONNECTIONS,
      [&thread_pool]() { thread_pool.shutdown(); });

  // Start listener
  if (!listener.start()) {
    std::cerr << "Failed to start listener\n";
    return 1;
  }

  std::cout << "EdgeSQL Lite ready - listening on "
            << config.server.bind_address << ":" << config.server.port << "\n";

  // Main loop - wait for shutdown signal
  while (!edgesql::core::SignalHandler::shutdown_requested()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Initiate graceful shutdown
  edgesql::core::ShutdownCoordinator::instance().initiate(
      std::chrono::seconds(30));

  std::cout << "EdgeSQL Lite shutdown complete\n";
  return 0;
}

// Implement Config methods
namespace edgesql {

Config Config::defaults() { return Config{}; }

Config Config::load([[maybe_unused]] const std::string &path) {
  // TODO: Implement configuration file loading
  std::cout << "Loading configuration from: " << path << "\n";
  return defaults();
}

} // namespace edgesql
