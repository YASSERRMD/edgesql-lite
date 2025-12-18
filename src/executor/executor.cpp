/**
 * @file executor.cpp
 * @brief Executor implementation
 */

#include "executor.hpp"
#include <algorithm>
#include <iostream>

namespace edgesql {
namespace executor {

// TableScanOperator implementation

TableScanOperator::TableScanOperator(uint32_t table_id,
                                     const std::string &table_name,
                                     storage::PageManager &page_manager,
                                     const planner::TableInfo *schema)
    : table_id_(table_id), table_name_(table_name), page_manager_(page_manager),
      schema_(schema) {}

void TableScanOperator::open(ExecutionContext &ctx) {
  current_page_ = 0;
  current_slot_ = 0;
  page_ = page_manager_.get_page(table_id_, current_page_);
  ctx.record_instructions(10); // Opening cost
}

bool TableScanOperator::next(ExecutionContext &ctx, ResultRow &row) {
  ctx.record_instructions(1);

  while (page_ != nullptr) {
    // Try to read from current page
    while (current_slot_ < page_->slot_count()) {
      const uint8_t *data = nullptr;
      uint16_t length = 0;

      if (page_->get_record(current_slot_, &data, &length)) {
        ctx.record_row_scanned();
        ctx.record_instructions(5);

        // Parse record into row
        row.values.clear();
        if (schema_) {
          for (size_t i = 0; i < schema_->columns.size(); ++i) {
            // Simplified: just add null values
            // In real implementation, parse actual record data
            row.values.push_back(sql::Literal::null());
          }
        }

        current_slot_++;
        return true;
      }
      current_slot_++;
    }

    // Move to next page
    current_page_++;
    current_slot_ = 0;
    page_ = page_manager_.get_page(table_id_, current_page_);
    ctx.record_instructions(10);
  }

  return false;
}

void TableScanOperator::close() { page_ = nullptr; }

std::vector<std::string> TableScanOperator::column_names() const {
  std::vector<std::string> names;
  if (schema_) {
    for (const auto &col : schema_->columns) {
      names.push_back(col.name);
    }
  }
  return names;
}

// FilterOperator implementation

FilterOperator::FilterOperator(std::unique_ptr<Operator> child,
                               const sql::Expression *predicate)
    : child_(std::move(child)), predicate_(predicate) {}

void FilterOperator::open(ExecutionContext &ctx) { child_->open(ctx); }

bool FilterOperator::next(ExecutionContext &ctx, ResultRow &row) {
  while (child_->next(ctx, row)) {
    ctx.record_instructions(5); // Evaluation cost

    if (!predicate_ || evaluate_predicate(row)) {
      return true;
    }
  }
  return false;
}

void FilterOperator::close() { child_->close(); }

std::vector<std::string> FilterOperator::column_names() const {
  return child_->column_names();
}

bool FilterOperator::evaluate_predicate(const ResultRow &row) {
  // Simplified: always return true
  // In real implementation, evaluate the expression tree
  (void)row;
  return true;
}

// LimitOperator implementation

LimitOperator::LimitOperator(std::unique_ptr<Operator> child, int64_t limit,
                             int64_t offset)
    : child_(std::move(child)), limit_(limit), offset_(offset) {}

void LimitOperator::open(ExecutionContext &ctx) {
  child_->open(ctx);
  skipped_ = 0;
  returned_ = 0;
}

bool LimitOperator::next(ExecutionContext &ctx, ResultRow &row) {
  // Skip offset rows
  while (skipped_ < offset_) {
    if (!child_->next(ctx, row)) {
      return false;
    }
    skipped_++;
    ctx.record_instructions(1);
  }

  // Return up to limit rows
  if (limit_ >= 0 && returned_ >= limit_) {
    return false;
  }

  if (child_->next(ctx, row)) {
    returned_++;
    ctx.record_row_returned();
    return true;
  }

  return false;
}

void LimitOperator::close() { child_->close(); }

std::vector<std::string> LimitOperator::column_names() const {
  return child_->column_names();
}

// SortOperator implementation

SortOperator::SortOperator(std::unique_ptr<Operator> child,
                           std::vector<size_t> sort_columns,
                           std::vector<bool> ascending)
    : child_(std::move(child)), sort_columns_(std::move(sort_columns)),
      ascending_(std::move(ascending)) {}

void SortOperator::open(ExecutionContext &ctx) {
  child_->open(ctx);
  buffer_.clear();
  current_row_ = 0;
  materialized_ = false;
}

bool SortOperator::next(ExecutionContext &ctx, ResultRow &row) {
  // Materialize all rows on first call
  if (!materialized_) {
    ResultRow temp;
    while (child_->next(ctx, temp)) {
      buffer_.push_back(std::move(temp));
      ctx.record_instructions(2);
      ctx.check_budget(); // Check budget while materializing
    }

    // Sort the buffer
    std::sort(buffer_.begin(), buffer_.end(),
              [this](const ResultRow &a, const ResultRow &b) {
                for (size_t i = 0; i < sort_columns_.size(); ++i) {
                  size_t col = sort_columns_[i];
                  if (col >= a.values.size() || col >= b.values.size())
                    continue;

                  // Simplified comparison (just compare as integers)
                  const auto &va = a.values[col];
                  const auto &vb = b.values[col];

                  if (va.type == sql::Literal::Type::INTEGER &&
                      vb.type == sql::Literal::Type::INTEGER) {
                    if (va.int_value != vb.int_value) {
                      return ascending_[i] ? (va.int_value < vb.int_value)
                                           : (va.int_value > vb.int_value);
                    }
                  }
                }
                return false;
              });

    ctx.record_instructions(static_cast<uint64_t>(buffer_.size()) *
                            10); // Sort cost
    materialized_ = true;
  }

  if (current_row_ < buffer_.size()) {
    row = std::move(buffer_[current_row_]);
    current_row_++;
    return true;
  }

  return false;
}

void SortOperator::close() {
  child_->close();
  buffer_.clear();
}

std::vector<std::string> SortOperator::column_names() const {
  return child_->column_names();
}

// Executor implementation

Executor::Executor(storage::PageManager &page_manager,
                   planner::Catalog &catalog)
    : page_manager_(page_manager), catalog_(catalog) {}

ExecutionResult Executor::execute(const planner::PlanNode &plan,
                                  ExecutionContext &ctx) {
  ctx.start();

  ExecutionResult result;
  result.success = false;

  try {
    switch (plan.type) {
    case planner::PlanNodeType::TABLE_SCAN:
    case planner::PlanNodeType::FILTER:
    case planner::PlanNodeType::PROJECT:
    case planner::PlanNodeType::SORT:
    case planner::PlanNodeType::LIMIT:
    case planner::PlanNodeType::AGGREGATE:
      result = execute_select(plan, ctx);
      break;

    case planner::PlanNodeType::INSERT: {
      const auto *node = std::get_if<planner::InsertNode>(&plan.node);
      if (node) {
        result = execute_insert(*node, ctx);
      }
      break;
    }

    case planner::PlanNodeType::CREATE_TABLE: {
      const auto *node = std::get_if<planner::CreateTableNode>(&plan.node);
      if (node) {
        result = execute_create_table(*node, ctx);
      }
      break;
    }

    case planner::PlanNodeType::DROP_TABLE: {
      const auto *node = std::get_if<planner::DropTableNode>(&plan.node);
      if (node) {
        result = execute_drop_table(*node, ctx);
      }
      break;
    }

    default:
      result.error = "Unsupported plan type";
      break;
    }
  } catch (const std::exception &e) {
    result.success = false;
    result.error = e.what();
  }

  ctx.finalize();
  result.stats = ctx.stats();

  return result;
}

std::unique_ptr<Operator>
Executor::build_operator(const planner::PlanNode &plan) {
  switch (plan.type) {
  case planner::PlanNodeType::TABLE_SCAN: {
    const auto *node = std::get_if<planner::TableScanNode>(&plan.node);
    if (node) {
      const auto *schema = catalog_.get_table_by_id(node->table_id);
      return std::make_unique<TableScanOperator>(
          node->table_id, node->table_name, page_manager_, schema);
    }
    break;
  }

  case planner::PlanNodeType::FILTER: {
    const auto *node = std::get_if<planner::FilterNode>(&plan.node);
    if (node && node->child) {
      auto child = build_operator(*node->child);
      return std::make_unique<FilterOperator>(std::move(child),
                                              node->predicate.get());
    }
    break;
  }

  case planner::PlanNodeType::LIMIT: {
    const auto *node = std::get_if<planner::LimitNode>(&plan.node);
    if (node && node->child) {
      auto child = build_operator(*node->child);
      return std::make_unique<LimitOperator>(std::move(child), node->limit,
                                             node->offset);
    }
    break;
  }

  case planner::PlanNodeType::SORT: {
    const auto *node = std::get_if<planner::SortNode>(&plan.node);
    if (node && node->child) {
      auto child = build_operator(*node->child);
      // Extract sort columns (simplified)
      std::vector<size_t> cols;
      std::vector<bool> asc;
      for (size_t i = 0; i < node->ascending.size(); ++i) {
        cols.push_back(i);
        asc.push_back(node->ascending[i]);
      }
      return std::make_unique<SortOperator>(std::move(child), std::move(cols),
                                            std::move(asc));
    }
    break;
  }

  default:
    break;
  }

  return nullptr;
}

ExecutionResult Executor::execute_select(const planner::PlanNode &plan,
                                         ExecutionContext &ctx) {
  ExecutionResult result;

  auto op = build_operator(plan);
  if (!op) {
    result.error = "Failed to build operator tree";
    return result;
  }

  op->open(ctx);
  result.column_names = op->column_names();

  ResultRow row;
  while (op->next(ctx, row)) {
    result.rows.push_back(std::move(row));
    ctx.check_budget();
  }

  op->close();
  result.success = true;
  return result;
}

ExecutionResult Executor::execute_insert(const planner::InsertNode &node,
                                         ExecutionContext &ctx) {
  ExecutionResult result;

  // Get table schema
  const auto *table = catalog_.get_table(node.table_name);
  if (!table) {
    result.error = "Table not found: " + node.table_name;
    return result;
  }

  // Insert rows (simplified - just count them)
  result.rows_affected = node.values.size();
  ctx.record_instructions(static_cast<uint64_t>(node.values.size()) * 20);

  result.success = true;
  return result;
}

ExecutionResult
Executor::execute_create_table(const planner::CreateTableNode &node,
                               ExecutionContext &ctx) {
  ExecutionResult result;

  // Check if table exists
  if (!node.if_not_exists && catalog_.table_exists(node.table_name)) {
    result.error = "Table already exists: " + node.table_name;
    return result;
  }

  // Create table in catalog
  std::vector<planner::ColumnInfo> columns;
  for (const auto &col : node.columns) {
    planner::ColumnInfo info;
    info.name = col.name;
    info.not_null = col.not_null;
    info.primary_key = col.primary_key;

    // Map type string to ColumnType
    if (col.type == "INTEGER") {
      info.type = storage::ColumnType::INTEGER;
    } else if (col.type == "TEXT") {
      info.type = storage::ColumnType::TEXT;
    } else if (col.type == "FLOAT") {
      info.type = storage::ColumnType::FLOAT;
    } else if (col.type == "BOOLEAN") {
      info.type = storage::ColumnType::BOOLEAN;
    } else if (col.type == "BLOB") {
      info.type = storage::ColumnType::BLOB;
    } else {
      info.type = storage::ColumnType::TEXT;
    }

    columns.push_back(info);
  }

  uint32_t table_id = catalog_.create_table(node.table_name, columns);
  if (table_id == 0 && !node.if_not_exists) {
    result.error = "Failed to create table";
    return result;
  }

  ctx.record_instructions(100);
  result.success = true;
  return result;
}

ExecutionResult Executor::execute_drop_table(const planner::DropTableNode &node,
                                             ExecutionContext &ctx) {
  ExecutionResult result;

  if (!catalog_.table_exists(node.table_name)) {
    if (!node.if_exists) {
      result.error = "Table not found: " + node.table_name;
      return result;
    }
  } else {
    catalog_.drop_table(node.table_name);
  }

  ctx.record_instructions(50);
  result.success = true;
  return result;
}

} // namespace executor
} // namespace edgesql
