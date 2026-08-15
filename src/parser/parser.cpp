#include "xlang3/parser.h"

#include <cstdlib>

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
  if (match(TokenKind::KwWhile)) {
    auto stmt = std::make_unique<ast::WhileStmt>();
    stmt->condition = parse_expression();
    consume(TokenKind::Colon, "expected ':' after while condition");
    consume(TokenKind::Newline, "expected newline after while");
    consume(TokenKind::Indent, "expected indented while body");
    stmt->body = parse_block();
    return stmt;
  }
  return parse_simple_statement();
}

ast::StmtPtr Parser::parse_simple_statement() {
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
  if (check(TokenKind::Identifier) && current_ + 1 < tokens_.size() && tokens_[current_ + 1].kind == TokenKind::Assign) {
    const std::string name = advance().text;
    advance();
    auto value = parse_expression();
    match(TokenKind::Newline);
    return std::make_unique<ast::AssignStmt>(name, std::move(value));
  }
  auto expr = parse_expression();
  match(TokenKind::Newline);
  return std::make_unique<ast::ExprStmt>(std::move(expr));
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
  return parse_or();
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
  while (match(TokenKind::LParen)) {
    std::vector<ast::ExprPtr> args;
    if (!check(TokenKind::RParen)) {
      do {
        args.push_back(parse_expression());
      } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, "expected ')' after call arguments");
    expr = std::make_unique<ast::CallExpr>(std::move(expr), std::move(args));
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
    auto expr = parse_expression();
    consume(TokenKind::RParen, "expected ')' after expression");
    return expr;
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
