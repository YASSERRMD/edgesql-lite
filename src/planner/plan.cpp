/**
 * @file plan.cpp
 * @brief Query plan node implementations
 */

#include "plan.hpp"

namespace edgesql {
namespace planner {

std::unique_ptr<PlanNode> PlanNode::table_scan(uint32_t table_id,
                                               const std::string &name) {
  auto node = std::make_unique<PlanNode>();
  node->type = PlanNodeType::TABLE_SCAN;
  node->node = TableScanNode{table_id, name, {}};
  return node;
}

std::unique_ptr<PlanNode>
PlanNode::filter(std::unique_ptr<PlanNode> child,
                 std::unique_ptr<sql::Expression> predicate) {
  auto node = std::make_unique<PlanNode>();
  node->type = PlanNodeType::FILTER;
  FilterNode f;
  f.child = std::move(child);
  f.predicate = std::move(predicate);
  node->node = std::move(f);
  return node;
}

std::unique_ptr<PlanNode>
PlanNode::project(std::unique_ptr<PlanNode> child,
                  std::vector<std::unique_ptr<sql::Expression>> exprs,
                  std::vector<std::string> names) {
  auto node = std::make_unique<PlanNode>();
  node->type = PlanNodeType::PROJECT;
  ProjectNode p;
  p.child = std::move(child);
  p.expressions = std::move(exprs);
  p.output_names = std::move(names);
  node->node = std::move(p);
  return node;
}

std::unique_ptr<PlanNode>
PlanNode::sort(std::unique_ptr<PlanNode> child,
               std::vector<std::unique_ptr<sql::Expression>> keys,
               std::vector<bool> ascending) {
  auto node = std::make_unique<PlanNode>();
  node->type = PlanNodeType::SORT;
  SortNode s;
  s.child = std::move(child);
  s.sort_keys = std::move(keys);
  s.ascending = std::move(ascending);
  node->node = std::move(s);
  return node;
}

std::unique_ptr<PlanNode> PlanNode::limit(std::unique_ptr<PlanNode> child,
                                          int64_t limit_val,
                                          int64_t offset_val) {
  auto node = std::make_unique<PlanNode>();
  node->type = PlanNodeType::LIMIT;
  LimitNode l;
  l.child = std::move(child);
  l.limit = limit_val;
  l.offset = offset_val;
  node->node = std::move(l);
  return node;
}

std::unique_ptr<PlanNode> PlanNode::aggregate(std::unique_ptr<PlanNode> child,
                                              std::vector<AggregateExpr> aggs) {
  auto node = std::make_unique<PlanNode>();
  node->type = PlanNodeType::AGGREGATE;
  AggregateNode a;
  a.child = std::move(child);
  a.aggregates = std::move(aggs);
  node->node = std::move(a);
  return node;
}

std::unique_ptr<PlanNode> PlanNode::insert(
    uint32_t table_id, const std::string &name,
    std::vector<std::string> columns,
    std::vector<std::vector<std::unique_ptr<sql::Expression>>> values) {
  auto node = std::make_unique<PlanNode>();
  node->type = PlanNodeType::INSERT;
  InsertNode i;
  i.table_id = table_id;
  i.table_name = name;
  i.column_names = std::move(columns);
  i.values = std::move(values);
  node->node = std::move(i);
  return node;
}

std::unique_ptr<PlanNode>
PlanNode::create_table(const std::string &name,
                       std::vector<sql::ColumnDef> columns,
                       bool if_not_exists) {
  auto node = std::make_unique<PlanNode>();
  node->type = PlanNodeType::CREATE_TABLE;
  CreateTableNode c;
  c.table_name = name;
  c.columns = std::move(columns);
  c.if_not_exists = if_not_exists;
  node->node = std::move(c);
  return node;
}

std::unique_ptr<PlanNode> PlanNode::drop_table(const std::string &name,
                                               bool if_exists) {
  auto node = std::make_unique<PlanNode>();
  node->type = PlanNodeType::DROP_TABLE;
  DropTableNode d;
  d.table_name = name;
  d.if_exists = if_exists;
  node->node = std::move(d);
  return node;
}

} // namespace planner
} // namespace edgesql
