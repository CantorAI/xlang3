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

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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
  Bytes,
  FString,
  KwDef,
  KwClass,
  KwReturn,
  KwIf,
  KwElif,
  KwElse,
  KwTry,
  KwExcept,
  KwCase,
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
  KwBreak,
  KwContinue,
  KwPass,
  KwDel,
  KwAssert,
  KwMatch,
  KwTrue,
  KwFalse,
  KwNone,
  KwAnd,
  KwOr,
  KwNot,
  KwIs,
  KwAsync,
  KwAwait,
  KwLambda,
  KwYield,
  LParen,
  RParen,
  LBracket,
  RBracket,
  LBrace,
  RBrace,
  Ellipsis,
  Dot,
  Comma,
  Semicolon,
  Colon,
  ColonEqual,
  At,
  Arrow,
  Assign,
  PlusAssign,
  MinusAssign,
  StarAssign,
  DoubleStarAssign,
  SlashAssign,
  DoubleSlashAssign,
  PercentAssign,
  AmpAssign,
  PipeAssign,
  CaretAssign,
  LeftShiftAssign,
  RightShiftAssign,
  Plus,
  Minus,
  Star,
  DoubleStar,
  Slash,
  DoubleSlash,
  Percent,
  Amp,
  Pipe,
  Caret,
  Tilde,
  LeftShift,
  RightShift,
  EqualEqual,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string_view text;
  uint32_t line = 1;
  uint32_t column = 1;
  bool is_triple_string = false;
};

struct LexResult {
  std::vector<Token> tokens;
  std::vector<std::unique_ptr<std::string>> owned_text;
  std::vector<std::string> errors;
};

struct ParseResult {
  ast::Module module;
  std::vector<std::string> errors;
};

struct ParseExpressionResult {
  ast::ExprPtr expression;
  std::vector<std::string> errors;
};

class Lexer {
public:
  explicit Lexer(std::string_view source);
  LexResult tokenize();

private:
  void emit(TokenKind kind, std::string_view text, uint32_t line, uint32_t column, bool is_triple_string = false);
  void emit_owned(TokenKind kind, std::string text, uint32_t line, uint32_t column, bool is_triple_string = false);
  void tokenize_line(std::string_view line_text, uint32_t line_no, uint32_t indent);

  std::string_view source_;
  std::vector<Token> tokens_;
  std::vector<std::unique_ptr<std::string>> owned_text_;
  std::vector<std::string> errors_;
  std::vector<uint32_t> indent_stack_{0};
};

class Parser {
public:
  explicit Parser(LexResult lex);
  ParseResult parse_module();
  ParseExpressionResult parse_expression_module();

private:
  ast::StmtPtr parse_statement();
  ast::StmtPtr parse_statement_impl();
  ast::StmtPtr parse_decorated_statement();
  ast::StmtPtr parse_if_statement();
  ast::StmtPtr parse_try_statement();
  ast::StmtPtr parse_with_statement();
  ast::StmtPtr parse_match_statement();
  ast::StmtPtr parse_simple_statement();
  ast::StmtPtr parse_raw_block_statement();
  std::vector<ast::StmtPtr> parse_suite_after_colon(const std::string& context);
  std::vector<ast::StmtPtr> parse_block();
  bool parse_dotted_name(std::string& out, const std::string& message, bool allow_leading_dots = false);
  bool consume_optional_type_params();
  bool is_simple_statement_end() const;
  ast::ExprPtr parse_with_manager_expr();
  ast::ExprPtr parse_for_target();
  ast::ExprPtr parse_expression();
  ast::ExprPtr parse_named_expression();
  ast::ExprPtr parse_tuple();
  ast::ExprPtr parse_conditional();
  ast::ExprPtr parse_or();
  ast::ExprPtr parse_and();
  ast::ExprPtr parse_not();
  ast::ExprPtr parse_await();
  ast::ExprPtr parse_lambda();
  ast::ExprPtr parse_compare();
  ast::ExprPtr parse_bit_or();
  ast::ExprPtr parse_bit_xor();
  ast::ExprPtr parse_bit_and();
  ast::ExprPtr parse_shift();
  ast::ExprPtr parse_term();
  ast::ExprPtr parse_factor();
  ast::ExprPtr parse_power();
  ast::ExprPtr parse_unary();
  ast::ExprPtr parse_call();
  ast::ExprPtr parse_primary();
  ast::ExprPtr parse_comprehension_target(std::string& first_name);
  std::vector<ast::CompClause> parse_extra_comp_clauses();
  ast::ExprPtr finish_generator_expression(ast::ExprPtr first);
  bool parse_function_signature(
      std::vector<std::string>& params,
      std::vector<ast::FunctionDef::Param>& signature,
      ast::ExprPtr& return_annotation);
  ast::ExprPtr parse_optional_annotation();

  bool match(TokenKind kind);
  bool check(TokenKind kind) const;
  const Token& peek() const;
  const Token& previous() const;
  const Token& advance();
  bool consume(TokenKind kind, const std::string& message);
  void consume_simple_statement_end();
  void skip_newlines();
  void error_here(const std::string& message);

  std::vector<Token> tokens_;
  std::vector<std::unique_ptr<std::string>> owned_text_;
  size_t current_ = 0;
  std::vector<std::string> errors_;
};

ParseResult parse_source(const std::string& source);
ParseExpressionResult parse_expression_source(const std::string& source);

} // namespace xlang3
