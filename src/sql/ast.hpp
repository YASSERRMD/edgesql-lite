#pragma once

/**
 * @file ast.hpp
 * @brief Abstract Syntax Tree for SQL statements
 */

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace edgesql {
namespace sql {

// Forward declarations
struct SelectStmt;
struct InsertStmt;
struct CreateTableStmt;
struct DropTableStmt;
struct Expression;

/**
 * @brief Statement types
 */
enum class StmtType { SELECT, INSERT, CREATE_TABLE, DROP_TABLE };

/**
 * @brief Expression types
 */
enum class ExprType {
  LITERAL,
  COLUMN_REF,
  BINARY_OP,
  UNARY_OP,
  FUNCTION_CALL,
  STAR
};

/**
 * @brief Binary operators
 */
enum class BinaryOp {
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  EQ,
  NE,
  LT,
  LE,
  GT,
  GE,
  AND,
  OR
};

/**
 * @brief Unary operators
 */
enum class UnaryOp { NOT, MINUS };

/**
 * @brief Literal value
 */
struct Literal {
  enum class Type { NULL_VAL, INTEGER, FLOAT, STRING, BOOLEAN } type;

  union {
    int64_t int_value;
    double float_value;
    bool bool_value;
  };
  std::string string_value;

  Literal() : type(Type::NULL_VAL), int_value(0) {}

  static Literal null() { return Literal(); }
  static Literal integer(int64_t v) {
    Literal l;
    l.type = Type::INTEGER;
    l.int_value = v;
    return l;
  }
  static Literal floating(double v) {
    Literal l;
    l.type = Type::FLOAT;
    l.float_value = v;
    return l;
  }
  static Literal string(const std::string &v) {
    Literal l;
    l.type = Type::STRING;
    l.string_value = v;
    return l;
  }
  static Literal boolean(bool v) {
    Literal l;
    l.type = Type::BOOLEAN;
    l.bool_value = v;
    return l;
  }
};

/**
 * @brief Column reference
 */
struct ColumnRef {
  std::string table_name; // Optional
  std::string column_name;
};

/**
 * @brief Binary operation
 */
struct BinaryExpr {
  BinaryOp op;
  std::unique_ptr<Expression> left;
  std::unique_ptr<Expression> right;
};

/**
 * @brief Unary operation
 */
struct UnaryExpr {
  UnaryOp op;
  std::unique_ptr<Expression> operand;
};

/**
 * @brief Function call (aggregates)
 */
struct FunctionCall {
  std::string name;
  std::vector<std::unique_ptr<Expression>> args;
  bool distinct{false};
};

/**
 * @brief Expression node
 */
struct Expression {
  ExprType type;
  std::variant<Literal, ColumnRef, std::unique_ptr<BinaryExpr>,
               std::unique_ptr<UnaryExpr>, std::unique_ptr<FunctionCall>,
               std::monostate // For STAR
               >
      value;

  std::string alias; // Optional alias (AS ...)

  static std::unique_ptr<Expression> star();
  static std::unique_ptr<Expression> literal(const Literal &lit);
  static std::unique_ptr<Expression> column(const std::string &name);
  static std::unique_ptr<Expression> column(const std::string &table,
                                            const std::string &name);
  static std::unique_ptr<Expression> binary(BinaryOp op,
                                            std::unique_ptr<Expression> left,
                                            std::unique_ptr<Expression> right);
  static std::unique_ptr<Expression> unary(UnaryOp op,
                                           std::unique_ptr<Expression> operand);
  static std::unique_ptr<Expression>
  function(const std::string &name,
           std::vector<std::unique_ptr<Expression>> args,
           bool distinct = false);
};

/**
 * @brief Order by specification
 */
struct OrderByItem {
  std::unique_ptr<Expression> expr;
  bool ascending{true};
};

/**
 * @brief SELECT statement
 */
struct SelectStmt {
  std::vector<std::unique_ptr<Expression>> columns;
  std::string table_name;
  std::unique_ptr<Expression> where_clause;
  std::vector<OrderByItem> order_by;
  int64_t limit{-1}; // -1 means no limit
  int64_t offset{0};
};

/**
 * @brief INSERT statement
 */
struct InsertStmt {
  std::string table_name;
  std::vector<std::string> column_names; // Optional, if empty, all columns
  std::vector<std::vector<std::unique_ptr<Expression>>> values; // Multiple rows
};

/**
 * @brief Column definition for CREATE TABLE
 */
struct ColumnDef {
  std::string name;
  std::string type; // INTEGER, TEXT, FLOAT, BOOLEAN, BLOB
  bool not_null{false};
  bool primary_key{false};
  std::unique_ptr<Expression> default_value;
};

/**
 * @brief CREATE TABLE statement
 */
struct CreateTableStmt {
  std::string table_name;
  std::vector<ColumnDef> columns;
  bool if_not_exists{false};
};

/**
 * @brief DROP TABLE statement
 */
struct DropTableStmt {
  std::string table_name;
  bool if_exists{false};
};

/**
 * @brief Statement wrapper
 */
struct Statement {
  StmtType type;
  std::variant<std::unique_ptr<SelectStmt>, std::unique_ptr<InsertStmt>,
               std::unique_ptr<CreateTableStmt>, std::unique_ptr<DropTableStmt>>
      stmt;

  static Statement select(std::unique_ptr<SelectStmt> s);
  static Statement insert(std::unique_ptr<InsertStmt> s);
  static Statement create_table(std::unique_ptr<CreateTableStmt> s);
  static Statement drop_table(std::unique_ptr<DropTableStmt> s);
};

} // namespace sql
} // namespace edgesql
