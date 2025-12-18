/**
 * @file parser.cpp
 * @brief SQL Parser implementation
 */

#include "parser.hpp"
#include <algorithm>
#include <cctype>

namespace edgesql {
namespace sql {

Parser::Parser(std::string_view input) : tokenizer_(input) {
  current_ = tokenizer_.next_token();
}

std::optional<Statement> Parser::parse() {
  has_error_ = false;

  if (check(TokenType::END_OF_INPUT)) {
    set_error("Empty statement");
    return std::nullopt;
  }

  Statement result;

  if (match(TokenType::SELECT)) {
    auto stmt = parse_select();
    if (!stmt)
      return std::nullopt;
    result = Statement::select(std::move(stmt));
  } else if (match(TokenType::INSERT)) {
    auto stmt = parse_insert();
    if (!stmt)
      return std::nullopt;
    result = Statement::insert(std::move(stmt));
  } else if (match(TokenType::CREATE)) {
    if (!match(TokenType::TABLE)) {
      set_error("Expected TABLE after CREATE");
      return std::nullopt;
    }
    auto stmt = parse_create_table();
    if (!stmt)
      return std::nullopt;
    result = Statement::create_table(std::move(stmt));
  } else if (match(TokenType::DROP)) {
    if (!match(TokenType::TABLE)) {
      set_error("Expected TABLE after DROP");
      return std::nullopt;
    }
    auto stmt = parse_drop_table();
    if (!stmt)
      return std::nullopt;
    result = Statement::drop_table(std::move(stmt));
  } else {
    set_error("Expected SELECT, INSERT, CREATE, or DROP");
    return std::nullopt;
  }

  // Optional semicolon
  match(TokenType::SEMICOLON);

  return result;
}

std::unique_ptr<SelectStmt> Parser::parse_select() {
  auto stmt = std::make_unique<SelectStmt>();

  // Parse columns
  stmt->columns = parse_select_columns();
  if (has_error_)
    return nullptr;

  // FROM clause
  if (!match(TokenType::FROM)) {
    set_error("Expected FROM");
    return nullptr;
  }

  Token table = expect(TokenType::IDENTIFIER, "Expected table name");
  if (has_error_)
    return nullptr;
  stmt->table_name = std::string(table.text);

  // Optional WHERE clause
  if (match(TokenType::WHERE)) {
    stmt->where_clause = parse_expression();
    if (has_error_)
      return nullptr;
  }

  // Optional ORDER BY
  if (match(TokenType::ORDER)) {
    if (!match(TokenType::BY)) {
      set_error("Expected BY after ORDER");
      return nullptr;
    }
    stmt->order_by = parse_order_by();
    if (has_error_)
      return nullptr;
  }

  // Optional LIMIT
  if (match(TokenType::LIMIT)) {
    Token limit = expect(TokenType::INTEGER, "Expected integer after LIMIT");
    if (has_error_)
      return nullptr;
    stmt->limit = limit.int_value;

    // Optional OFFSET
    if (match(TokenType::OFFSET)) {
      Token offset =
          expect(TokenType::INTEGER, "Expected integer after OFFSET");
      if (has_error_)
        return nullptr;
      stmt->offset = offset.int_value;
    }
  }

  return stmt;
}

std::unique_ptr<InsertStmt> Parser::parse_insert() {
  auto stmt = std::make_unique<InsertStmt>();

  if (!match(TokenType::INTO)) {
    set_error("Expected INTO after INSERT");
    return nullptr;
  }

  Token table = expect(TokenType::IDENTIFIER, "Expected table name");
  if (has_error_)
    return nullptr;
  stmt->table_name = std::string(table.text);

  // Optional column list
  if (match(TokenType::LPAREN)) {
    do {
      Token col = expect(TokenType::IDENTIFIER, "Expected column name");
      if (has_error_)
        return nullptr;
      stmt->column_names.push_back(std::string(col.text));
    } while (match(TokenType::COMMA));

    if (!match(TokenType::RPAREN)) {
      set_error("Expected ')' after column list");
      return nullptr;
    }
  }

  if (!match(TokenType::VALUES)) {
    set_error("Expected VALUES");
    return nullptr;
  }

  // Parse value rows
  do {
    if (!match(TokenType::LPAREN)) {
      set_error("Expected '(' before values");
      return nullptr;
    }

    std::vector<std::unique_ptr<Expression>> row;
    do {
      auto expr = parse_expression();
      if (has_error_)
        return nullptr;
      row.push_back(std::move(expr));
    } while (match(TokenType::COMMA));

    if (!match(TokenType::RPAREN)) {
      set_error("Expected ')' after values");
      return nullptr;
    }

    stmt->values.push_back(std::move(row));
  } while (match(TokenType::COMMA));

  return stmt;
}

std::unique_ptr<CreateTableStmt> Parser::parse_create_table() {
  auto stmt = std::make_unique<CreateTableStmt>();

  // IF NOT EXISTS
  if (current_.type == TokenType::IDENTIFIER &&
      std::string(current_.text) == "IF") {
    advance();
    if (!(current_.type == TokenType::NOT)) {
      set_error("Expected NOT after IF");
      return nullptr;
    }
    advance();
    if (!(current_.type == TokenType::IDENTIFIER &&
          std::string(current_.text) == "EXISTS")) {
      set_error("Expected EXISTS after IF NOT");
      return nullptr;
    }
    advance();
    stmt->if_not_exists = true;
  }

  Token table = expect(TokenType::IDENTIFIER, "Expected table name");
  if (has_error_)
    return nullptr;
  stmt->table_name = std::string(table.text);

  if (!match(TokenType::LPAREN)) {
    set_error("Expected '(' after table name");
    return nullptr;
  }

  stmt->columns = parse_column_defs();
  if (has_error_)
    return nullptr;

  if (!match(TokenType::RPAREN)) {
    set_error("Expected ')' after column definitions");
    return nullptr;
  }

  return stmt;
}

std::unique_ptr<DropTableStmt> Parser::parse_drop_table() {
  auto stmt = std::make_unique<DropTableStmt>();

  // IF EXISTS
  if (current_.type == TokenType::IDENTIFIER &&
      std::string(current_.text) == "IF") {
    advance();
    if (!(current_.type == TokenType::IDENTIFIER &&
          std::string(current_.text) == "EXISTS")) {
      set_error("Expected EXISTS after IF");
      return nullptr;
    }
    advance();
    stmt->if_exists = true;
  }

  Token table = expect(TokenType::IDENTIFIER, "Expected table name");
  if (has_error_)
    return nullptr;
  stmt->table_name = std::string(table.text);

  return stmt;
}

std::vector<std::unique_ptr<Expression>> Parser::parse_select_columns() {
  std::vector<std::unique_ptr<Expression>> columns;

  do {
    if (match(TokenType::STAR)) {
      columns.push_back(Expression::star());
    } else {
      auto expr = parse_expression();
      if (has_error_)
        return {};

      // Optional alias
      if (current_.type == TokenType::IDENTIFIER &&
          std::string(current_.text) == "AS") {
        advance();
        Token alias = expect(TokenType::IDENTIFIER, "Expected alias name");
        if (has_error_)
          return {};
        expr->alias = std::string(alias.text);
      }

      columns.push_back(std::move(expr));
    }
  } while (match(TokenType::COMMA));

  return columns;
}

std::vector<OrderByItem> Parser::parse_order_by() {
  std::vector<OrderByItem> items;

  do {
    OrderByItem item;
    item.expr = parse_expression();
    if (has_error_)
      return {};

    if (match(TokenType::ASC)) {
      item.ascending = true;
    } else if (match(TokenType::DESC)) {
      item.ascending = false;
    }

    items.push_back(std::move(item));
  } while (match(TokenType::COMMA));

  return items;
}

std::vector<ColumnDef> Parser::parse_column_defs() {
  std::vector<ColumnDef> columns;

  do {
    ColumnDef col = parse_column_def();
    if (has_error_)
      return {};
    columns.push_back(std::move(col));
  } while (match(TokenType::COMMA));

  return columns;
}

ColumnDef Parser::parse_column_def() {
  ColumnDef col;

  Token name = expect(TokenType::IDENTIFIER, "Expected column name");
  if (has_error_)
    return col;
  col.name = std::string(name.text);

  // Type
  if (check(TokenType::INT) || check(TokenType::INTEGER_TYPE)) {
    advance();
    col.type = "INTEGER";
  } else if (check(TokenType::TEXT)) {
    advance();
    col.type = "TEXT";
  } else if (check(TokenType::FLOAT_TYPE)) {
    advance();
    col.type = "FLOAT";
  } else if (check(TokenType::BOOLEAN)) {
    advance();
    col.type = "BOOLEAN";
  } else if (check(TokenType::BLOB)) {
    advance();
    col.type = "BLOB";
  } else if (check(TokenType::IDENTIFIER)) {
    // Custom type name
    col.type = std::string(current_.text);
    advance();
  } else {
    set_error("Expected column type");
    return col;
  }

  // Optional constraints
  while (!check(TokenType::COMMA) && !check(TokenType::RPAREN) &&
         !check(TokenType::END_OF_INPUT)) {
    if (match(TokenType::NOT)) {
      if (!match(TokenType::NULL_KEYWORD)) {
        set_error("Expected NULL after NOT");
        return col;
      }
      col.not_null = true;
    } else if (current_.type == TokenType::IDENTIFIER &&
               std::string(current_.text) == "PRIMARY") {
      advance();
      if (current_.type == TokenType::IDENTIFIER &&
          std::string(current_.text) == "KEY") {
        advance();
      }
      col.primary_key = true;
    } else if (current_.type == TokenType::IDENTIFIER &&
               std::string(current_.text) == "DEFAULT") {
      advance();
      col.default_value = parse_primary();
      if (has_error_)
        return col;
    } else {
      break;
    }
  }

  return col;
}

// Expression parsing with precedence

std::unique_ptr<Expression> Parser::parse_expression() {
  return parse_or_expr();
}

std::unique_ptr<Expression> Parser::parse_or_expr() {
  auto left = parse_and_expr();
  if (has_error_)
    return nullptr;

  while (match(TokenType::OR)) {
    auto right = parse_and_expr();
    if (has_error_)
      return nullptr;
    left = Expression::binary(BinaryOp::OR, std::move(left), std::move(right));
  }

  return left;
}

std::unique_ptr<Expression> Parser::parse_and_expr() {
  auto left = parse_comparison();
  if (has_error_)
    return nullptr;

  while (match(TokenType::AND)) {
    auto right = parse_comparison();
    if (has_error_)
      return nullptr;
    left = Expression::binary(BinaryOp::AND, std::move(left), std::move(right));
  }

  return left;
}

std::unique_ptr<Expression> Parser::parse_comparison() {
  auto left = parse_additive();
  if (has_error_)
    return nullptr;

  BinaryOp op;
  bool has_op = false;

  if (match(TokenType::EQ)) {
    op = BinaryOp::EQ;
    has_op = true;
  } else if (match(TokenType::NE)) {
    op = BinaryOp::NE;
    has_op = true;
  } else if (match(TokenType::LT)) {
    op = BinaryOp::LT;
    has_op = true;
  } else if (match(TokenType::LE)) {
    op = BinaryOp::LE;
    has_op = true;
  } else if (match(TokenType::GT)) {
    op = BinaryOp::GT;
    has_op = true;
  } else if (match(TokenType::GE)) {
    op = BinaryOp::GE;
    has_op = true;
  }

  if (has_op) {
    auto right = parse_additive();
    if (has_error_)
      return nullptr;
    return Expression::binary(op, std::move(left), std::move(right));
  }

  return left;
}

std::unique_ptr<Expression> Parser::parse_additive() {
  auto left = parse_multiplicative();
  if (has_error_)
    return nullptr;

  while (true) {
    BinaryOp op;
    if (match(TokenType::PLUS)) {
      op = BinaryOp::ADD;
    } else if (match(TokenType::MINUS)) {
      op = BinaryOp::SUB;
    } else
      break;

    auto right = parse_multiplicative();
    if (has_error_)
      return nullptr;
    left = Expression::binary(op, std::move(left), std::move(right));
  }

  return left;
}

std::unique_ptr<Expression> Parser::parse_multiplicative() {
  auto left = parse_unary();
  if (has_error_)
    return nullptr;

  while (true) {
    BinaryOp op;
    if (match(TokenType::STAR)) {
      op = BinaryOp::MUL;
    } else if (match(TokenType::SLASH)) {
      op = BinaryOp::DIV;
    } else if (match(TokenType::PERCENT)) {
      op = BinaryOp::MOD;
    } else
      break;

    auto right = parse_unary();
    if (has_error_)
      return nullptr;
    left = Expression::binary(op, std::move(left), std::move(right));
  }

  return left;
}

std::unique_ptr<Expression> Parser::parse_unary() {
  if (match(TokenType::NOT)) {
    auto operand = parse_unary();
    if (has_error_)
      return nullptr;
    return Expression::unary(UnaryOp::NOT, std::move(operand));
  }

  if (match(TokenType::MINUS)) {
    auto operand = parse_unary();
    if (has_error_)
      return nullptr;
    return Expression::unary(UnaryOp::MINUS, std::move(operand));
  }

  return parse_primary();
}

std::unique_ptr<Expression> Parser::parse_primary() {
  // Parenthesized expression
  if (match(TokenType::LPAREN)) {
    auto expr = parse_expression();
    if (has_error_)
      return nullptr;
    if (!match(TokenType::RPAREN)) {
      set_error("Expected ')'");
      return nullptr;
    }
    return expr;
  }

  // Literals
  if (check(TokenType::INTEGER)) {
    Token tok = current_;
    advance();
    return Expression::literal(Literal::integer(tok.int_value));
  }

  if (check(TokenType::FLOAT)) {
    Token tok = current_;
    advance();
    return Expression::literal(Literal::floating(tok.float_value));
  }

  if (check(TokenType::STRING)) {
    Token tok = current_;
    advance();
    // Remove quotes
    std::string value(tok.text);
    if (value.size() >= 2) {
      value = value.substr(1, value.size() - 2);
    }
    return Expression::literal(Literal::string(value));
  }

  if (match(TokenType::NULL_KEYWORD)) {
    return Expression::literal(Literal::null());
  }

  if (match(TokenType::TRUE_KEYWORD)) {
    return Expression::literal(Literal::boolean(true));
  }

  if (match(TokenType::FALSE_KEYWORD)) {
    return Expression::literal(Literal::boolean(false));
  }

  // Aggregate functions
  if (check(TokenType::COUNT) || check(TokenType::SUM) ||
      check(TokenType::MIN) || check(TokenType::MAX) || check(TokenType::AVG)) {
    std::string name(current_.text);
    advance();
    return parse_function_call(name);
  }

  // Identifier (column reference or function)
  if (check(TokenType::IDENTIFIER)) {
    std::string name(current_.text);
    advance();

    // Check for function call
    if (check(TokenType::LPAREN)) {
      return parse_function_call(name);
    }

    // Check for table.column
    if (match(TokenType::STAR)) {
      // Actually, this would be after a '.', but we don't have DOT token yet
      // For simplicity, just return column reference
    }

    return Expression::column(name);
  }

  set_error("Expected expression");
  return nullptr;
}

std::unique_ptr<Expression>
Parser::parse_function_call(const std::string &name) {
  if (!match(TokenType::LPAREN)) {
    set_error("Expected '(' after function name");
    return nullptr;
  }

  std::vector<std::unique_ptr<Expression>> args;
  bool distinct = false;

  // DISTINCT for aggregates
  if (current_.type == TokenType::IDENTIFIER &&
      std::string(current_.text) == "DISTINCT") {
    distinct = true;
    advance();
  }

  if (!check(TokenType::RPAREN)) {
    // Handle COUNT(*)
    if (check(TokenType::STAR)) {
      advance();
      args.push_back(Expression::star());
    } else {
      do {
        auto arg = parse_expression();
        if (has_error_)
          return nullptr;
        args.push_back(std::move(arg));
      } while (match(TokenType::COMMA));
    }
  }

  if (!match(TokenType::RPAREN)) {
    set_error("Expected ')' after function arguments");
    return nullptr;
  }

  return Expression::function(name, std::move(args), distinct);
}

// Token helpers

Token Parser::current_token() { return current_; }

Token Parser::advance() {
  Token prev = current_;
  current_ = tokenizer_.next_token();
  return prev;
}

bool Parser::check(TokenType type) { return current_.type == type; }

bool Parser::match(TokenType type) {
  if (check(type)) {
    advance();
    return true;
  }
  return false;
}

Token Parser::expect(TokenType type, const std::string &message) {
  if (check(type)) {
    return advance();
  }
  set_error(message);
  return Token();
}

void Parser::set_error(const std::string &message) {
  set_error(message, current_);
}

void Parser::set_error(const std::string &message, const Token &token) {
  has_error_ = true;
  error_.message = message;
  error_.line = token.line;
  error_.column = token.column;
}

} // namespace sql
} // namespace edgesql
