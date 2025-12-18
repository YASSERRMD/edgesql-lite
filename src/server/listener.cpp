/**
 * @file listener.cpp
 * @brief TCP connection listener implementation
 */

#include "listener.hpp"
#include "../core/signal_handler.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

namespace edgesql::server {

Listener::Listener(const std::string &address, uint16_t port,
                   ConnectionHandler handler)
    : address_(address), port_(port), handler_(std::move(handler)) {}

Listener::~Listener() { stop(); }

bool Listener::start() {
  if (running_.load(std::memory_order_acquire)) {
    return true; // Already running
  }

  if (!setup_socket()) {
    return false;
  }

  running_.store(true, std::memory_order_release);
  accept_thread_ = std::thread([this]() { accept_loop(); });

  std::cout << "Listener started on " << address_ << ":" << port_ << "\n";
  return true;
}

void Listener::stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    return; // Not running
  }

  // Close socket to interrupt accept()
  close_socket();

  // Wait for thread
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  std::cout << "Listener stopped\n";
}

bool Listener::setup_socket() {
  // Create socket
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    std::cerr << "Failed to create socket: " << strerror(errno) << "\n";
    return false;
  }

  // Set socket options
  int opt = 1;
  if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    std::cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << "\n";
    close_socket();
    return false;
  }

  // Set non-blocking
  int flags = fcntl(server_fd_, F_GETFL, 0);
  if (flags < 0 || fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
    std::cerr << "Failed to set non-blocking: " << strerror(errno) << "\n";
    close_socket();
    return false;
  }

  // Bind
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port_);

  if (address_ == "0.0.0.0" || address_.empty()) {
    addr.sin_addr.s_addr = INADDR_ANY;
  } else {
    if (inet_pton(AF_INET, address_.c_str(), &addr.sin_addr) <= 0) {
      std::cerr << "Invalid address: " << address_ << "\n";
      close_socket();
      return false;
    }
  }

  if (bind(server_fd_, reinterpret_cast<struct sockaddr *>(&addr),
           sizeof(addr)) < 0) {
    std::cerr << "Failed to bind: " << strerror(errno) << "\n";
    close_socket();
    return false;
  }

  // Listen
  constexpr int backlog = 128;
  if (listen(server_fd_, backlog) < 0) {
    std::cerr << "Failed to listen: " << strerror(errno) << "\n";
    close_socket();
    return false;
  }

  return true;
}

void Listener::close_socket() {
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
}

void Listener::accept_loop() {
  struct pollfd pfd{};
  pfd.fd = server_fd_;
  pfd.events = POLLIN;

  while (running_.load(std::memory_order_acquire) &&
         !edgesql::core::SignalHandler::shutdown_requested()) {

    // Poll with timeout to check shutdown periodically
    int result = poll(&pfd, 1, 100); // 100ms timeout

    if (result < 0) {
      if (errno == EINTR) {
        continue; // Interrupted by signal
      }
      std::cerr << "Poll error: " << strerror(errno) << "\n";
      break;
    }

    if (result == 0) {
      continue; // Timeout, check shutdown flag
    }

    if (!(pfd.revents & POLLIN)) {
      continue;
    }

    // Accept connection
    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    int client_fd =
        accept(server_fd_, reinterpret_cast<struct sockaddr *>(&client_addr),
               &client_len);

    if (client_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        continue; // No pending connections
      }
      if (errno == EINTR) {
        continue; // Interrupted by signal
      }
      std::cerr << "Accept error: " << strerror(errno) << "\n";
      continue;
    }

    // Get client info
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));

    ConnectionInfo info{.socket_fd = client_fd,
                        .client_address = client_ip,
                        .client_port = ntohs(client_addr.sin_port)};

    // Dispatch to handler (should be quick, actual work done in thread pool)
    try {
      handler_(std::move(info));
    } catch (const std::exception &e) {
      std::cerr << "Handler error: " << e.what() << "\n";
      close(client_fd);
    }
  }
}

} // namespace edgesql::server
