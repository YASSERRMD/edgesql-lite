#pragma once

/**
 * @file plan.hpp
 * @brief Query execution plan definitions
 */

#include "../sql/ast.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace edgesql {
namespace planner {

/**
 * @brief Plan node types
 */
enum class PlanNodeType {
  TABLE_SCAN,
  INDEX_SCAN,
  FILTER,
  PROJECT,
  SORT,
  LIMIT,
  AGGREGATE,
  INSERT,
  CREATE_TABLE,
  DROP_TABLE
};

// Forward declarations
struct PlanNode;

/**
 * @brief Column reference in plan
 */
struct PlanColumn {
  std::string name;
  std::string table;
  uint32_t index; // Column index in source
};

/**
 * @brief Table scan node
 */
struct TableScanNode {
  uint32_t table_id;
  std::string table_name;
  std::vector<uint32_t> column_indices; // Which columns to read
};

/**
 * @brief Filter node
 */
struct FilterNode {
  std::unique_ptr<PlanNode> child;
  std::unique_ptr<sql::Expression> predicate;
};

/**
 * @brief Project node
 */
struct ProjectNode {
  std::unique_ptr<PlanNode> child;
  std::vector<std::unique_ptr<sql::Expression>> expressions;
  std::vector<std::string> output_names;
};

/**
 * @brief Sort node
 */
struct SortNode {
  std::unique_ptr<PlanNode> child;
  std::vector<std::unique_ptr<sql::Expression>> sort_keys;
  std::vector<bool> ascending;
};

/**
 * @brief Limit node
 */
struct LimitNode {
  std::unique_ptr<PlanNode> child;
  int64_t limit;
  int64_t offset;
};

/**
 * @brief Aggregate type
 */
enum class AggregateType { COUNT, SUM, MIN, MAX, AVG };

/**
 * @brief Aggregate definition
 */
struct AggregateExpr {
  AggregateType type;
  std::unique_ptr<sql::Expression> arg;
  bool distinct;
  std::string output_name;
};

/**
 * @brief Aggregate node
 */
struct AggregateNode {
  std::unique_ptr<PlanNode> child;
  std::vector<AggregateExpr> aggregates;
  std::vector<std::unique_ptr<sql::Expression>> group_by;
};

/**
 * @brief Insert node
 */
struct InsertNode {
  uint32_t table_id;
  std::string table_name;
  std::vector<std::string> column_names;
  std::vector<std::vector<std::unique_ptr<sql::Expression>>> values;
};

/**
 * @brief Create table node
 */
struct CreateTableNode {
  std::string table_name;
  std::vector<sql::ColumnDef> columns;
  bool if_not_exists;
};

/**
 * @brief Drop table node
 */
struct DropTableNode {
  std::string table_name;
  bool if_exists;
};

/**
 * @brief Plan node
 */
struct PlanNode {
  PlanNodeType type;

  std::variant<TableScanNode, FilterNode, ProjectNode, SortNode, LimitNode,
               AggregateNode, InsertNode, CreateTableNode, DropTableNode>
      node;

  // Estimated cost and cardinality
  double estimated_cost{0.0};
  uint64_t estimated_rows{0};

  static std::unique_ptr<PlanNode> table_scan(uint32_t table_id,
                                              const std::string &name);
  static std::unique_ptr<PlanNode>
  filter(std::unique_ptr<PlanNode> child,
         std::unique_ptr<sql::Expression> predicate);
  static std::unique_ptr<PlanNode>
  project(std::unique_ptr<PlanNode> child,
          std::vector<std::unique_ptr<sql::Expression>> exprs,
          std::vector<std::string> names);
  static std::unique_ptr<PlanNode>
  sort(std::unique_ptr<PlanNode> child,
       std::vector<std::unique_ptr<sql::Expression>> keys,
       std::vector<bool> ascending);
  static std::unique_ptr<PlanNode> limit(std::unique_ptr<PlanNode> child,
                                         int64_t limit, int64_t offset);
  static std::unique_ptr<PlanNode> aggregate(std::unique_ptr<PlanNode> child,
                                             std::vector<AggregateExpr> aggs);
  static std::unique_ptr<PlanNode>
  insert(uint32_t table_id, const std::string &name,
         std::vector<std::string> columns,
         std::vector<std::vector<std::unique_ptr<sql::Expression>>> values);
  static std::unique_ptr<PlanNode>
  create_table(const std::string &name, std::vector<sql::ColumnDef> columns,
               bool if_not_exists);
  static std::unique_ptr<PlanNode> drop_table(const std::string &name,
                                              bool if_exists);
};

} // namespace planner
} // namespace edgesql
