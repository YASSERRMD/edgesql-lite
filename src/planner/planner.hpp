#pragma once

/**
 * @file planner.hpp
 * @brief Query planner
 */

#include "../sql/ast.hpp"
#include "catalog.hpp"
#include "plan.hpp"
#include <optional>
#include <string>

namespace edgesql {
namespace planner {

/**
 * @brief Planning error
 */
struct PlanError {
  std::string message;

  std::string to_string() const { return "Planning error: " + message; }
};

/**
 * @brief Query planner
 *
 * Converts parsed SQL statements to execution plans.
 */
class Planner {
public:
  /**
   * @brief Constructor
   * @param catalog Schema catalog
   */
  explicit Planner(Catalog &catalog);

  /**
   * @brief Plan a statement
   * @param stmt Parsed statement
   * @return Execution plan, or nullopt on error
   */
  std::optional<std::unique_ptr<PlanNode>> plan(const sql::Statement &stmt);

  /**
   * @brief Get the last planning error
   */
  const PlanError &error() const { return error_; }

  /**
   * @brief Check if there was a planning error
   */
  bool has_error() const { return has_error_; }

private:
  std::unique_ptr<PlanNode> plan_select(const sql::SelectStmt &stmt);
  std::unique_ptr<PlanNode> plan_insert(const sql::InsertStmt &stmt);
  std::unique_ptr<PlanNode> plan_create_table(const sql::CreateTableStmt &stmt);
  std::unique_ptr<PlanNode> plan_drop_table(const sql::DropTableStmt &stmt);

  bool validate_columns(const sql::SelectStmt &stmt, const TableInfo *table);
  bool
  detect_aggregates(const std::vector<std::unique_ptr<sql::Expression>> &exprs);

  void set_error(const std::string &message);

  Catalog &catalog_;
  PlanError error_;
  bool has_error_{false};
};

} // namespace planner
} // namespace edgesql
