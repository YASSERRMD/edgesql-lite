#pragma once

/**
 * @file tokenizer.hpp
 * @brief SQL tokenizer for EdgeSQL Lite
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace edgesql {
namespace sql {

/**
 * @brief Token types
 */
enum class TokenType {
  // End of input
  END_OF_INPUT,

  // Literals
  INTEGER,
  FLOAT,
  STRING,
  IDENTIFIER,

  // Keywords
  SELECT,
  FROM,
  WHERE,
  ORDER,
  BY,
  ASC,
  DESC,
  LIMIT,
  OFFSET,
  INSERT,
  INTO,
  VALUES,
  CREATE,
  TABLE,
  DROP,
  AND,
  OR,
  NOT,
  NULL_KEYWORD,
  TRUE_KEYWORD,
  FALSE_KEYWORD,

  // Aggregate functions
  COUNT,
  SUM,
  MIN,
  MAX,
  AVG,

  // Types
  INT,
  INTEGER_TYPE,
  TEXT,
  FLOAT_TYPE,
  BOOLEAN,
  BLOB,

  // Operators and punctuation
  LPAREN,    // (
  RPAREN,    // )
  COMMA,     // ,
  SEMICOLON, // ;
  STAR,      // *
  PLUS,      // +
  MINUS,     // -
  SLASH,     // /
  PERCENT,   // %
  EQ,        // =
  NE,        // != or <>
  LT,        // <
  LE,        // <=
  GT,        // >
  GE,        // >=

  // Error
  ERROR
};

/**
 * @brief Token
 */
struct Token {
  TokenType type;
  std::string_view text;
  size_t line;
  size_t column;

  // For numeric literals
  union {
    int64_t int_value;
    double float_value;
  };

  Token() : type(TokenType::END_OF_INPUT), line(0), column(0), int_value(0) {}

  Token(TokenType t, std::string_view txt, size_t ln, size_t col)
      : type(t), text(txt), line(ln), column(col), int_value(0) {}

  bool is_keyword() const {
    return type >= TokenType::SELECT && type <= TokenType::BLOB;
  }

  bool is_operator() const {
    return type >= TokenType::LPAREN && type <= TokenType::GE;
  }

  bool is_literal() const {
    return type >= TokenType::INTEGER && type <= TokenType::STRING;
  }
};

/**
 * @brief SQL Tokenizer
 *
 * Single-pass tokenizer with minimal memory allocation.
 */
class Tokenizer {
public:
  /**
   * @brief Constructor
   * @param input SQL input string
   */
  explicit Tokenizer(std::string_view input);

  /**
   * @brief Get next token
   * @return Next token
   */
  Token next_token();

  /**
   * @brief Peek at next token without consuming
   * @return Next token
   */
  Token peek_token();

  /**
   * @brief Check if at end of input
   */
  bool at_end() const { return pos_ >= input_.size(); }

  /**
   * @brief Get current position
   */
  size_t position() const { return pos_; }

  /**
   * @brief Get current line
   */
  size_t line() const { return line_; }

  /**
   * @brief Get current column
   */
  size_t column() const { return column_; }

  /**
   * @brief Get error message (if any)
   */
  const std::string &error() const { return error_; }

private:
  void skip_whitespace();
  void skip_line_comment();
  void skip_block_comment();

  Token scan_identifier_or_keyword();
  Token scan_number();
  Token scan_string();

  char current() const;
  char peek(size_t offset = 1) const;
  void advance();

  static TokenType keyword_type(std::string_view text);

  std::string_view input_;
  size_t pos_{0};
  size_t line_{1};
  size_t column_{1};

  std::string error_;
  Token peeked_;
  bool has_peeked_{false};
};

} // namespace sql
} // namespace edgesql
