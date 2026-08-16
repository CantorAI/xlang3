/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#pragma once

#include "xlang3/ast.h"

#include <string>
#include <vector>

namespace xlang3 {

enum class TokenKind {
  End,
  Newline,
  Indent,
  Dedent,
  Identifier,
  Integer,
  Double,
  String,
  KwDef,
  KwClass,
  KwReturn,
  KwIf,
  KwElse,
  KwTry,
  KwExcept,
  KwFinally,
  KwRaise,
  KwWith,
  KwWhile,
  KwFor,
  KwIn,
  KwImport,
  KwFrom,
  KwAs,
  KwGlobal,
  KwNonlocal,
  KwTrue,
  KwFalse,
  KwNone,
  KwAnd,
  KwOr,
  KwNot,
  LParen,
  RParen,
  LBracket,
  RBracket,
  LBrace,
  RBrace,
  Dot,
  Comma,
  Colon,
  Assign,
  Plus,
  Minus,
  Star,
  Slash,
  Percent,
  EqualEqual,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;
  uint32_t line = 1;
  uint32_t column = 1;
  bool is_triple_string = false;
};

struct ParseResult {
  ast::Module module;
  std::vector<std::string> errors;
};

class Lexer {
public:
  explicit Lexer(std::string source);
  std::vector<Token> tokenize();
  const std::vector<std::string>& errors() const { return errors_; }

private:
  void emit(TokenKind kind, std::string text, uint32_t line, uint32_t column, bool is_triple_string = false);
  void tokenize_line(const std::string& line_text, uint32_t line_no, uint32_t indent);

  std::string source_;
  std::vector<Token> tokens_;
  std::vector<std::string> errors_;
  std::vector<uint32_t> indent_stack_{0};
};

class Parser {
public:
  explicit Parser(std::vector<Token> tokens);
  ParseResult parse_module();

private:
  ast::StmtPtr parse_statement();
  ast::StmtPtr parse_simple_statement();
  ast::StmtPtr parse_raw_block_statement();
  std::vector<ast::StmtPtr> parse_block();
  bool parse_dotted_name(std::string& out, const std::string& message);
  ast::ExprPtr parse_expression();
  ast::ExprPtr parse_tuple();
  ast::ExprPtr parse_or();
  ast::ExprPtr parse_and();
  ast::ExprPtr parse_not();
  ast::ExprPtr parse_compare();
  ast::ExprPtr parse_term();
  ast::ExprPtr parse_factor();
  ast::ExprPtr parse_unary();
  ast::ExprPtr parse_call();
  ast::ExprPtr parse_primary();

  bool match(TokenKind kind);
  bool check(TokenKind kind) const;
  const Token& peek() const;
  const Token& previous() const;
  const Token& advance();
  bool consume(TokenKind kind, const std::string& message);
  void skip_newlines();
  void error_here(const std::string& message);

  std::vector<Token> tokens_;
  size_t current_ = 0;
  std::vector<std::string> errors_;
};

ParseResult parse_source(const std::string& source);

} // namespace xlang3
