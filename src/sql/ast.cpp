/**
 * @file ast.cpp
 * @brief AST helper implementations
 */

#include "ast.hpp"

namespace edgesql {
namespace sql {

std::unique_ptr<Expression> Expression::star() {
  auto expr = std::make_unique<Expression>();
  expr->type = ExprType::STAR;
  expr->value = std::monostate{};
  return expr;
}

std::unique_ptr<Expression> Expression::literal(const Literal &lit) {
  auto expr = std::make_unique<Expression>();
  expr->type = ExprType::LITERAL;
  expr->value = lit;
  return expr;
}

std::unique_ptr<Expression> Expression::column(const std::string &name) {
  auto expr = std::make_unique<Expression>();
  expr->type = ExprType::COLUMN_REF;
  ColumnRef ref;
  ref.column_name = name;
  expr->value = ref;
  return expr;
}

std::unique_ptr<Expression> Expression::column(const std::string &table,
                                               const std::string &name) {
  auto expr = std::make_unique<Expression>();
  expr->type = ExprType::COLUMN_REF;
  ColumnRef ref;
  ref.table_name = table;
  ref.column_name = name;
  expr->value = ref;
  return expr;
}

std::unique_ptr<Expression>
Expression::binary(BinaryOp op, std::unique_ptr<Expression> left,
                   std::unique_ptr<Expression> right) {
  auto expr = std::make_unique<Expression>();
  expr->type = ExprType::BINARY_OP;
  auto bin = std::make_unique<BinaryExpr>();
  bin->op = op;
  bin->left = std::move(left);
  bin->right = std::move(right);
  expr->value = std::move(bin);
  return expr;
}

std::unique_ptr<Expression>
Expression::unary(UnaryOp op, std::unique_ptr<Expression> operand) {
  auto expr = std::make_unique<Expression>();
  expr->type = ExprType::UNARY_OP;
  auto un = std::make_unique<UnaryExpr>();
  un->op = op;
  un->operand = std::move(operand);
  expr->value = std::move(un);
  return expr;
}

std::unique_ptr<Expression>
Expression::function(const std::string &name,
                     std::vector<std::unique_ptr<Expression>> args,
                     bool distinct) {
  auto expr = std::make_unique<Expression>();
  expr->type = ExprType::FUNCTION_CALL;
  auto fn = std::make_unique<FunctionCall>();
  fn->name = name;
  fn->args = std::move(args);
  fn->distinct = distinct;
  expr->value = std::move(fn);
  return expr;
}

Statement Statement::select(std::unique_ptr<SelectStmt> s) {
  Statement stmt;
  stmt.type = StmtType::SELECT;
  stmt.stmt = std::move(s);
  return stmt;
}

Statement Statement::insert(std::unique_ptr<InsertStmt> s) {
  Statement stmt;
  stmt.type = StmtType::INSERT;
  stmt.stmt = std::move(s);
  return stmt;
}

Statement Statement::create_table(std::unique_ptr<CreateTableStmt> s) {
  Statement stmt;
  stmt.type = StmtType::CREATE_TABLE;
  stmt.stmt = std::move(s);
  return stmt;
}

Statement Statement::drop_table(std::unique_ptr<DropTableStmt> s) {
  Statement stmt;
  stmt.type = StmtType::DROP_TABLE;
  stmt.stmt = std::move(s);
  return stmt;
}

} // namespace sql
} // namespace edgesql
