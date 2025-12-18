#pragma once

/**
 * @file http_server.hpp
 * @brief Simple HTTP server for query handling
 */

#include "../core/thread_pool.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace edgesql {
namespace server {

/**
 * @brief HTTP method
 */
enum class HttpMethod { GET, POST, PUT, DELETE, OPTIONS, HEAD, UNKNOWN };

/**
 * @brief HTTP request
 */
struct HttpRequest {
  HttpMethod method;
  std::string path;
  std::string query_string;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
  std::string client_ip;
  uint16_t client_port;
};

/**
 * @brief HTTP response
 */
struct HttpResponse {
  int status_code{200};
  std::string status_text{"OK"};
  std::unordered_map<std::string, std::string> headers;
  std::string body;

  static HttpResponse ok(const std::string &body,
                         const std::string &content_type = "application/json");
  static HttpResponse error(int code, const std::string &message);
  static HttpResponse not_found(const std::string &path);
  static HttpResponse method_not_allowed();
  static HttpResponse bad_request(const std::string &message);
  static HttpResponse internal_error(const std::string &message);
  static HttpResponse service_unavailable(const std::string &message);
};

/**
 * @brief Request handler function
 */
using RequestHandler = std::function<HttpResponse(const HttpRequest &)>;

/**
 * @brief HTTP server
 */
class HttpServer {
public:
  /**
   * @brief Constructor
   * @param port Port to listen on
   * @param thread_pool Thread pool for handling requests
   */
  HttpServer(uint16_t port, core::ThreadPool &thread_pool);

  /**
   * @brief Destructor
   */
  ~HttpServer();

  /**
   * @brief Start the server
   */
  bool start();

  /**
   * @brief Stop the server
   */
  void stop();

  /**
   * @brief Check if running
   */
  bool is_running() const { return running_; }

  /**
   * @brief Register a handler for a path
   */
  void route(HttpMethod method, const std::string &path,
             RequestHandler handler);

  /**
   * @brief Register a handler for GET requests
   */
  void get(const std::string &path, RequestHandler handler);

  /**
   * @brief Register a handler for POST requests
   */
  void post(const std::string &path, RequestHandler handler);

private:
  void accept_loop();
  void handle_connection(int client_fd, const std::string &client_ip,
                         uint16_t client_port);
  HttpRequest parse_request(int client_fd);
  void send_response(int client_fd, const HttpResponse &response);
  HttpResponse route_request(const HttpRequest &request);

  static HttpMethod parse_method(const std::string &method);
  static std::string method_to_string(HttpMethod method);

  uint16_t port_;
  core::ThreadPool &thread_pool_;
  int server_fd_{-1};
  bool running_{false};

  std::unordered_map<std::string, RequestHandler> handlers_;
};

} // namespace server
} // namespace edgesql
