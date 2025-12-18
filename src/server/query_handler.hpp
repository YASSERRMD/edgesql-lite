#pragma once

/**
 * @file query_handler.hpp
 * @brief HTTP query endpoint handler
 */

#include "../executor/context.hpp"
#include "../executor/executor.hpp"
#include "../memory/arena.hpp"
#include "../planner/planner.hpp"
#include "../sql/parser.hpp"
#include "http_server.hpp"

namespace edgesql {
namespace server {

/**
 * @brief Query handler
 *
 * Handles SQL query requests over HTTP.
 */
class QueryHandler {
public:
  /**
   * @brief Constructor
   * @param executor Query executor
   * @param planner Query planner
   */
  QueryHandler(executor::Executor &executor, planner::Planner &planner);

  /**
   * @brief Handle a query request
   * @param request HTTP request
   * @return HTTP response
   */
  HttpResponse handle(const HttpRequest &request);

  /**
   * @brief Get handler function
   */
  RequestHandler get_handler();

  /**
   * @brief Set default budget
   */
  void set_budget(const executor::QueryBudget &budget) { budget_ = budget; }

private:
  std::string format_result(const executor::ExecutionResult &result);
  std::string format_error(const std::string &message);
  std::string format_stats(const executor::ExecutionStats &stats);

  executor::Executor &executor_;
  planner::Planner &planner_;
  executor::QueryBudget budget_;
};

} // namespace server
} // namespace edgesql
