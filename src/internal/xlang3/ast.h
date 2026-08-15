#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xlang3::ast {

struct Expr {
  virtual ~Expr() = default;
};

struct Stmt {
  virtual ~Stmt() = default;
};

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct LiteralExpr final : Expr {
  enum class Kind { None, Bool, Int, Double, String };
  Kind kind;
  std::string text;
  bool bool_value = false;

  explicit LiteralExpr(Kind kind, std::string text = {}) : kind(kind), text(std::move(text)) {}
};

struct NameExpr final : Expr {
  std::string name;
  explicit NameExpr(std::string name) : name(std::move(name)) {}
};

struct UnaryExpr final : Expr {
  std::string op;
  ExprPtr expr;
  UnaryExpr(std::string op, ExprPtr expr) : op(std::move(op)), expr(std::move(expr)) {}
};

struct BinaryExpr final : Expr {
  std::string op;
  ExprPtr lhs;
  ExprPtr rhs;
  BinaryExpr(ExprPtr lhs, std::string op, ExprPtr rhs)
      : op(std::move(op)), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

struct CallExpr final : Expr {
  ExprPtr callee;
  std::vector<ExprPtr> args;
  CallExpr(ExprPtr callee, std::vector<ExprPtr> args)
      : callee(std::move(callee)), args(std::move(args)) {}
};

struct TupleExpr final : Expr {
  std::vector<ExprPtr> items;
  explicit TupleExpr(std::vector<ExprPtr> items) : items(std::move(items)) {}
};

struct ExprStmt final : Stmt {
  ExprPtr expr;
  explicit ExprStmt(ExprPtr expr) : expr(std::move(expr)) {}
};

struct AssignStmt final : Stmt {
  std::string name;
  ExprPtr value;
  AssignStmt(std::string name, ExprPtr value) : name(std::move(name)), value(std::move(value)) {}
};

struct ReturnStmt final : Stmt {
  ExprPtr value;
  explicit ReturnStmt(ExprPtr value) : value(std::move(value)) {}
};

struct NonlocalStmt final : Stmt {
  std::vector<std::string> names;
  explicit NonlocalStmt(std::vector<std::string> names) : names(std::move(names)) {}
};

struct IfStmt final : Stmt {
  ExprPtr condition;
  std::vector<StmtPtr> then_body;
  std::vector<StmtPtr> else_body;
};

struct WhileStmt final : Stmt {
  ExprPtr condition;
  std::vector<StmtPtr> body;
};

struct FunctionDef final : Stmt {
  std::string name;
  std::vector<std::string> params;
  std::vector<StmtPtr> body;
};

struct Module {
  std::vector<StmtPtr> body;
};

} // namespace xlang3::ast
