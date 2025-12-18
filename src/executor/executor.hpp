#pragma once

/**
 * @file executor.hpp
 * @brief Pull-based query executor
 */

#include "../planner/catalog.hpp"
#include "../planner/plan.hpp"
#include "../sql/ast.hpp"
#include "../storage/page_manager.hpp"
#include "context.hpp"
#include <memory>
#include <variant>
#include <vector>

namespace edgesql {
namespace executor {

/**
 * @brief Result row (column values)
 */
struct ResultRow {
  std::vector<sql::Literal> values;
};

/**
 * @brief Execution result
 */
struct ExecutionResult {
  bool success{false};
  std::string error;
  std::vector<std::string> column_names;
  std::vector<ResultRow> rows;
  uint64_t rows_affected{0};
  ExecutionStats stats;
};

/**
 * @brief Base operator interface (pull-based)
 */
class Operator {
public:
  virtual ~Operator() = default;

  /**
   * @brief Open the operator (prepare for execution)
   */
  virtual void open(ExecutionContext &ctx) = 0;

  /**
   * @brief Get next row
   * @return true if a row was produced, false if done
   */
  virtual bool next(ExecutionContext &ctx, ResultRow &row) = 0;

  /**
   * @brief Close the operator (cleanup)
   */
  virtual void close() = 0;

  /**
   * @brief Get output column names
   */
  virtual std::vector<std::string> column_names() const = 0;
};

/**
 * @brief Table scan operator
 */
class TableScanOperator : public Operator {
public:
  TableScanOperator(uint32_t table_id, const std::string &table_name,
                    storage::PageManager &page_manager,
                    const planner::TableInfo *schema);

  void open(ExecutionContext &ctx) override;
  bool next(ExecutionContext &ctx, ResultRow &row) override;
  void close() override;
  std::vector<std::string> column_names() const override;

private:
  uint32_t table_id_;
  std::string table_name_;
  storage::PageManager &page_manager_;
  const planner::TableInfo *schema_;

  uint32_t current_page_{0};
  uint16_t current_slot_{0};
  storage::Page *page_{nullptr};
};

/**
 * @brief Filter operator
 */
class FilterOperator : public Operator {
public:
  FilterOperator(std::unique_ptr<Operator> child,
                 const sql::Expression *predicate);

  void open(ExecutionContext &ctx) override;
  bool next(ExecutionContext &ctx, ResultRow &row) override;
  void close() override;
  std::vector<std::string> column_names() const override;

private:
  bool evaluate_predicate(const ResultRow &row);

  std::unique_ptr<Operator> child_;
  const sql::Expression *predicate_;
};

/**
 * @brief Limit operator
 */
class LimitOperator : public Operator {
public:
  LimitOperator(std::unique_ptr<Operator> child, int64_t limit, int64_t offset);

  void open(ExecutionContext &ctx) override;
  bool next(ExecutionContext &ctx, ResultRow &row) override;
  void close() override;
  std::vector<std::string> column_names() const override;

private:
  std::unique_ptr<Operator> child_;
  int64_t limit_;
  int64_t offset_;
  int64_t skipped_{0};
  int64_t returned_{0};
};

/**
 * @brief Sort operator (in-memory)
 */
class SortOperator : public Operator {
public:
  SortOperator(std::unique_ptr<Operator> child,
               std::vector<size_t> sort_columns, std::vector<bool> ascending);

  void open(ExecutionContext &ctx) override;
  bool next(ExecutionContext &ctx, ResultRow &row) override;
  void close() override;
  std::vector<std::string> column_names() const override;

private:
  std::unique_ptr<Operator> child_;
  std::vector<size_t> sort_columns_;
  std::vector<bool> ascending_;
  std::vector<ResultRow> buffer_;
  size_t current_row_{0};
  bool materialized_{false};
};

/**
 * @brief Query executor
 */
class Executor {
public:
  /**
   * @brief Constructor
   * @param page_manager Page manager for storage access
   * @param catalog Schema catalog
   */
  Executor(storage::PageManager &page_manager, planner::Catalog &catalog);

  /**
   * @brief Execute a query plan
   * @param plan Query plan
   * @param ctx Execution context
   * @return Execution result
   */
  ExecutionResult execute(const planner::PlanNode &plan, ExecutionContext &ctx);

private:
  std::unique_ptr<Operator> build_operator(const planner::PlanNode &plan);

  ExecutionResult execute_select(const planner::PlanNode &plan,
                                 ExecutionContext &ctx);
  ExecutionResult execute_insert(const planner::InsertNode &node,
                                 ExecutionContext &ctx);
  ExecutionResult execute_create_table(const planner::CreateTableNode &node,
                                       ExecutionContext &ctx);
  ExecutionResult execute_drop_table(const planner::DropTableNode &node,
                                     ExecutionContext &ctx);

  storage::PageManager &page_manager_;
  planner::Catalog &catalog_;
};

} // namespace executor
} // namespace edgesql
