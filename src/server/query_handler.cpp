/**
 * @file query_handler.cpp
 * @brief Query handler implementation
 */

#include "query_handler.hpp"
#include <sstream>

namespace edgesql {
namespace server {

QueryHandler::QueryHandler(executor::Executor &executor,
                           planner::Planner &planner)
    : executor_(executor), planner_(planner) {}

HttpResponse QueryHandler::handle(const HttpRequest &request) {
  // Extract query from body or query string
  std::string query;

  if (!request.body.empty()) {
    query = request.body;
  } else {
    // Look for 'q' parameter in query string
    size_t pos = request.query_string.find("q=");
    if (pos != std::string::npos) {
      query = request.query_string.substr(pos + 2);
      // URL decode (simplified)
      size_t end = query.find('&');
      if (end != std::string::npos) {
        query = query.substr(0, end);
      }
    }
  }

  if (query.empty()) {
    return HttpResponse::bad_request("No query provided");
  }

  // Parse query
  sql::Parser parser(query);
  auto stmt = parser.parse();

  if (!stmt) {
    return HttpResponse::bad_request(parser.error().to_string());
  }

  // Plan query
  auto plan = planner_.plan(*stmt);

  if (!plan) {
    return HttpResponse::bad_request(planner_.error().to_string());
  }

  // Create execution context
  memory::Arena arena(64 * 1024); // 64KB blocks
  memory::QueryAllocator allocator(budget_.max_memory_bytes, arena);
  executor::ExecutionContext ctx(budget_, allocator);

  // Execute
  auto result = executor_.execute(**plan, ctx);

  if (!result.success) {
    // Check if budget violation
    if (ctx.violation() != executor::BudgetViolation::NONE) {
      std::string response = format_error(ctx.violation_message());
      return HttpResponse::error(429,
                                 "Budget exceeded: " + ctx.violation_message());
    }
    return HttpResponse::internal_error(result.error);
  }

  // Format response
  std::string response = format_result(result);
  return HttpResponse::ok(response);
}

RequestHandler QueryHandler::get_handler() {
  return [this](const HttpRequest &request) { return handle(request); };
}

std::string
QueryHandler::format_result(const executor::ExecutionResult &result) {
  std::ostringstream out;

  out << "{\n";
  out << "  \"success\": true,\n";

  // Column names
  out << "  \"columns\": [";
  for (size_t i = 0; i < result.column_names.size(); ++i) {
    if (i > 0)
      out << ", ";
    out << "\"" << result.column_names[i] << "\"";
  }
  out << "],\n";

  // Rows
  out << "  \"rows\": [\n";
  for (size_t i = 0; i < result.rows.size(); ++i) {
    out << "    [";
    const auto &row = result.rows[i];
    for (size_t j = 0; j < row.values.size(); ++j) {
      if (j > 0)
        out << ", ";

      const auto &val = row.values[j];
      switch (val.type) {
      case sql::Literal::Type::NULL_VAL:
        out << "null";
        break;
      case sql::Literal::Type::INTEGER:
        out << val.int_value;
        break;
      case sql::Literal::Type::FLOAT:
        out << val.float_value;
        break;
      case sql::Literal::Type::STRING:
        out << "\"" << val.string_value << "\"";
        break;
      case sql::Literal::Type::BOOLEAN:
        out << (val.bool_value ? "true" : "false");
        break;
      }
    }
    out << "]";
    if (i < result.rows.size() - 1)
      out << ",";
    out << "\n";
  }
  out << "  ],\n";

  // Stats
  out << "  \"rows_affected\": " << result.rows_affected << ",\n";
  out << "  \"stats\": " << format_stats(result.stats) << "\n";
  out << "}";

  return out.str();
}

std::string QueryHandler::format_error(const std::string &message) {
  std::ostringstream out;
  out << "{\n";
  out << "  \"success\": false,\n";
  out << "  \"error\": \"" << message << "\"\n";
  out << "}";
  return out.str();
}

std::string QueryHandler::format_stats(const executor::ExecutionStats &stats) {
  std::ostringstream out;
  out << "{\n";
  out << "    \"instructions\": " << stats.instructions_executed << ",\n";
  out << "    \"rows_scanned\": " << stats.rows_scanned << ",\n";
  out << "    \"rows_returned\": " << stats.rows_returned << ",\n";
  out << "    \"memory_bytes\": " << stats.memory_used << ",\n";
  out << "    \"elapsed_us\": " << stats.elapsed_time.count() << "\n";
  out << "  }";
  return out.str();
}

} // namespace server
} // namespace edgesql
