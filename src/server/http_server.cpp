/**
 * @file http_server.cpp
 * @brief HTTP server implementation
 */

#include "http_server.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

namespace edgesql {
namespace server {

// HttpResponse static methods

HttpResponse HttpResponse::ok(const std::string &body,
                              const std::string &content_type) {
  HttpResponse resp;
  resp.status_code = 200;
  resp.status_text = "OK";
  resp.body = body;
  resp.headers["Content-Type"] = content_type;
  resp.headers["Content-Length"] = std::to_string(body.size());
  return resp;
}

HttpResponse HttpResponse::error(int code, const std::string &message) {
  HttpResponse resp;
  resp.status_code = code;
  resp.status_text = "Error";
  resp.body = R"({"error":")" + message + R"("})";
  resp.headers["Content-Type"] = "application/json";
  resp.headers["Content-Length"] = std::to_string(resp.body.size());
  return resp;
}

HttpResponse HttpResponse::not_found(const std::string &path) {
  return error(404, "Not found: " + path);
}

HttpResponse HttpResponse::method_not_allowed() {
  return error(405, "Method not allowed");
}

HttpResponse HttpResponse::bad_request(const std::string &message) {
  return error(400, message);
}

HttpResponse HttpResponse::internal_error(const std::string &message) {
  return error(500, message);
}

HttpResponse HttpResponse::service_unavailable(const std::string &message) {
  return error(503, message);
}

// HttpServer implementation

HttpServer::HttpServer(uint16_t port, core::ThreadPool &thread_pool)
    : port_(port), thread_pool_(thread_pool) {}

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start() {
  // Create socket
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    std::cerr << "Failed to create socket\n";
    return false;
  }

  // Set socket options
  int opt = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  // Bind
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);

  if (bind(server_fd_, reinterpret_cast<struct sockaddr *>(&addr),
           sizeof(addr)) < 0) {
    std::cerr << "Failed to bind to port " << port_ << "\n";
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  // Listen
  if (listen(server_fd_, 64) < 0) {
    std::cerr << "Failed to listen\n";
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }

  // Set non-blocking
  int flags = fcntl(server_fd_, F_GETFL, 0);
  fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

  running_ = true;

  std::cout << "HTTP server listening on port " << port_ << "\n";
  return true;
}

void HttpServer::stop() {
  running_ = false;

  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
}

void HttpServer::route(HttpMethod method, const std::string &path,
                       RequestHandler handler) {
  std::string key = method_to_string(method) + " " + path;
  handlers_[key] = std::move(handler);
}

void HttpServer::get(const std::string &path, RequestHandler handler) {
  route(HttpMethod::GET, path, std::move(handler));
}

void HttpServer::post(const std::string &path, RequestHandler handler) {
  route(HttpMethod::POST, path, std::move(handler));
}

void HttpServer::accept_loop() {
  struct pollfd pfd{};
  pfd.fd = server_fd_;
  pfd.events = POLLIN;

  while (running_) {
    int result = poll(&pfd, 1, 100);

    if (result <= 0)
      continue;

    if (pfd.revents & POLLIN) {
      struct sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);

      int client_fd =
          accept(server_fd_, reinterpret_cast<struct sockaddr *>(&client_addr),
                 &client_len);

      if (client_fd < 0)
        continue;

      char client_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
      uint16_t client_port = ntohs(client_addr.sin_port);

      // Handle in thread pool
      thread_pool_.submit(
          [this, client_fd, ip = std::string(client_ip), client_port]() {
            handle_connection(client_fd, ip, client_port);
          });
    }
  }
}

void HttpServer::handle_connection(int client_fd, const std::string &client_ip,
                                   uint16_t client_port) {
  HttpRequest request = parse_request(client_fd);
  request.client_ip = client_ip;
  request.client_port = client_port;

  HttpResponse response = route_request(request);
  send_response(client_fd, response);

  close(client_fd);
}

HttpRequest HttpServer::parse_request(int client_fd) {
  HttpRequest request;

  // Read request (simplified - real implementation would be more robust)
  char buffer[8192];
  ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

  if (bytes <= 0) {
    request.method = HttpMethod::UNKNOWN;
    return request;
  }

  buffer[bytes] = '\0';
  std::string data(buffer);

  // Parse request line
  std::istringstream stream(data);
  std::string method_str, path;
  stream >> method_str >> path;

  request.method = parse_method(method_str);

  // Split path and query string
  size_t qpos = path.find('?');
  if (qpos != std::string::npos) {
    request.query_string = path.substr(qpos + 1);
    request.path = path.substr(0, qpos);
  } else {
    request.path = path;
  }

  // Parse headers (simplified)
  std::string line;
  std::getline(stream, line); // Skip rest of request line

  while (std::getline(stream, line) && line != "\r" && !line.empty()) {
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string name = line.substr(0, colon);
      std::string value = line.substr(colon + 1);
      // Trim
      while (!value.empty() && (value[0] == ' ' || value[0] == '\t')) {
        value = value.substr(1);
      }
      while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
      }
      request.headers[name] = value;
    }
  }

  // Parse body
  size_t body_pos = data.find("\r\n\r\n");
  if (body_pos != std::string::npos) {
    request.body = data.substr(body_pos + 4);
  }

  return request;
}

void HttpServer::send_response(int client_fd, const HttpResponse &response) {
  std::ostringstream out;

  out << "HTTP/1.1 " << response.status_code << " " << response.status_text
      << "\r\n";

  for (const auto &[name, value] : response.headers) {
    out << name << ": " << value << "\r\n";
  }

  out << "Connection: close\r\n";
  out << "\r\n";
  out << response.body;

  std::string data = out.str();
  send(client_fd, data.c_str(), data.size(), 0);
}

HttpResponse HttpServer::route_request(const HttpRequest &request) {
  std::string key = method_to_string(request.method) + " " + request.path;

  auto it = handlers_.find(key);
  if (it != handlers_.end()) {
    try {
      return it->second(request);
    } catch (const std::exception &e) {
      return HttpResponse::internal_error(e.what());
    }
  }

  return HttpResponse::not_found(request.path);
}

HttpMethod HttpServer::parse_method(const std::string &method) {
  if (method == "GET")
    return HttpMethod::GET;
  if (method == "POST")
    return HttpMethod::POST;
  if (method == "PUT")
    return HttpMethod::PUT;
  if (method == "DELETE")
    return HttpMethod::DELETE;
  if (method == "OPTIONS")
    return HttpMethod::OPTIONS;
  if (method == "HEAD")
    return HttpMethod::HEAD;
  return HttpMethod::UNKNOWN;
}

std::string HttpServer::method_to_string(HttpMethod method) {
  switch (method) {
  case HttpMethod::GET:
    return "GET";
  case HttpMethod::POST:
    return "POST";
  case HttpMethod::PUT:
    return "PUT";
  case HttpMethod::DELETE:
    return "DELETE";
  case HttpMethod::OPTIONS:
    return "OPTIONS";
  case HttpMethod::HEAD:
    return "HEAD";
  default:
    return "UNKNOWN";
  }
}

} // namespace server
} // namespace edgesql
