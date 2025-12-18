#pragma once

/**
 * @file parser.hpp
 * @brief SQL Parser for EdgeSQL Lite
 */

#include "ast.hpp"
#include "tokenizer.hpp"
#include <optional>
#include <string>

namespace edgesql {
namespace sql {

/**
 * @brief Parse error information
 */
struct ParseError {
  std::string message;
  size_t line;
  size_t column;

  std::string to_string() const {
    return "Parse error at line " + std::to_string(line) + ", column " +
           std::to_string(column) + ": " + message;
  }
};

/**
 * @brief SQL Parser
 *
 * Recursive descent parser for SQL statements.
 */
class Parser {
public:
  /**
   * @brief Constructor
   * @param input SQL input string
   */
  explicit Parser(std::string_view input);

  /**
   * @brief Parse a single statement
   * @return Parsed statement, or nullopt on error
   */
  std::optional<Statement> parse();

  /**
   * @brief Get the last parse error
   */
  const ParseError &error() const { return error_; }

  /**
   * @brief Check if there was a parse error
   */
  bool has_error() const { return has_error_; }

private:
  // Statement parsers
  std::unique_ptr<SelectStmt> parse_select();
  std::unique_ptr<InsertStmt> parse_insert();
  std::unique_ptr<CreateTableStmt> parse_create_table();
  std::unique_ptr<DropTableStmt> parse_drop_table();

  // Expression parsers (precedence climbing)
  std::unique_ptr<Expression> parse_expression();
  std::unique_ptr<Expression> parse_or_expr();
  std::unique_ptr<Expression> parse_and_expr();
  std::unique_ptr<Expression> parse_comparison();
  std::unique_ptr<Expression> parse_additive();
  std::unique_ptr<Expression> parse_multiplicative();
  std::unique_ptr<Expression> parse_unary();
  std::unique_ptr<Expression> parse_primary();
  std::unique_ptr<Expression> parse_function_call(const std::string &name);

  // Helper parsers
  std::vector<std::unique_ptr<Expression>> parse_select_columns();
  std::vector<OrderByItem> parse_order_by();
  std::vector<ColumnDef> parse_column_defs();
  ColumnDef parse_column_def();

  // Token helpers
  Token current_token();
  Token advance();
  bool check(TokenType type);
  bool match(TokenType type);
  Token expect(TokenType type, const std::string &message);

  // Error handling
  void set_error(const std::string &message);
  void set_error(const std::string &message, const Token &token);

  Tokenizer tokenizer_;
  Token current_;
  ParseError error_;
  bool has_error_{false};
};

} // namespace sql
} // namespace edgesql
