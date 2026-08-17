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

#include "xlang3/source_cursor.h"

#include <cctype>
#include <cstdlib>
#include <unordered_set>

namespace xlang3 {

Parser::Parser(LexResult lex)
    : tokens_(std::move(lex.tokens)),
      owned_text_(std::move(lex.owned_text)),
      errors_(std::move(lex.errors)) {}

ParseResult parse_source(const std::string& source) {
  Lexer lexer(source);
  Parser parser(lexer.tokenize());
  auto result = parser.parse_module();
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
  if (check(TokenKind::At)) {
    return parse_decorated_statement();
  }
  bool is_async_def = false;
  if (match(TokenKind::KwAsync)) {
    if (match(TokenKind::KwFor)) {
      auto stmt = std::make_unique<ast::ForStmt>();
      const Token target = peek();
      if (!consume(TokenKind::Identifier, "expected loop target after async for")) return nullptr;
      stmt->target = std::string(target.text);
      consume(TokenKind::KwIn, "expected 'in' after loop target");
      stmt->iterable = parse_expression();
      consume(TokenKind::Colon, "expected ':' after async for iterable");
      consume(TokenKind::Newline, "expected newline after async for");
      consume(TokenKind::Indent, "expected indented async for body");
      stmt->body = parse_block();
      return stmt;
    }
    if (match(TokenKind::KwWith)) {
      return parse_with_statement();
    }
    if (!check(TokenKind::KwDef)) {
      error_here("expected def, for, or with after async");
      return nullptr;
    }
    is_async_def = true;
  }
  if (match(TokenKind::KwDef)) {
    const Token name = peek();
    if (!consume(TokenKind::Identifier, "expected function name")) return nullptr;
    consume_optional_type_params();
    std::vector<std::string> params;
    std::vector<ast::FunctionDef::Param> signature;
    ast::ExprPtr return_annotation;
    if (!parse_function_signature(params, signature, return_annotation)) return nullptr;
    consume(TokenKind::Colon, "expected ':' after function header");
    consume(TokenKind::Newline, "expected newline after function header");
    consume(TokenKind::Indent, "expected indented function body");
    auto fn = std::make_unique<ast::FunctionDef>();
    fn->name = std::string(name.text);
    fn->params = std::move(params);
    fn->signature = std::move(signature);
    fn->return_annotation = std::move(return_annotation);
    fn->body = parse_block();
    fn->is_async = is_async_def;
    return fn;
  }
  if (match(TokenKind::KwClass)) {
    const Token name = peek();
    if (!consume(TokenKind::Identifier, "expected class name")) return nullptr;
    consume_optional_type_params();
    std::vector<ast::ExprPtr> bases;
    std::vector<std::pair<std::string, ast::ExprPtr>> keywords;
    if (match(TokenKind::LParen)) {
      while (!check(TokenKind::RParen) && !check(TokenKind::End)) {
        if (check(TokenKind::Identifier) && current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == TokenKind::Assign) {
          const std::string key(advance().text);
          advance();
          keywords.push_back(std::make_pair(key, parse_or()));
        } else {
          bases.push_back(parse_or());
        }
        if (!match(TokenKind::Comma)) {
          break;
        }
      }
      consume(TokenKind::RParen, "expected ')' after class bases");
    }
    consume(TokenKind::Colon, "expected ':' after class name");
    consume(TokenKind::Newline, "expected newline after class header");
    consume(TokenKind::Indent, "expected indented class body");
    auto klass = std::make_unique<ast::ClassDef>();
    klass->name = std::string(name.text);
    klass->bases = std::move(bases);
    klass->keywords = std::move(keywords);
    klass->body = parse_block();
    return klass;
  }
  if (match(TokenKind::KwIf)) {
    return parse_if_statement();
  }
  if (match(TokenKind::KwTry)) {
    return parse_try_statement();
  }
  if (match(TokenKind::KwWith)) {
    return parse_with_statement();
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
    stmt->target = std::string(target.text);
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
    if (!parse_dotted_name(module_name, "expected module name after from", true)) return nullptr;
    consume(TokenKind::KwImport, "expected import after module name");
    std::vector<ast::ImportBinding> names;
    if (match(TokenKind::Star)) {
      names.push_back(ast::ImportBinding{"*", "*"});
    } else {
      do {
        const Token name = peek();
        if (!consume(TokenKind::Identifier, "expected imported name")) return nullptr;
        std::string bind_name(name.text);
        if (match(TokenKind::KwAs)) {
          const Token alias = peek();
          if (!consume(TokenKind::Identifier, "expected alias after as")) return nullptr;
          bind_name = std::string(alias.text);
        }
        names.push_back(ast::ImportBinding{std::string(name.text), bind_name});
      } while (match(TokenKind::Comma));
    }
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
      bind_name = std::string(alias.text);
    }
    match(TokenKind::Newline);
    return std::make_unique<ast::ImportStmt>(std::move(name), std::move(bind_name));
  }
  if (match(TokenKind::KwMatch)) {
    return parse_match_statement();
  }
  return parse_simple_statement();
}

ast::StmtPtr Parser::parse_decorated_statement() {
  std::vector<ast::ExprPtr> decorators;
  while (match(TokenKind::At)) {
    decorators.push_back(parse_expression());
    consume(TokenKind::Newline, "expected newline after decorator");
  }
  auto stmt = parse_statement();
  if (auto* fn = dynamic_cast<ast::FunctionDef*>(stmt.get())) {
    fn->decorators = std::move(decorators);
  } else if (auto* klass = dynamic_cast<ast::ClassDef*>(stmt.get())) {
    klass->decorators = std::move(decorators);
  } else {
    error_here("decorator must be followed by function or class definition");
  }
  return stmt;
}

ast::StmtPtr Parser::parse_if_statement() {
  auto stmt = std::make_unique<ast::IfStmt>();
  stmt->condition = parse_expression();
  consume(TokenKind::Colon, "expected ':' after if condition");
  consume(TokenKind::Newline, "expected newline after if");
  consume(TokenKind::Indent, "expected indented if body");
  stmt->then_body = parse_block();
  if (match(TokenKind::KwElif)) {
    std::vector<ast::StmtPtr> nested;
    nested.push_back(parse_if_statement());
    stmt->else_body = std::move(nested);
  } else if (match(TokenKind::KwElse)) {
    consume(TokenKind::Colon, "expected ':' after else");
    consume(TokenKind::Newline, "expected newline after else");
    consume(TokenKind::Indent, "expected indented else body");
    stmt->else_body = parse_block();
  }
  return stmt;
}

ast::StmtPtr Parser::parse_try_statement() {
  auto stmt = std::make_unique<ast::TryExceptStmt>();
  consume(TokenKind::Colon, "expected ':' after try");
  consume(TokenKind::Newline, "expected newline after try");
  consume(TokenKind::Indent, "expected indented try body");
  stmt->try_body = parse_block();
  while (match(TokenKind::KwExcept)) {
    ast::ExceptHandler handler;
    if (!check(TokenKind::Colon)) {
      handler.type = parse_expression();
      if (match(TokenKind::KwAs)) {
        const Token name = peek();
        if (!consume(TokenKind::Identifier, "expected exception name after as")) return nullptr;
        handler.name = std::string(name.text);
      }
    }
    consume(TokenKind::Colon, "expected ':' after except");
    consume(TokenKind::Newline, "expected newline after except");
    consume(TokenKind::Indent, "expected indented except body");
    handler.body = parse_block();
    stmt->handlers.push_back(std::move(handler));
  }
  if (match(TokenKind::KwElse)) {
    consume(TokenKind::Colon, "expected ':' after try else");
    consume(TokenKind::Newline, "expected newline after try else");
    consume(TokenKind::Indent, "expected indented try else body");
    stmt->else_body = parse_block();
  }
  if (match(TokenKind::KwFinally)) {
    consume(TokenKind::Colon, "expected ':' after finally");
    consume(TokenKind::Newline, "expected newline after finally");
    consume(TokenKind::Indent, "expected indented finally body");
    stmt->finally_body = parse_block();
  }
  if (stmt->handlers.empty() && stmt->finally_body.empty()) {
    error_here("expected except or finally after try body");
  }
  return stmt;
}

ast::ExprPtr Parser::parse_with_manager_expr() {
  return parse_or();
}

ast::StmtPtr Parser::parse_with_statement() {
  struct WithItem {
    ast::ExprPtr manager;
    std::string target;
  };
  std::vector<WithItem> items;
  const bool parenthesized = match(TokenKind::LParen);
  do {
    WithItem item;
    item.manager = parse_with_manager_expr();
    if (match(TokenKind::KwAs)) {
      const Token target = peek();
      if (!consume(TokenKind::Identifier, "expected name after as")) return nullptr;
      item.target = std::string(target.text);
    }
    items.push_back(std::move(item));
  } while (match(TokenKind::Comma) && !(parenthesized && check(TokenKind::RParen)));
  if (parenthesized) {
    consume(TokenKind::RParen, "expected ')' after with items");
  }
  consume(TokenKind::Colon, "expected ':' after with");
  consume(TokenKind::Newline, "expected newline after with");
  consume(TokenKind::Indent, "expected indented with body");
  auto body = parse_block();
  for (auto it = items.rbegin(); it != items.rend(); ++it) {
    auto stmt = std::make_unique<ast::WithStmt>();
    stmt->manager = std::move(it->manager);
    stmt->target = std::move(it->target);
    stmt->body = std::move(body);
    body.clear();
    body.push_back(std::move(stmt));
  }
  return std::move(body.front());
}

ast::StmtPtr Parser::parse_match_statement() {
  auto stmt = std::make_unique<ast::MatchStmt>();
  stmt->subject = parse_expression();
  consume(TokenKind::Colon, "expected ':' after match subject");
  consume(TokenKind::Newline, "expected newline after match");
  consume(TokenKind::Indent, "expected indented match body");
  skip_newlines();
  while (!check(TokenKind::Dedent) && !check(TokenKind::End)) {
    if (!match(TokenKind::KwCase)) {
      error_here("expected case in match body");
      advance();
      continue;
    }
    ast::MatchCase match_case;
    if (check(TokenKind::Identifier) && peek().text == "_") {
      advance();
      match_case.wildcard = true;
    } else {
      match_case.pattern = parse_expression();
    }
    consume(TokenKind::Colon, "expected ':' after case pattern");
    consume(TokenKind::Newline, "expected newline after case");
    consume(TokenKind::Indent, "expected indented case body");
    match_case.body = parse_block();
    stmt->cases.push_back(std::move(match_case));
    skip_newlines();
  }
  consume(TokenKind::Dedent, "expected dedent after match body");
  return stmt;
}

ast::StmtPtr Parser::parse_simple_statement() {
  if (auto raw = parse_raw_block_statement()) {
    return raw;
  }
  if (match(TokenKind::KwPass)) {
    consume_simple_statement_end();
    return std::make_unique<ast::PassStmt>();
  }
  if (match(TokenKind::KwBreak)) {
    consume_simple_statement_end();
    return std::make_unique<ast::BreakStmt>();
  }
  if (match(TokenKind::KwContinue)) {
    consume_simple_statement_end();
    return std::make_unique<ast::ContinueStmt>();
  }
  if (match(TokenKind::KwDel)) {
    auto target = parse_expression();
    consume_simple_statement_end();
    return std::make_unique<ast::DelStmt>(std::move(target));
  }
  if (match(TokenKind::KwAssert)) {
    auto condition = parse_expression();
    ast::ExprPtr message;
    if (match(TokenKind::Comma)) {
      message = parse_expression();
    }
    consume_simple_statement_end();
    return std::make_unique<ast::AssertStmt>(std::move(condition), std::move(message));
  }
  if (match(TokenKind::KwGlobal)) {
    std::vector<std::string> names;
    do {
      const Token name = peek();
      if (!consume(TokenKind::Identifier, "expected name after global")) {
        return nullptr;
      }
      names.push_back(std::string(name.text));
    } while (match(TokenKind::Comma));
    consume_simple_statement_end();
    return std::make_unique<ast::GlobalStmt>(std::move(names));
  }
  if (match(TokenKind::KwNonlocal)) {
    std::vector<std::string> names;
    do {
      const Token name = peek();
      if (!consume(TokenKind::Identifier, "expected name after nonlocal")) {
        return nullptr;
      }
      names.push_back(std::string(name.text));
    } while (match(TokenKind::Comma));
    consume_simple_statement_end();
    return std::make_unique<ast::NonlocalStmt>(std::move(names));
  }
  if (match(TokenKind::KwReturn)) {
    ast::ExprPtr value;
    if (!check(TokenKind::Newline) && !check(TokenKind::Dedent) && !check(TokenKind::End)) {
      value = parse_expression();
    } else {
      value = std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::None);
    }
    consume_simple_statement_end();
    return std::make_unique<ast::ReturnStmt>(std::move(value));
  }
  if (match(TokenKind::KwRaise)) {
    ast::ExprPtr value;
    ast::ExprPtr cause;
    if (!is_simple_statement_end() && !check(TokenKind::Dedent) && !check(TokenKind::End)) {
      value = parse_expression();
      if (match(TokenKind::KwFrom)) {
        cause = parse_expression();
      }
    }
    consume_simple_statement_end();
    return std::make_unique<ast::RaiseStmt>(std::move(value), std::move(cause));
  }
  if (check(TokenKind::Identifier) && current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == TokenKind::Assign) {
    const std::string name(advance().text);
    advance();
    auto value = parse_expression();
    consume_simple_statement_end();
    return std::make_unique<ast::AssignStmt>(name, std::move(value));
  }
  auto expr = parse_expression();
  if (match(TokenKind::Assign)) {
    auto value = parse_expression();
    consume_simple_statement_end();
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
  consume_simple_statement_end();
  return std::make_unique<ast::ExprStmt>(std::move(expr));
}

namespace {

bool is_known_raw_block_language(std::string_view language) {
  static const std::unordered_set<std::string> kLanguages = {
      "sql",
      "markdown",
      "html",
      "yaml",
      "json",
      "text",
  };
  return kLanguages.find(std::string(language)) != kLanguages.end();
}

} // namespace

ast::StmtPtr Parser::parse_raw_block_statement() {
  if (!check(TokenKind::String) || !peek().is_triple_string) {
    return nullptr;
  }

  const Token token = peek();
  SourceLines lines(token.text);
  SourceLine line;
  std::string_view header;
  while (lines.next(line)) {
    header = trim_ascii_space(line.text);
    if (!header.empty()) {
      break;
    }
  }
  if (header.empty()) {
    return nullptr;
  }

  std::string_view language;
  std::string_view provider;
  size_t header_offset = 0;
  next_ascii_word(header, header_offset, language);
  next_ascii_word(header, header_offset, provider);
  if (!is_known_raw_block_language(language)) {
    return nullptr;
  }

  std::string body;
  bool first_body_line = true;
  while (lines.next(line)) {
    if (!first_body_line) {
      body.push_back('\n');
    }
    first_body_line = false;
    body += std::string(line.text);
  }
  advance();
  match(TokenKind::Newline);
  return std::make_unique<ast::RawBlockStmt>(std::string(language), std::string(provider), std::move(body));
}

bool Parser::parse_dotted_name(std::string& out, const std::string& message, bool allow_leading_dots) {
  out.clear();
  if (allow_leading_dots) {
    while (match(TokenKind::Dot)) {
      out += ".";
    }
  }
  if (check(TokenKind::KwImport) && !out.empty()) {
    return true;
  }
  const Token first = peek();
  if (!consume(TokenKind::Identifier, message)) {
    return !out.empty();
  }
  out += std::string(first.text);
  while (match(TokenKind::Dot)) {
    if (check(TokenKind::KwImport)) {
      if (allow_leading_dots) {
        out += ".";
        return true;
      }
      error_here("expected name after '.'");
      return false;
    }
    const Token part = peek();
    if (!consume(TokenKind::Identifier, "expected name after '.'")) {
      return false;
    }
    out += ".";
    out += std::string(part.text);
  }
  return true;
}

bool Parser::consume_optional_type_params() {
  if (!match(TokenKind::LBracket)) {
    return true;
  }
  uint32_t depth = 1;
  while (depth != 0 && !check(TokenKind::End)) {
    if (match(TokenKind::LBracket)) {
      ++depth;
    } else if (match(TokenKind::RBracket)) {
      --depth;
    } else {
      advance();
    }
  }
  return depth == 0;
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
  return parse_await();
}

ast::ExprPtr Parser::parse_await() {
  if (match(TokenKind::KwAwait)) {
    return std::make_unique<ast::AwaitExpr>(parse_await());
  }
  if (match(TokenKind::KwYield)) {
    const bool from = match(TokenKind::KwFrom);
    if (is_simple_statement_end() || check(TokenKind::RParen) || check(TokenKind::RBracket) || check(TokenKind::RBrace)) {
      return std::make_unique<ast::YieldExpr>(std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::None), from);
    }
    return std::make_unique<ast::YieldExpr>(parse_await(), from);
  }
  return parse_lambda();
}

ast::ExprPtr Parser::parse_lambda() {
  if (!match(TokenKind::KwLambda)) {
    return parse_compare();
  }
  std::vector<std::string> params;
  if (!check(TokenKind::Colon)) {
    do {
      const Token name = peek();
      if (!consume(TokenKind::Identifier, "expected lambda parameter name")) return nullptr;
      params.push_back(std::string(name.text));
    } while (match(TokenKind::Comma));
  }
  consume(TokenKind::Colon, "expected ':' after lambda parameters");
  return std::make_unique<ast::LambdaExpr>(std::move(params), parse_expression());
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
    else if (match(TokenKind::Percent)) op = "%";
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
      std::vector<ast::CallExpr::Arg> args;
      bool simple_positional = true;
      if (!check(TokenKind::RParen)) {
        do {
          ast::CallExpr::Arg arg;
          if (match(TokenKind::DoubleStar)) {
            arg.kw_star = true;
            arg.value = parse_or();
            simple_positional = false;
          } else if (match(TokenKind::Star)) {
            arg.star = true;
            arg.value = parse_or();
            simple_positional = false;
          } else if (check(TokenKind::Identifier) && current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == TokenKind::Assign) {
            arg.name = std::string(advance().text);
            advance();
            arg.value = parse_or();
            simple_positional = false;
          } else {
            arg.value = parse_or();
          }
          args.push_back(std::move(arg));
        } while (match(TokenKind::Comma));
      }
      consume(TokenKind::RParen, "expected ')' after call arguments");
      if (simple_positional) {
        std::vector<ast::ExprPtr> positional;
        positional.reserve(args.size());
        for (auto& arg : args) {
          positional.push_back(std::move(arg.value));
        }
        expr = std::make_unique<ast::CallExpr>(std::move(expr), std::move(positional));
      } else {
        expr = std::make_unique<ast::CallExpr>(std::move(expr), std::move(args));
      }
    } else if (match(TokenKind::LBracket)) {
      auto index = parse_expression();
      consume(TokenKind::RBracket, "expected ']' after subscript");
      expr = std::make_unique<ast::SubscriptExpr>(std::move(expr), std::move(index));
    } else if (match(TokenKind::Dot)) {
      const Token attr = peek();
      consume(TokenKind::Identifier, "expected attribute name after '.'");
      expr = std::make_unique<ast::AttrExpr>(std::move(expr), std::string(attr.text));
    } else {
      break;
    }
  }
  return expr;
}

ast::ExprPtr Parser::parse_primary() {
  if (match(TokenKind::Integer)) return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::Int, std::string(previous().text));
  if (match(TokenKind::Double)) return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::Double, std::string(previous().text));
  if (match(TokenKind::String)) return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::String, std::string(previous().text));
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
  if (match(TokenKind::Identifier)) return std::make_unique<ast::NameExpr>(std::string(previous().text));
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
      return std::make_unique<ast::ListCompExpr>(std::move(first), std::string(target.text), std::move(iterable), std::move(filter));
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

ast::ExprPtr Parser::parse_optional_annotation() {
  if (!match(TokenKind::Colon)) {
    return nullptr;
  }
  return parse_or();
}

bool Parser::parse_function_signature(
    std::vector<std::string>& params,
    std::vector<ast::FunctionDef::Param>& signature,
    ast::ExprPtr& return_annotation) {
  consume(TokenKind::LParen, "expected '(' after function name");
  bool saw_keyword_only_marker = false;
  if (!check(TokenKind::RParen)) {
    do {
      if (match(TokenKind::Slash)) {
        for (auto& param : signature) {
          if (param.kind == ast::FunctionDef::Param::Kind::PosOrKeyword) {
            param.kind = ast::FunctionDef::Param::Kind::PosOnly;
          }
        }
        continue;
      }

      ast::FunctionDef::Param param;
      if (match(TokenKind::DoubleStar)) {
        const Token name = peek();
        if (!consume(TokenKind::Identifier, "expected ** parameter name")) return false;
        param.name = std::string(name.text);
        param.kind = ast::FunctionDef::Param::Kind::KwArgs;
        param.annotation = parse_optional_annotation();
      } else if (match(TokenKind::Star)) {
        saw_keyword_only_marker = true;
        if (check(TokenKind::Comma) || check(TokenKind::RParen)) {
          continue;
        }
        const Token name = peek();
        if (!consume(TokenKind::Identifier, "expected * parameter name")) return false;
        param.name = std::string(name.text);
        param.kind = ast::FunctionDef::Param::Kind::VarArgs;
        param.annotation = parse_optional_annotation();
      } else {
        const Token name = peek();
        if (!consume(TokenKind::Identifier, "expected parameter name")) return false;
        param.name = std::string(name.text);
        param.kind = saw_keyword_only_marker
            ? ast::FunctionDef::Param::Kind::KeywordOnly
            : ast::FunctionDef::Param::Kind::PosOrKeyword;
        param.annotation = parse_optional_annotation();
      }

      if (match(TokenKind::Assign)) {
        param.default_value = parse_or();
      }
      params.push_back(param.name);
      signature.push_back(std::move(param));
    } while (match(TokenKind::Comma) && !check(TokenKind::RParen));
  }
  consume(TokenKind::RParen, "expected ')' after parameters");
  if (match(TokenKind::Arrow)) {
    return_annotation = parse_expression();
  }
  return true;
}

void Parser::consume_simple_statement_end() {
  if (match(TokenKind::Semicolon)) {
    return;
  }
  match(TokenKind::Newline);
}

bool Parser::is_simple_statement_end() const {
  return check(TokenKind::Semicolon) || check(TokenKind::Newline);
}

void Parser::skip_newlines() {
  while (match(TokenKind::Newline)) {}
}

void Parser::error_here(const std::string& message) {
  const Token& tok = peek();
  errors_.push_back("line " + std::to_string(tok.line) + ", column " + std::to_string(tok.column) + ": " + message);
}

} // namespace xlang3
