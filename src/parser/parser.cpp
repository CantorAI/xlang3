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
#include "xlang3/parser.h"

#include <cstdlib>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace xlang3 {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

ParseResult parse_source(const std::string& source) {
  Lexer lexer(source);
  auto tokens = lexer.tokenize();
  Parser parser(std::move(tokens));
  auto result = parser.parse_module();
  result.errors.insert(result.errors.begin(), lexer.errors().begin(), lexer.errors().end());
  return result;
}

ParseResult Parser::parse_module() {
  ParseResult result;
  skip_newlines();
  while (!check(TokenKind::End)) {
    auto stmt = parse_statement();
    if (stmt) {
      result.module.body.push_back(std::move(stmt));
    } else {
      advance();
    }
    skip_newlines();
  }
  result.errors = errors_;
  return result;
}

ast::StmtPtr Parser::parse_statement() {
  if (match(TokenKind::KwDef)) {
    const Token name = peek();
    if (!consume(TokenKind::Identifier, "expected function name")) return nullptr;
    consume(TokenKind::LParen, "expected '(' after function name");
    std::vector<std::string> params;
    if (!check(TokenKind::RParen)) {
      do {
        const Token param = peek();
        if (!consume(TokenKind::Identifier, "expected parameter name")) return nullptr;
        params.push_back(param.text);
      } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, "expected ')' after parameters");
    consume(TokenKind::Colon, "expected ':' after function header");
    consume(TokenKind::Newline, "expected newline after function header");
    consume(TokenKind::Indent, "expected indented function body");
    auto fn = std::make_unique<ast::FunctionDef>();
    fn->name = name.text;
    fn->params = std::move(params);
    fn->body = parse_block();
    return fn;
  }
  if (match(TokenKind::KwClass)) {
    const Token name = peek();
    if (!consume(TokenKind::Identifier, "expected class name")) return nullptr;
    consume(TokenKind::Colon, "expected ':' after class name");
    consume(TokenKind::Newline, "expected newline after class header");
    consume(TokenKind::Indent, "expected indented class body");
    auto klass = std::make_unique<ast::ClassDef>();
    klass->name = name.text;
    klass->body = parse_block();
    return klass;
  }
  if (match(TokenKind::KwIf)) {
    auto stmt = std::make_unique<ast::IfStmt>();
    stmt->condition = parse_expression();
    consume(TokenKind::Colon, "expected ':' after if condition");
    consume(TokenKind::Newline, "expected newline after if");
    consume(TokenKind::Indent, "expected indented if body");
    stmt->then_body = parse_block();
    if (match(TokenKind::KwElse)) {
      consume(TokenKind::Colon, "expected ':' after else");
      consume(TokenKind::Newline, "expected newline after else");
      consume(TokenKind::Indent, "expected indented else body");
      stmt->else_body = parse_block();
    }
    return stmt;
  }
  if (match(TokenKind::KwTry)) {
    auto stmt = std::make_unique<ast::TryExceptStmt>();
    consume(TokenKind::Colon, "expected ':' after try");
    consume(TokenKind::Newline, "expected newline after try");
    consume(TokenKind::Indent, "expected indented try body");
    stmt->try_body = parse_block();
    consume(TokenKind::KwExcept, "expected except after try body");
    consume(TokenKind::Colon, "expected ':' after except");
    consume(TokenKind::Newline, "expected newline after except");
    consume(TokenKind::Indent, "expected indented except body");
    stmt->except_body = parse_block();
    return stmt;
  }
  if (match(TokenKind::KwWhile)) {
    auto stmt = std::make_unique<ast::WhileStmt>();
    stmt->condition = parse_expression();
    consume(TokenKind::Colon, "expected ':' after while condition");
    consume(TokenKind::Newline, "expected newline after while");
    consume(TokenKind::Indent, "expected indented while body");
    stmt->body = parse_block();
    return stmt;
  }
  if (match(TokenKind::KwFor)) {
    auto stmt = std::make_unique<ast::ForStmt>();
    const Token target = peek();
    if (!consume(TokenKind::Identifier, "expected loop target after for")) return nullptr;
    stmt->target = target.text;
    consume(TokenKind::KwIn, "expected 'in' after loop target");
    stmt->iterable = parse_expression();
    consume(TokenKind::Colon, "expected ':' after for iterable");
    consume(TokenKind::Newline, "expected newline after for");
    consume(TokenKind::Indent, "expected indented for body");
    stmt->body = parse_block();
    return stmt;
  }
  if (match(TokenKind::KwFrom)) {
    std::string module_name;
    if (!parse_dotted_name(module_name, "expected module name after from")) return nullptr;
    consume(TokenKind::KwImport, "expected import after module name");
    std::vector<ast::ImportBinding> names;
    do {
      const Token name = peek();
      if (!consume(TokenKind::Identifier, "expected imported name")) return nullptr;
      std::string bind_name = name.text;
      if (match(TokenKind::KwAs)) {
        const Token alias = peek();
        if (!consume(TokenKind::Identifier, "expected alias after as")) return nullptr;
        bind_name = alias.text;
      }
      names.push_back(ast::ImportBinding{name.text, bind_name});
    } while (match(TokenKind::Comma));
    match(TokenKind::Newline);
    return std::make_unique<ast::FromImportStmt>(std::move(module_name), std::move(names));
  }
  if (match(TokenKind::KwImport)) {
    std::string name;
    if (!parse_dotted_name(name, "expected module name after import")) return nullptr;
    std::string bind_name = name.substr(0, name.find('.'));
    if (match(TokenKind::KwAs)) {
      const Token alias = peek();
      if (!consume(TokenKind::Identifier, "expected alias after as")) return nullptr;
      bind_name = alias.text;
    }
    match(TokenKind::Newline);
    return std::make_unique<ast::ImportStmt>(std::move(name), std::move(bind_name));
  }
  return parse_simple_statement();
}

ast::StmtPtr Parser::parse_simple_statement() {
  if (auto raw = parse_raw_block_statement()) {
    return raw;
  }
  if (match(TokenKind::KwGlobal)) {
    std::vector<std::string> names;
    do {
      const Token name = peek();
      if (!consume(TokenKind::Identifier, "expected name after global")) {
        return nullptr;
      }
      names.push_back(name.text);
    } while (match(TokenKind::Comma));
    match(TokenKind::Newline);
    return std::make_unique<ast::GlobalStmt>(std::move(names));
  }
  if (match(TokenKind::KwNonlocal)) {
    std::vector<std::string> names;
    do {
      const Token name = peek();
      if (!consume(TokenKind::Identifier, "expected name after nonlocal")) {
        return nullptr;
      }
      names.push_back(name.text);
    } while (match(TokenKind::Comma));
    match(TokenKind::Newline);
    return std::make_unique<ast::NonlocalStmt>(std::move(names));
  }
  if (match(TokenKind::KwReturn)) {
    ast::ExprPtr value;
    if (!check(TokenKind::Newline) && !check(TokenKind::Dedent) && !check(TokenKind::End)) {
      value = parse_expression();
    } else {
      value = std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::None);
    }
    match(TokenKind::Newline);
    return std::make_unique<ast::ReturnStmt>(std::move(value));
  }
  if (match(TokenKind::KwRaise)) {
    ast::ExprPtr value;
    if (!check(TokenKind::Newline) && !check(TokenKind::Dedent) && !check(TokenKind::End)) {
      value = parse_expression();
    } else {
      value = std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::None);
    }
    match(TokenKind::Newline);
    return std::make_unique<ast::RaiseStmt>(std::move(value));
  }
  if (check(TokenKind::Identifier) && current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == TokenKind::Assign) {
    const std::string name = advance().text;
    advance();
    auto value = parse_expression();
    match(TokenKind::Newline);
    return std::make_unique<ast::AssignStmt>(name, std::move(value));
  }
  auto expr = parse_expression();
  if (match(TokenKind::Assign)) {
    auto value = parse_expression();
    match(TokenKind::Newline);
    if (auto* subscript = dynamic_cast<ast::SubscriptExpr*>(expr.get())) {
      return std::make_unique<ast::SubscriptAssignStmt>(
          std::move(subscript->object),
          std::move(subscript->index),
          std::move(value));
    }
    if (auto* attr = dynamic_cast<ast::AttrExpr*>(expr.get())) {
      return std::make_unique<ast::AttrAssignStmt>(
          std::move(attr->object),
          attr->name,
          std::move(value));
    }
    error_here("expected assignable target");
    return nullptr;
  }
  match(TokenKind::Newline);
  return std::make_unique<ast::ExprStmt>(std::move(expr));
}

namespace {

std::string trim_copy(const std::string& value) {
  size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
    ++first;
  }
  size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
    --last;
  }
  return value.substr(first, last - first);
}

bool is_known_raw_block_language(const std::string& language) {
  static const std::unordered_set<std::string> kLanguages = {
      "sql",
      "markdown",
      "html",
      "yaml",
      "json",
      "text",
  };
  return kLanguages.find(language) != kLanguages.end();
}

} // namespace

ast::StmtPtr Parser::parse_raw_block_statement() {
  if (!check(TokenKind::String) || !peek().is_triple_string) {
    return nullptr;
  }

  const Token token = peek();
  std::istringstream input(token.text);
  std::string header;
  while (std::getline(input, header)) {
    header = trim_copy(header);
    if (!header.empty()) {
      break;
    }
  }
  if (header.empty()) {
    return nullptr;
  }

  std::istringstream header_input(header);
  std::string language;
  std::string provider;
  header_input >> language;
  header_input >> provider;
  if (!is_known_raw_block_language(language)) {
    return nullptr;
  }

  std::string body;
  std::string body_line;
  bool first_body_line = true;
  while (std::getline(input, body_line)) {
    if (!first_body_line) {
      body.push_back('\n');
    }
    first_body_line = false;
    body += body_line;
  }
  advance();
  match(TokenKind::Newline);
  return std::make_unique<ast::RawBlockStmt>(std::move(language), std::move(provider), std::move(body));
}

bool Parser::parse_dotted_name(std::string& out, const std::string& message) {
  const Token first = peek();
  if (!consume(TokenKind::Identifier, message)) {
    return false;
  }
  out = first.text;
  while (match(TokenKind::Dot)) {
    const Token part = peek();
    if (!consume(TokenKind::Identifier, "expected name after '.'")) {
      return false;
    }
    out += ".";
    out += part.text;
  }
  return true;
}

std::vector<ast::StmtPtr> Parser::parse_block() {
  std::vector<ast::StmtPtr> body;
  skip_newlines();
  while (!check(TokenKind::Dedent) && !check(TokenKind::End)) {
    auto stmt = parse_statement();
    if (stmt) {
      body.push_back(std::move(stmt));
    } else {
      advance();
    }
    skip_newlines();
  }
  consume(TokenKind::Dedent, "expected dedent after block");
  return body;
}

ast::ExprPtr Parser::parse_expression() {
  return parse_tuple();
}

ast::ExprPtr Parser::parse_tuple() {
  auto first = parse_or();
  if (!match(TokenKind::Comma)) {
    return first;
  }

  std::vector<ast::ExprPtr> items;
  items.push_back(std::move(first));
  while (!check(TokenKind::RParen) && !check(TokenKind::Newline) && !check(TokenKind::End)) {
    items.push_back(parse_or());
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  return std::make_unique<ast::TupleExpr>(std::move(items));
}

ast::ExprPtr Parser::parse_or() {
  auto expr = parse_and();
  while (match(TokenKind::KwOr)) {
    expr = std::make_unique<ast::BinaryExpr>(std::move(expr), "or", parse_and());
  }
  return expr;
}

ast::ExprPtr Parser::parse_and() {
  auto expr = parse_not();
  while (match(TokenKind::KwAnd)) {
    expr = std::make_unique<ast::BinaryExpr>(std::move(expr), "and", parse_not());
  }
  return expr;
}

ast::ExprPtr Parser::parse_not() {
  if (match(TokenKind::KwNot)) {
    return std::make_unique<ast::UnaryExpr>("not", parse_not());
  }
  return parse_compare();
}

ast::ExprPtr Parser::parse_compare() {
  auto expr = parse_term();
  while (true) {
    std::string op;
    if (match(TokenKind::EqualEqual)) op = "==";
    else if (match(TokenKind::NotEqual)) op = "!=";
    else if (match(TokenKind::Less)) op = "<";
    else if (match(TokenKind::LessEqual)) op = "<=";
    else if (match(TokenKind::Greater)) op = ">";
    else if (match(TokenKind::GreaterEqual)) op = ">=";
    else break;
    expr = std::make_unique<ast::BinaryExpr>(std::move(expr), op, parse_term());
  }
  return expr;
}

ast::ExprPtr Parser::parse_term() {
  auto expr = parse_factor();
  while (true) {
    std::string op;
    if (match(TokenKind::Plus)) op = "+";
    else if (match(TokenKind::Minus)) op = "-";
    else break;
    expr = std::make_unique<ast::BinaryExpr>(std::move(expr), op, parse_factor());
  }
  return expr;
}

ast::ExprPtr Parser::parse_factor() {
  auto expr = parse_unary();
  while (true) {
    std::string op;
    if (match(TokenKind::Star)) op = "*";
    else if (match(TokenKind::Slash)) op = "/";
    else break;
    expr = std::make_unique<ast::BinaryExpr>(std::move(expr), op, parse_unary());
  }
  return expr;
}

ast::ExprPtr Parser::parse_unary() {
  if (match(TokenKind::Minus)) {
    return std::make_unique<ast::UnaryExpr>("-", parse_unary());
  }
  if (match(TokenKind::Plus)) {
    return parse_unary();
  }
  return parse_call();
}

ast::ExprPtr Parser::parse_call() {
  auto expr = parse_primary();
  while (true) {
    if (match(TokenKind::LParen)) {
      std::vector<ast::ExprPtr> args;
      if (!check(TokenKind::RParen)) {
        do {
          args.push_back(parse_or());
        } while (match(TokenKind::Comma));
      }
      consume(TokenKind::RParen, "expected ')' after call arguments");
      expr = std::make_unique<ast::CallExpr>(std::move(expr), std::move(args));
    } else if (match(TokenKind::LBracket)) {
      auto index = parse_expression();
      consume(TokenKind::RBracket, "expected ']' after subscript");
      expr = std::make_unique<ast::SubscriptExpr>(std::move(expr), std::move(index));
    } else if (match(TokenKind::Dot)) {
      const Token attr = peek();
      consume(TokenKind::Identifier, "expected attribute name after '.'");
      expr = std::make_unique<ast::AttrExpr>(std::move(expr), attr.text);
    } else {
      break;
    }
  }
  return expr;
}

ast::ExprPtr Parser::parse_primary() {
  if (match(TokenKind::Integer)) return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::Int, previous().text);
  if (match(TokenKind::Double)) return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::Double, previous().text);
  if (match(TokenKind::String)) return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::String, previous().text);
  if (match(TokenKind::KwTrue)) {
    auto lit = std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::Bool);
    lit->bool_value = true;
    return lit;
  }
  if (match(TokenKind::KwFalse)) {
    auto lit = std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::Bool);
    lit->bool_value = false;
    return lit;
  }
  if (match(TokenKind::KwNone)) return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::None);
  if (match(TokenKind::Identifier)) return std::make_unique<ast::NameExpr>(previous().text);
  if (match(TokenKind::LParen)) {
    if (match(TokenKind::RParen)) {
      return std::make_unique<ast::TupleExpr>(std::vector<ast::ExprPtr>{});
    }
    auto expr = parse_expression();
    consume(TokenKind::RParen, "expected ')' after expression");
    return expr;
  }
  if (match(TokenKind::LBracket)) {
    if (match(TokenKind::RBracket)) {
      return std::make_unique<ast::ListExpr>(std::vector<ast::ExprPtr>{});
    }
    auto first = parse_or();
    if (match(TokenKind::KwFor)) {
      const Token target = peek();
      consume(TokenKind::Identifier, "expected comprehension target after for");
      consume(TokenKind::KwIn, "expected 'in' after comprehension target");
      auto iterable = parse_expression();
      ast::ExprPtr filter;
      if (match(TokenKind::KwIf)) {
        filter = parse_expression();
      }
      consume(TokenKind::RBracket, "expected ']' after list comprehension");
      return std::make_unique<ast::ListCompExpr>(std::move(first), target.text, std::move(iterable), std::move(filter));
    }
    std::vector<ast::ExprPtr> items;
    items.push_back(std::move(first));
    while (match(TokenKind::Comma) && !check(TokenKind::RBracket)) {
      items.push_back(parse_or());
    }
    consume(TokenKind::RBracket, "expected ']' after list literal");
    return std::make_unique<ast::ListExpr>(std::move(items));
  }
  if (match(TokenKind::LBrace)) {
    if (match(TokenKind::RBrace)) {
      return std::make_unique<ast::DictExpr>(std::vector<std::pair<ast::ExprPtr, ast::ExprPtr>>{});
    }
    auto first = parse_or();
    if (match(TokenKind::Colon)) {
      std::vector<std::pair<ast::ExprPtr, ast::ExprPtr>> entries;
      entries.push_back(std::make_pair(std::move(first), parse_or()));
      while (match(TokenKind::Comma) && !check(TokenKind::RBrace)) {
        auto key = parse_or();
        consume(TokenKind::Colon, "expected ':' after dict key");
        entries.push_back(std::make_pair(std::move(key), parse_or()));
      }
      consume(TokenKind::RBrace, "expected '}' after dict literal");
      return std::make_unique<ast::DictExpr>(std::move(entries));
    }
    std::vector<ast::ExprPtr> items;
    items.push_back(std::move(first));
    while (match(TokenKind::Comma) && !check(TokenKind::RBrace)) {
      items.push_back(parse_or());
    }
    consume(TokenKind::RBrace, "expected '}' after set literal");
    return std::make_unique<ast::SetExpr>(std::move(items));
  }
  error_here("expected expression");
  return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::None);
}

bool Parser::match(TokenKind kind) {
  if (!check(kind)) return false;
  advance();
  return true;
}

bool Parser::check(TokenKind kind) const {
  if (current_ >= tokens_.size()) return kind == TokenKind::End;
  return tokens_[current_].kind == kind;
}

const Token& Parser::peek() const {
  return tokens_[current_];
}

const Token& Parser::previous() const {
  return tokens_[current_ - 1];
}

const Token& Parser::advance() {
  if (!check(TokenKind::End)) {
    ++current_;
  }
  return previous();
}

bool Parser::consume(TokenKind kind, const std::string& message) {
  if (match(kind)) return true;
  error_here(message);
  return false;
}

void Parser::skip_newlines() {
  while (match(TokenKind::Newline)) {}
}

void Parser::error_here(const std::string& message) {
  const Token& tok = peek();
  errors_.push_back("line " + std::to_string(tok.line) + ", column " + std::to_string(tok.column) + ": " + message);
}

} // namespace xlang3
