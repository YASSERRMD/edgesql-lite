/**
 * @file planner.cpp
 * @brief Query planner implementation
 */

#include "planner.hpp"
#include <algorithm>

namespace edgesql {
namespace planner {

Planner::Planner(Catalog &catalog) : catalog_(catalog) {}

std::optional<std::unique_ptr<PlanNode>>
Planner::plan(const sql::Statement &stmt) {
  has_error_ = false;

  std::unique_ptr<PlanNode> plan;

  switch (stmt.type) {
  case sql::StmtType::SELECT: {
    const auto *select =
        std::get_if<std::unique_ptr<sql::SelectStmt>>(&stmt.stmt);
    if (select) {
      plan = plan_select(**select);
    }
    break;
  }
  case sql::StmtType::INSERT: {
    const auto *insert =
        std::get_if<std::unique_ptr<sql::InsertStmt>>(&stmt.stmt);
    if (insert) {
      plan = plan_insert(**insert);
    }
    break;
  }
  case sql::StmtType::CREATE_TABLE: {
    const auto *create =
        std::get_if<std::unique_ptr<sql::CreateTableStmt>>(&stmt.stmt);
    if (create) {
      plan = plan_create_table(**create);
    }
    break;
  }
  case sql::StmtType::DROP_TABLE: {
    const auto *drop =
        std::get_if<std::unique_ptr<sql::DropTableStmt>>(&stmt.stmt);
    if (drop) {
      plan = plan_drop_table(**drop);
    }
    break;
  }
  }

  if (has_error_ || !plan) {
    return std::nullopt;
  }

  return plan;
}

std::unique_ptr<PlanNode> Planner::plan_select(const sql::SelectStmt &stmt) {
  // Look up table
  const TableInfo *table = catalog_.get_table(stmt.table_name);
  if (!table) {
    set_error("Table not found: " + stmt.table_name);
    return nullptr;
  }

  // Validate columns
  if (!validate_columns(stmt, table)) {
    return nullptr;
  }

  // Start with table scan
  auto plan = PlanNode::table_scan(table->id, table->name);

  // Add filter if WHERE clause
  if (stmt.where_clause) {
    // Deep copy the expression for the plan
    // In a real implementation, we'd properly clone expressions
    // For now, we'll use a simple approach
    plan = PlanNode::filter(std::move(plan), nullptr);
  }

  // Check for aggregates
  bool has_aggregates = detect_aggregates(stmt.columns);

  if (has_aggregates) {
    // Add aggregate node
    std::vector<AggregateExpr> aggs;
    // TODO: Extract aggregates from columns
    plan = PlanNode::aggregate(std::move(plan), std::move(aggs));
  }

  // Add projection
  std::vector<std::unique_ptr<sql::Expression>> exprs;
  std::vector<std::string> names;
  // TODO: Deep copy expressions for projection

  // Add sort if ORDER BY
  if (!stmt.order_by.empty()) {
    std::vector<std::unique_ptr<sql::Expression>> sort_keys;
    std::vector<bool> ascending;

    for (const auto &item : stmt.order_by) {
      ascending.push_back(item.ascending);
      // TODO: Clone sort key expressions
    }

    plan = PlanNode::sort(std::move(plan), std::move(sort_keys),
                          std::move(ascending));
  }

  // Add limit if present
  if (stmt.limit >= 0) {
    plan = PlanNode::limit(std::move(plan), stmt.limit, stmt.offset);
  }

  return plan;
}

std::unique_ptr<PlanNode> Planner::plan_insert(const sql::InsertStmt &stmt) {
  // Look up table
  const TableInfo *table = catalog_.get_table(stmt.table_name);
  if (!table) {
    set_error("Table not found: " + stmt.table_name);
    return nullptr;
  }

  // Validate columns if specified
  if (!stmt.column_names.empty()) {
    for (const auto &col_name : stmt.column_names) {
      if (table->find_column(col_name) < 0) {
        set_error("Column not found: " + col_name);
        return nullptr;
      }
    }
  }

  // Validate value count
  size_t expected_cols = stmt.column_names.empty() ? table->columns.size()
                                                   : stmt.column_names.size();

  for (const auto &row : stmt.values) {
    if (row.size() != expected_cols) {
      set_error("Value count mismatch");
      return nullptr;
    }
  }

  // Create insert node
  std::vector<std::vector<std::unique_ptr<sql::Expression>>> values_copy;
  // TODO: Deep copy values

  return PlanNode::insert(table->id, table->name, stmt.column_names,
                          std::move(values_copy));
}

std::unique_ptr<PlanNode>
Planner::plan_create_table(const sql::CreateTableStmt &stmt) {
  // Check if table exists
  if (!stmt.if_not_exists && catalog_.table_exists(stmt.table_name)) {
    set_error("Table already exists: " + stmt.table_name);
    return nullptr;
  }

  std::vector<sql::ColumnDef> columns_copy;
  for (const auto &col : stmt.columns) {
    sql::ColumnDef copy;
    copy.name = col.name;
    copy.type = col.type;
    copy.not_null = col.not_null;
    copy.primary_key = col.primary_key;
    // Note: default_value copy would need proper cloning
    columns_copy.push_back(std::move(copy));
  }

  return PlanNode::create_table(stmt.table_name, std::move(columns_copy),
                                stmt.if_not_exists);
}

std::unique_ptr<PlanNode>
Planner::plan_drop_table(const sql::DropTableStmt &stmt) {
  // Check if table exists
  if (!stmt.if_exists && !catalog_.table_exists(stmt.table_name)) {
    set_error("Table not found: " + stmt.table_name);
    return nullptr;
  }

  return PlanNode::drop_table(stmt.table_name, stmt.if_exists);
}

bool Planner::validate_columns(const sql::SelectStmt &stmt,
                               const TableInfo *table) {
  for (const auto &col_expr : stmt.columns) {
    if (col_expr->type == sql::ExprType::STAR) {
      continue; // SELECT * is always valid
    }

    if (col_expr->type == sql::ExprType::COLUMN_REF) {
      const auto *ref = std::get_if<sql::ColumnRef>(&col_expr->value);
      if (ref && table->find_column(ref->column_name) < 0) {
        set_error("Column not found: " + ref->column_name);
        return false;
      }
    }

    // For other expression types, we'd need to recursively validate
  }

  return true;
}

bool Planner::detect_aggregates(
    const std::vector<std::unique_ptr<sql::Expression>> &exprs) {
  for (const auto &expr : exprs) {
    if (expr->type == sql::ExprType::FUNCTION_CALL) {
      const auto *fn =
          std::get_if<std::unique_ptr<sql::FunctionCall>>(&expr->value);
      if (fn) {
        const std::string &name = (*fn)->name;
        if (name == "COUNT" || name == "SUM" || name == "MIN" ||
            name == "MAX" || name == "AVG") {
          return true;
        }
      }
    }
  }
  return false;
}

void Planner::set_error(const std::string &message) {
  has_error_ = true;
  error_.message = message;
}

} // namespace planner
} // namespace edgesql
