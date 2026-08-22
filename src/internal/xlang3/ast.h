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

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xlang3::ast {

struct Expr {
  virtual ~Expr() = default;
};

struct Stmt {
  uint32_t line = 0;

  virtual ~Stmt() = default;
};

using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

struct LiteralExpr final : Expr {
  enum class Kind { None, Bool, Int, Double, String, Bytes, Ellipsis };
  Kind kind;
  std::string text;
  bool bool_value = false;

  explicit LiteralExpr(Kind kind, std::string text = {}) : kind(kind), text(std::move(text)) {}
};

struct FStringExpr final : Expr {
  struct Part {
    bool is_expr = false;
    std::string text;
  };
  std::vector<Part> parts;
  explicit FStringExpr(std::vector<Part> parts) : parts(std::move(parts)) {}
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

struct AwaitExpr final : Expr {
  ExprPtr expr;
  explicit AwaitExpr(ExprPtr expr) : expr(std::move(expr)) {}
};

struct YieldExpr final : Expr {
  ExprPtr expr;
  bool from = false;
  YieldExpr(ExprPtr expr, bool from) : expr(std::move(expr)), from(from) {}
};

struct BinaryExpr final : Expr {
  std::string op;
  ExprPtr lhs;
  ExprPtr rhs;
  BinaryExpr(ExprPtr lhs, std::string op, ExprPtr rhs)
      : op(std::move(op)), lhs(std::move(lhs)), rhs(std::move(rhs)) {}
};

struct CompareChainExpr final : Expr {
  ExprPtr first;
  std::vector<std::pair<std::string, ExprPtr>> comparisons;
  CompareChainExpr(ExprPtr first, std::vector<std::pair<std::string, ExprPtr>> comparisons)
      : first(std::move(first)), comparisons(std::move(comparisons)) {}
};

struct ConditionalExpr final : Expr {
  ExprPtr then_expr;
  ExprPtr condition;
  ExprPtr else_expr;
  ConditionalExpr(ExprPtr then_expr, ExprPtr condition, ExprPtr else_expr)
      : then_expr(std::move(then_expr)), condition(std::move(condition)), else_expr(std::move(else_expr)) {}
};

struct NamedExpr final : Expr {
  std::string name;
  ExprPtr value;
  NamedExpr(std::string name, ExprPtr value) : name(std::move(name)), value(std::move(value)) {}
};

struct StarredExpr final : Expr {
  ExprPtr expr;
  explicit StarredExpr(ExprPtr expr) : expr(std::move(expr)) {}
};

struct CallExpr final : Expr {
  struct Arg {
    std::string name;
    ExprPtr value;
    bool star = false;
    bool kw_star = false;
  };
  ExprPtr callee;
  std::vector<ExprPtr> args;
  std::vector<Arg> call_args;
  CallExpr(ExprPtr callee, std::vector<ExprPtr> args)
      : callee(std::move(callee)), args(std::move(args)) {}
  CallExpr(ExprPtr callee, std::vector<Arg> call_args)
      : callee(std::move(callee)), call_args(std::move(call_args)) {}
};

struct SubscriptExpr final : Expr {
  ExprPtr object;
  ExprPtr index;
  SubscriptExpr(ExprPtr object, ExprPtr index) : object(std::move(object)), index(std::move(index)) {}
};

struct SliceExpr final : Expr {
  ExprPtr start;
  ExprPtr stop;
  ExprPtr step;
  SliceExpr(ExprPtr start, ExprPtr stop, ExprPtr step)
      : start(std::move(start)), stop(std::move(stop)), step(std::move(step)) {}
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

struct CompClause {
  std::string target;
  ExprPtr target_expr;
  ExprPtr iterable;
  ExprPtr filter;
};

struct ListCompExpr final : Expr {
  ExprPtr result;
  std::string target;
  ExprPtr target_expr;
  ExprPtr iterable;
  ExprPtr filter;
  std::vector<CompClause> extra_clauses;
  ListCompExpr(ExprPtr result, std::string target, ExprPtr iterable, ExprPtr filter = {})
      : result(std::move(result)),
        target(std::move(target)),
        target_expr(std::make_unique<NameExpr>(this->target)),
        iterable(std::move(iterable)),
        filter(std::move(filter)) {}
  ListCompExpr(ExprPtr result, std::string target, ExprPtr target_expr, ExprPtr iterable, ExprPtr filter = {})
      : result(std::move(result)),
        target(std::move(target)),
        target_expr(std::move(target_expr)),
        iterable(std::move(iterable)),
        filter(std::move(filter)) {}
};

struct DictCompExpr final : Expr {
  ExprPtr key;
  ExprPtr value;
  std::string target;
  ExprPtr target_expr;
  ExprPtr iterable;
  ExprPtr filter;
  std::vector<CompClause> extra_clauses;
  DictCompExpr(ExprPtr key, ExprPtr value, std::string target, ExprPtr iterable, ExprPtr filter = {})
      : key(std::move(key)),
        value(std::move(value)),
        target(std::move(target)),
        target_expr(std::make_unique<NameExpr>(this->target)),
        iterable(std::move(iterable)),
        filter(std::move(filter)) {}
  DictCompExpr(ExprPtr key, ExprPtr value, std::string target, ExprPtr target_expr, ExprPtr iterable, ExprPtr filter = {})
      : key(std::move(key)),
        value(std::move(value)),
        target(std::move(target)),
        target_expr(std::move(target_expr)),
        iterable(std::move(iterable)),
        filter(std::move(filter)) {}
};

struct SetCompExpr final : Expr {
  ExprPtr result;
  std::string target;
  ExprPtr target_expr;
  ExprPtr iterable;
  ExprPtr filter;
  std::vector<CompClause> extra_clauses;
  SetCompExpr(ExprPtr result, std::string target, ExprPtr iterable, ExprPtr filter = {})
      : result(std::move(result)),
        target(std::move(target)),
        target_expr(std::make_unique<NameExpr>(this->target)),
        iterable(std::move(iterable)),
        filter(std::move(filter)) {}
  SetCompExpr(ExprPtr result, std::string target, ExprPtr target_expr, ExprPtr iterable, ExprPtr filter = {})
      : result(std::move(result)),
        target(std::move(target)),
        target_expr(std::move(target_expr)),
        iterable(std::move(iterable)),
        filter(std::move(filter)) {}
};

struct GeneratorExpr final : Expr {
  ExprPtr result;
  std::string target;
  ExprPtr target_expr;
  ExprPtr iterable;
  ExprPtr filter;
  std::vector<CompClause> extra_clauses;
  GeneratorExpr(ExprPtr result, std::string target, ExprPtr iterable, ExprPtr filter = {})
      : result(std::move(result)),
        target(std::move(target)),
        target_expr(std::make_unique<NameExpr>(this->target)),
        iterable(std::move(iterable)),
        filter(std::move(filter)) {}
  GeneratorExpr(ExprPtr result, std::string target, ExprPtr target_expr, ExprPtr iterable, ExprPtr filter = {})
      : result(std::move(result)),
        target(std::move(target)),
        target_expr(std::move(target_expr)),
        iterable(std::move(iterable)),
        filter(std::move(filter)) {}
};

struct LambdaExpr final : Expr {
  struct Param {
    enum class Kind { PosOnly, PosOrKeyword, VarArgs, KeywordOnly, KwArgs };
    std::string name;
    Kind kind = Kind::PosOrKeyword;
    ExprPtr default_value;
  };
  std::vector<std::string> params;
  std::vector<Param> signature;
  ExprPtr body;
  LambdaExpr(std::vector<std::string> params, ExprPtr body)
      : params(std::move(params)), body(std::move(body)) {}
  LambdaExpr(std::vector<std::string> params, std::vector<Param> signature, ExprPtr body)
      : params(std::move(params)), signature(std::move(signature)), body(std::move(body)) {}
};

struct ExprStmt final : Stmt {
  ExprPtr expr;
  explicit ExprStmt(ExprPtr expr) : expr(std::move(expr)) {}
};

struct PassStmt final : Stmt {};

struct BreakStmt final : Stmt {};

struct ContinueStmt final : Stmt {};

struct DelStmt final : Stmt {
  ExprPtr target;
  explicit DelStmt(ExprPtr target) : target(std::move(target)) {}
};

struct AssertStmt final : Stmt {
  ExprPtr condition;
  ExprPtr message;
  AssertStmt(ExprPtr condition, ExprPtr message)
      : condition(std::move(condition)), message(std::move(message)) {}
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

struct AnnotatedAssignStmt final : Stmt {
  ExprPtr target;
  ExprPtr annotation;
  ExprPtr value;
  AnnotatedAssignStmt(ExprPtr target, ExprPtr annotation, ExprPtr value)
      : target(std::move(target)), annotation(std::move(annotation)), value(std::move(value)) {}
};

struct UnpackAssignStmt final : Stmt {
  ExprPtr target;
  ExprPtr value;
  UnpackAssignStmt(ExprPtr target, ExprPtr value) : target(std::move(target)), value(std::move(value)) {}
};

struct MultiAssignStmt final : Stmt {
  std::vector<ExprPtr> targets;
  ExprPtr value;
  MultiAssignStmt(std::vector<ExprPtr> targets, ExprPtr value)
      : targets(std::move(targets)), value(std::move(value)) {}
};

struct AugAssignStmt final : Stmt {
  ExprPtr target;
  std::string op;
  ExprPtr value;
  AugAssignStmt(ExprPtr target, std::string op, ExprPtr value)
      : target(std::move(target)), op(std::move(op)), value(std::move(value)) {}
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

struct ImportManyStmt final : Stmt {
  std::vector<ImportBinding> names;
  explicit ImportManyStmt(std::vector<ImportBinding> names) : names(std::move(names)) {}
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
  ExprPtr cause;
  RaiseStmt(ExprPtr value, ExprPtr cause = {}) : value(std::move(value)), cause(std::move(cause)) {}
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

struct ExceptHandler {
  ExprPtr type;
  std::string name;
  std::vector<StmtPtr> body;
};

struct TryExceptStmt final : Stmt {
  std::vector<StmtPtr> try_body;
  std::vector<ExceptHandler> handlers;
  std::vector<StmtPtr> else_body;
  std::vector<StmtPtr> finally_body;
};

struct WithStmt final : Stmt {
  ExprPtr manager;
  std::string target;
  std::vector<StmtPtr> body;
  bool is_async = false;
};

struct WhileStmt final : Stmt {
  ExprPtr condition;
  std::vector<StmtPtr> body;
  std::vector<StmtPtr> else_body;
};

struct ForStmt final : Stmt {
  std::string target;
  ExprPtr target_expr;
  ExprPtr iterable;
  std::vector<StmtPtr> body;
  std::vector<StmtPtr> else_body;
  bool is_async = false;
};

struct MatchCase {
  ExprPtr pattern;
  ExprPtr guard;
  std::string as_name;
  bool wildcard = false;
  std::vector<StmtPtr> body;
};

struct MatchStmt final : Stmt {
  ExprPtr subject;
  std::vector<MatchCase> cases;
};

struct FunctionDef final : Stmt {
  struct Param {
    enum class Kind { PosOnly, PosOrKeyword, VarArgs, KeywordOnly, KwArgs };
    std::string name;
    Kind kind = Kind::PosOrKeyword;
    ExprPtr default_value;
    ExprPtr annotation;
  };
  std::string name;
  std::vector<std::string> params;
  std::vector<Param> signature;
  std::vector<ExprPtr> decorators;
  ExprPtr return_annotation;
  std::vector<StmtPtr> body;
  bool is_async = false;
};

struct ClassDef final : Stmt {
  std::string name;
  std::vector<ExprPtr> bases;
  std::vector<std::pair<std::string, ExprPtr>> keywords;
  std::vector<ExprPtr> decorators;
  std::vector<StmtPtr> body;
};

struct Module {
  std::vector<StmtPtr> body;
};

} // namespace xlang3::ast
