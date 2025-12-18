/**
 * @file tokenizer.cpp
 * @brief SQL tokenizer implementation
 */

#include "tokenizer.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace edgesql {
namespace sql {

namespace {

// Keyword lookup table
const std::unordered_map<std::string, TokenType> keywords = {
    {"SELECT", TokenType::SELECT},
    {"FROM", TokenType::FROM},
    {"WHERE", TokenType::WHERE},
    {"ORDER", TokenType::ORDER},
    {"BY", TokenType::BY},
    {"ASC", TokenType::ASC},
    {"DESC", TokenType::DESC},
    {"LIMIT", TokenType::LIMIT},
    {"OFFSET", TokenType::OFFSET},
    {"INSERT", TokenType::INSERT},
    {"INTO", TokenType::INTO},
    {"VALUES", TokenType::VALUES},
    {"CREATE", TokenType::CREATE},
    {"TABLE", TokenType::TABLE},
    {"DROP", TokenType::DROP},
    {"AND", TokenType::AND},
    {"OR", TokenType::OR},
    {"NOT", TokenType::NOT},
    {"NULL", TokenType::NULL_KEYWORD},
    {"TRUE", TokenType::TRUE_KEYWORD},
    {"FALSE", TokenType::FALSE_KEYWORD},
    {"COUNT", TokenType::COUNT},
    {"SUM", TokenType::SUM},
    {"MIN", TokenType::MIN},
    {"MAX", TokenType::MAX},
    {"AVG", TokenType::AVG},
    {"INT", TokenType::INT},
    {"INTEGER", TokenType::INTEGER_TYPE},
    {"TEXT", TokenType::TEXT},
    {"FLOAT", TokenType::FLOAT_TYPE},
    {"BOOLEAN", TokenType::BOOLEAN},
    {"BOOL", TokenType::BOOLEAN},
    {"BLOB", TokenType::BLOB}};

std::string to_upper(std::string_view sv) {
  std::string result;
  result.reserve(sv.size());
  for (char c : sv) {
    result.push_back(
        static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return result;
}

} // anonymous namespace

Tokenizer::Tokenizer(std::string_view input) : input_(input) {}

Token Tokenizer::next_token() {
  if (has_peeked_) {
    has_peeked_ = false;
    return peeked_;
  }

  skip_whitespace();

  if (at_end()) {
    return Token(TokenType::END_OF_INPUT, "", line_, column_);
  }

  size_t start_line = line_;
  size_t start_column = column_;
  size_t start_pos = pos_;

  char c = current();

  // Single character tokens
  switch (c) {
  case '(':
    advance();
    return Token(TokenType::LPAREN, input_.substr(start_pos, 1), start_line,
                 start_column);
  case ')':
    advance();
    return Token(TokenType::RPAREN, input_.substr(start_pos, 1), start_line,
                 start_column);
  case ',':
    advance();
    return Token(TokenType::COMMA, input_.substr(start_pos, 1), start_line,
                 start_column);
  case ';':
    advance();
    return Token(TokenType::SEMICOLON, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '*':
    advance();
    return Token(TokenType::STAR, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '+':
    advance();
    return Token(TokenType::PLUS, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '-':
    // Could be minus or line comment
    if (peek() == '-') {
      skip_line_comment();
      return next_token();
    }
    advance();
    return Token(TokenType::MINUS, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '/':
    // Could be slash or block comment
    if (peek() == '*') {
      skip_block_comment();
      return next_token();
    }
    advance();
    return Token(TokenType::SLASH, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '%':
    advance();
    return Token(TokenType::PERCENT, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '=':
    advance();
    return Token(TokenType::EQ, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '<':
    advance();
    if (current() == '=') {
      advance();
      return Token(TokenType::LE, input_.substr(start_pos, 2), start_line,
                   start_column);
    }
    if (current() == '>') {
      advance();
      return Token(TokenType::NE, input_.substr(start_pos, 2), start_line,
                   start_column);
    }
    return Token(TokenType::LT, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '>':
    advance();
    if (current() == '=') {
      advance();
      return Token(TokenType::GE, input_.substr(start_pos, 2), start_line,
                   start_column);
    }
    return Token(TokenType::GT, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '!':
    advance();
    if (current() == '=') {
      advance();
      return Token(TokenType::NE, input_.substr(start_pos, 2), start_line,
                   start_column);
    }
    error_ = "Expected '=' after '!'";
    return Token(TokenType::ERROR, input_.substr(start_pos, 1), start_line,
                 start_column);
  case '\'':
  case '"':
    return scan_string();
  default:
    break;
  }

  // Numbers
  if (std::isdigit(static_cast<unsigned char>(c))) {
    return scan_number();
  }

  // Identifiers and keywords
  if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
    return scan_identifier_or_keyword();
  }

  // Unknown character
  error_ = "Unexpected character: ";
  error_ += c;
  advance();
  return Token(TokenType::ERROR, input_.substr(start_pos, 1), start_line,
               start_column);
}

Token Tokenizer::peek_token() {
  if (!has_peeked_) {
    peeked_ = next_token();
    has_peeked_ = true;
  }
  return peeked_;
}

void Tokenizer::skip_whitespace() {
  while (!at_end()) {
    char c = current();
    if (c == ' ' || c == '\t' || c == '\r') {
      advance();
    } else if (c == '\n') {
      advance();
      line_++;
      column_ = 1;
    } else {
      break;
    }
  }
}

void Tokenizer::skip_line_comment() {
  // Skip --
  advance();
  advance();

  while (!at_end() && current() != '\n') {
    advance();
  }
}

void Tokenizer::skip_block_comment() {
  // Skip /*
  advance();
  advance();

  while (!at_end()) {
    if (current() == '*' && peek() == '/') {
      advance();
      advance();
      return;
    }
    if (current() == '\n') {
      line_++;
      column_ = 0;
    }
    advance();
  }

  error_ = "Unterminated block comment";
}

Token Tokenizer::scan_identifier_or_keyword() {
  size_t start_pos = pos_;
  size_t start_line = line_;
  size_t start_column = column_;

  while (!at_end()) {
    char c = current();
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      advance();
    } else {
      break;
    }
  }

  std::string_view text = input_.substr(start_pos, pos_ - start_pos);
  TokenType type = keyword_type(text);

  return Token(type, text, start_line, start_column);
}

Token Tokenizer::scan_number() {
  size_t start_pos = pos_;
  size_t start_line = line_;
  size_t start_column = column_;

  bool has_dot = false;

  while (!at_end()) {
    char c = current();
    if (std::isdigit(static_cast<unsigned char>(c))) {
      advance();
    } else if (c == '.' && !has_dot) {
      has_dot = true;
      advance();
    } else {
      break;
    }
  }

  std::string_view text = input_.substr(start_pos, pos_ - start_pos);
  Token token(has_dot ? TokenType::FLOAT : TokenType::INTEGER, text, start_line,
              start_column);

  // Parse the value
  std::string text_str(text);
  if (has_dot) {
    token.float_value = std::stod(text_str);
  } else {
    token.int_value = std::stoll(text_str);
  }

  return token;
}

Token Tokenizer::scan_string() {
  char quote = current();
  size_t start_pos = pos_;
  size_t start_line = line_;
  size_t start_column = column_;

  advance(); // Skip opening quote

  while (!at_end() && current() != quote) {
    if (current() == '\n') {
      error_ = "Unterminated string literal";
      return Token(TokenType::ERROR, input_.substr(start_pos, pos_ - start_pos),
                   start_line, start_column);
    }
    if (current() == '\\') {
      advance(); // Skip escape character
      if (!at_end()) {
        advance();
      }
    } else {
      advance();
    }
  }

  if (at_end()) {
    error_ = "Unterminated string literal";
    return Token(TokenType::ERROR, input_.substr(start_pos, pos_ - start_pos),
                 start_line, start_column);
  }

  advance(); // Skip closing quote

  // Return the string including quotes
  return Token(TokenType::STRING, input_.substr(start_pos, pos_ - start_pos),
               start_line, start_column);
}

char Tokenizer::current() const {
  if (at_end())
    return '\0';
  return input_[pos_];
}

char Tokenizer::peek(size_t offset) const {
  if (pos_ + offset >= input_.size())
    return '\0';
  return input_[pos_ + offset];
}

void Tokenizer::advance() {
  if (!at_end()) {
    pos_++;
    column_++;
  }
}

TokenType Tokenizer::keyword_type(std::string_view text) {
  std::string upper = to_upper(text);
  auto it = keywords.find(upper);
  if (it != keywords.end()) {
    return it->second;
  }
  return TokenType::IDENTIFIER;
}

} // namespace sql
} // namespace edgesql
