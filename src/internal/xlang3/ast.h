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

struct SubscriptExpr final : Expr {
  ExprPtr object;
  ExprPtr index;
  SubscriptExpr(ExprPtr object, ExprPtr index) : object(std::move(object)), index(std::move(index)) {}
};

struct AttrExpr final : Expr {
  ExprPtr object;
  std::string name;
  AttrExpr(ExprPtr object, std::string name) : object(std::move(object)), name(std::move(name)) {}
};

struct TupleExpr final : Expr {
  std::vector<ExprPtr> items;
  explicit TupleExpr(std::vector<ExprPtr> items) : items(std::move(items)) {}
};

struct ListExpr final : Expr {
  std::vector<ExprPtr> items;
  explicit ListExpr(std::vector<ExprPtr> items) : items(std::move(items)) {}
};

struct DictExpr final : Expr {
  std::vector<std::pair<ExprPtr, ExprPtr>> entries;
  explicit DictExpr(std::vector<std::pair<ExprPtr, ExprPtr>> entries) : entries(std::move(entries)) {}
};

struct SetExpr final : Expr {
  std::vector<ExprPtr> items;
  explicit SetExpr(std::vector<ExprPtr> items) : items(std::move(items)) {}
};

struct ListCompExpr final : Expr {
  ExprPtr result;
  std::string target;
  ExprPtr iterable;
  ExprPtr filter;
  ListCompExpr(ExprPtr result, std::string target, ExprPtr iterable, ExprPtr filter = {})
      : result(std::move(result)), target(std::move(target)), iterable(std::move(iterable)), filter(std::move(filter)) {}
};

struct ExprStmt final : Stmt {
  ExprPtr expr;
  explicit ExprStmt(ExprPtr expr) : expr(std::move(expr)) {}
};

struct RawBlockStmt final : Stmt {
  std::string language;
  std::string provider;
  std::string body;
  RawBlockStmt(std::string language, std::string provider, std::string body)
      : language(std::move(language)), provider(std::move(provider)), body(std::move(body)) {}
};

struct AssignStmt final : Stmt {
  std::string name;
  ExprPtr value;
  AssignStmt(std::string name, ExprPtr value) : name(std::move(name)), value(std::move(value)) {}
};

struct SubscriptAssignStmt final : Stmt {
  ExprPtr object;
  ExprPtr index;
  ExprPtr value;
  SubscriptAssignStmt(ExprPtr object, ExprPtr index, ExprPtr value)
      : object(std::move(object)), index(std::move(index)), value(std::move(value)) {}
};

struct AttrAssignStmt final : Stmt {
  ExprPtr object;
  std::string name;
  ExprPtr value;
  AttrAssignStmt(ExprPtr object, std::string name, ExprPtr value)
      : object(std::move(object)), name(std::move(name)), value(std::move(value)) {}
};

struct ImportBinding {
  std::string name;
  std::string as_name;
};

struct ImportStmt final : Stmt {
  std::string name;
  std::string bind_name;
  ImportStmt(std::string name, std::string bind_name)
      : name(std::move(name)), bind_name(std::move(bind_name)) {}
};

struct FromImportStmt final : Stmt {
  std::string module;
  std::vector<ImportBinding> names;
  FromImportStmt(std::string module, std::vector<ImportBinding> names)
      : module(std::move(module)), names(std::move(names)) {}
};

struct ReturnStmt final : Stmt {
  ExprPtr value;
  explicit ReturnStmt(ExprPtr value) : value(std::move(value)) {}
};

struct RaiseStmt final : Stmt {
  ExprPtr value;
  explicit RaiseStmt(ExprPtr value) : value(std::move(value)) {}
};

struct GlobalStmt final : Stmt {
  std::vector<std::string> names;
  explicit GlobalStmt(std::vector<std::string> names) : names(std::move(names)) {}
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

struct TryExceptStmt final : Stmt {
  std::vector<StmtPtr> try_body;
  std::vector<StmtPtr> except_body;
};

struct WhileStmt final : Stmt {
  ExprPtr condition;
  std::vector<StmtPtr> body;
};

struct ForStmt final : Stmt {
  std::string target;
  ExprPtr iterable;
  std::vector<StmtPtr> body;
};

struct FunctionDef final : Stmt {
  std::string name;
  std::vector<std::string> params;
  std::vector<StmtPtr> body;
};

struct ClassDef final : Stmt {
  std::string name;
  std::vector<StmtPtr> body;
};

struct Module {
  std::vector<StmtPtr> body;
};

} // namespace xlang3::ast
