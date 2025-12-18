#pragma once

/**
 * @file listener.hpp
 * @brief TCP connection listener
 */

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace edgesql {
namespace server {

/**
 * @brief Connection information
 */
struct ConnectionInfo {
  int socket_fd;
  std::string client_address;
  uint16_t client_port;
};

/**
 * @brief Connection handler callback
 */
using ConnectionHandler = std::function<void(ConnectionInfo)>;

/**
 * @brief TCP listener for accepting connections
 */
class Listener {
public:
  /**
   * @brief Construct a listener
   * @param address Address to bind to
   * @param port Port to listen on
   * @param handler Connection handler callback
   */
  Listener(const std::string &address, uint16_t port,
           ConnectionHandler handler);

  /**
   * @brief Destructor - stops listening
   */
  ~Listener();

  // Non-copyable, non-movable
  Listener(const Listener &) = delete;
  Listener &operator=(const Listener &) = delete;
  Listener(Listener &&) = delete;
  Listener &operator=(Listener &&) = delete;

  /**
   * @brief Start listening for connections
   * @return true on success
   */
  bool start();

  /**
   * @brief Stop listening
   */
  void stop();

  /**
   * @brief Check if listener is running
   */
  bool running() const { return running_.load(std::memory_order_acquire); }

  /**
   * @brief Get the listening port
   */
  uint16_t port() const { return port_; }

  /**
   * @brief Get the listening address
   */
  const std::string &address() const { return address_; }

  /**
   * @brief Get the server socket file descriptor
   */
  int socket_fd() const { return server_fd_; }

private:
  void accept_loop();
  bool setup_socket();
  void close_socket();

  std::string address_;
  uint16_t port_;
  ConnectionHandler handler_;

  int server_fd_{-1};
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
};

} // namespace server
} // namespace edgesql
