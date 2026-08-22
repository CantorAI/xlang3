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
#include "xlang3/sema.h"
#include "xlang3/builtin_methods.h"
#include "xlang3/parser.h"
#include "xlang3/python_names.h"
#include "xlang3/scope_analysis.h"

#include "xlang_module_globals.h"

#include <algorithm>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace xlang3 {

namespace {

struct ClassInfo {
  std::vector<std::string> slot_names;
  std::unordered_map<std::string, uint32_t> slots;
  std::vector<std::string> match_args;
};

enum class KnownValueType : uint8_t {
  Unknown,
  String,
};

int64_t parse_integer_literal(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (char ch : text) {
    if (ch != '_') {
      normalized.push_back(ch);
    }
  }

  int base = 10;
  size_t start = 0;
  if (normalized.size() >= 2 && normalized[0] == '0') {
    const char marker = normalized[1];
    if (marker == 'x' || marker == 'X') {
      base = 16;
      start = 2;
    } else if (marker == 'b' || marker == 'B') {
      base = 2;
      start = 2;
    } else if (marker == 'o' || marker == 'O') {
      base = 8;
      start = 2;
    }
  }

  int64_t value = 0;
  for (size_t i = start; i < normalized.size(); ++i) {
    const char ch = normalized[i];
    int digit = -1;
    if (ch >= '0' && ch <= '9') {
      digit = ch - '0';
    } else if (ch >= 'a' && ch <= 'f') {
      digit = ch - 'a' + 10;
    } else if (ch >= 'A' && ch <= 'F') {
      digit = ch - 'A' + 10;
    }
    if (digit < 0 || digit >= base) {
      break;
    }
    value = value * base + digit;
  }
  return value;
}

void add_slot_name(const std::string& name, std::vector<std::string>& slots, std::unordered_set<std::string>& seen) {
  if (seen.insert(name).second) {
    slots.push_back(name);
  }
}

void collect_assignment_names(const ast::Expr& target, std::vector<std::string>& names, std::unordered_set<std::string>& seen) {
  if (auto* name = dynamic_cast<const ast::NameExpr*>(&target)) {
    if (seen.insert(name->name).second) {
      names.push_back(name->name);
    }
    return;
  }
  if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&target)) {
    for (const auto& item : tuple->items) {
      collect_assignment_names(*item, names, seen);
    }
    return;
  }
  if (auto* list = dynamic_cast<const ast::ListExpr*>(&target)) {
    for (const auto& item : list->items) {
      collect_assignment_names(*item, names, seen);
    }
    return;
  }
  if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&target)) {
    collect_assignment_names(*starred->expr, names, seen);
  }
}

std::vector<std::string> assignment_names(const ast::Expr& target) {
  std::vector<std::string> names;
  std::unordered_set<std::string> seen;
  collect_assignment_names(target, names, seen);
  return names;
}

bool append_literal_slot_name(const ast::Expr& expr, std::vector<std::string>& slots, std::unordered_set<std::string>& seen) {
  auto* literal = dynamic_cast<const ast::LiteralExpr*>(&expr);
  if (literal == nullptr || literal->kind != ast::LiteralExpr::Kind::String) {
    return false;
  }
  if (literal->text == "__dict__" || literal->text == "__weakref__") {
    return true;
  }
  if (seen.insert(literal->text).second) {
    slots.push_back(literal->text);
  }
  return true;
}

bool collect_literal_slots_from_expr(const ast::Expr& expr, std::vector<std::string>& slots, std::unordered_set<std::string>& seen) {
  if (append_literal_slot_name(expr, slots, seen)) {
    return true;
  }
  if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    for (const auto& item : tuple->items) {
      if (!append_literal_slot_name(*item, slots, seen)) {
        return false;
      }
    }
    return true;
  }
  if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
    for (const auto& item : list->items) {
      if (!append_literal_slot_name(*item, slots, seen)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

bool append_literal_string_name(const ast::Expr& expr, std::vector<std::string>& names) {
  auto* literal = dynamic_cast<const ast::LiteralExpr*>(&expr);
  if (literal == nullptr || literal->kind != ast::LiteralExpr::Kind::String) {
    return false;
  }
  names.push_back(literal->text);
  return true;
}

bool collect_literal_string_sequence(const ast::Expr& expr, std::vector<std::string>& names) {
  if (append_literal_string_name(expr, names)) {
    return true;
  }
  if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    for (const auto& item : tuple->items) {
      if (!append_literal_string_name(*item, names)) {
        return false;
      }
    }
    return true;
  }
  if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
    for (const auto& item : list->items) {
      if (!append_literal_string_name(*item, names)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

ir::ParamKind lower_param_kind(ast::FunctionDef::Param::Kind kind) {
  switch (kind) {
    case ast::FunctionDef::Param::Kind::PosOnly:
      return ir::ParamKind::PosOnly;
    case ast::FunctionDef::Param::Kind::PosOrKeyword:
      return ir::ParamKind::PosOrKeyword;
    case ast::FunctionDef::Param::Kind::VarArgs:
      return ir::ParamKind::VarArgs;
    case ast::FunctionDef::Param::Kind::KeywordOnly:
      return ir::ParamKind::KeywordOnly;
    case ast::FunctionDef::Param::Kind::KwArgs:
      return ir::ParamKind::KwArgs;
  }
  return ir::ParamKind::PosOrKeyword;
}

std::vector<ir::Param> lower_signature_metadata(const ast::FunctionDef& fn) {
  std::vector<ir::Param> params;
  if (fn.signature.empty()) {
    params.reserve(fn.params.size());
    for (const auto& name : fn.params) {
      params.push_back(ir::Param{name, ir::ParamKind::PosOrKeyword, UINT32_MAX});
    }
    return params;
  }
  params.reserve(fn.signature.size());
  for (const auto& param : fn.signature) {
    params.push_back(ir::Param{param.name, lower_param_kind(param.kind), UINT32_MAX});
  }
  return params;
}

ast::ExprPtr clone_expr(const ast::Expr& expr) {
  if (auto* lit = dynamic_cast<const ast::LiteralExpr*>(&expr)) {
    auto out = std::make_unique<ast::LiteralExpr>(lit->kind, lit->text);
    out->bool_value = lit->bool_value;
    return out;
  }
  if (auto* fstring = dynamic_cast<const ast::FStringExpr*>(&expr)) {
    return std::make_unique<ast::FStringExpr>(fstring->parts);
  }
  if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
    return std::make_unique<ast::NameExpr>(name->name);
  }
  if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
    return std::make_unique<ast::UnaryExpr>(unary->op, clone_expr(*unary->expr));
  }
  if (auto* await = dynamic_cast<const ast::AwaitExpr*>(&expr)) {
    return std::make_unique<ast::AwaitExpr>(clone_expr(*await->expr));
  }
  if (auto* yield = dynamic_cast<const ast::YieldExpr*>(&expr)) {
    return std::make_unique<ast::YieldExpr>(clone_expr(*yield->expr), yield->from);
  }
  if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    return std::make_unique<ast::BinaryExpr>(clone_expr(*binary->lhs), binary->op, clone_expr(*binary->rhs));
  }
  if (auto* chain = dynamic_cast<const ast::CompareChainExpr*>(&expr)) {
    std::vector<std::pair<std::string, ast::ExprPtr>> comparisons;
    for (const auto& comparison : chain->comparisons) {
      comparisons.push_back(std::make_pair(comparison.first, clone_expr(*comparison.second)));
    }
    return std::make_unique<ast::CompareChainExpr>(clone_expr(*chain->first), std::move(comparisons));
  }
  if (auto* conditional = dynamic_cast<const ast::ConditionalExpr*>(&expr)) {
    return std::make_unique<ast::ConditionalExpr>(
        clone_expr(*conditional->then_expr),
        clone_expr(*conditional->condition),
        clone_expr(*conditional->else_expr));
  }
  if (auto* named = dynamic_cast<const ast::NamedExpr*>(&expr)) {
    return std::make_unique<ast::NamedExpr>(named->name, clone_expr(*named->value));
  }
  if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&expr)) {
    return std::make_unique<ast::StarredExpr>(clone_expr(*starred->expr));
  }
  if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    if (!call->call_args.empty()) {
      std::vector<ast::CallExpr::Arg> args;
      for (const auto& arg : call->call_args) {
        ast::CallExpr::Arg cloned;
        cloned.name = arg.name;
        cloned.star = arg.star;
        cloned.kw_star = arg.kw_star;
        cloned.value = clone_expr(*arg.value);
        args.push_back(std::move(cloned));
      }
      return std::make_unique<ast::CallExpr>(clone_expr(*call->callee), std::move(args));
    }
    std::vector<ast::ExprPtr> args;
    for (const auto& arg : call->args) {
      args.push_back(clone_expr(*arg));
    }
    return std::make_unique<ast::CallExpr>(clone_expr(*call->callee), std::move(args));
  }
  if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
    return std::make_unique<ast::SubscriptExpr>(clone_expr(*subscript->object), clone_expr(*subscript->index));
  }
  if (auto* slice = dynamic_cast<const ast::SliceExpr*>(&expr)) {
    return std::make_unique<ast::SliceExpr>(
        clone_expr(*slice->start),
        clone_expr(*slice->stop),
        clone_expr(*slice->step));
  }
  if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
    return std::make_unique<ast::AttrExpr>(clone_expr(*attr->object), attr->name);
  }
  if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    std::vector<ast::ExprPtr> items;
    for (const auto& item : tuple->items) items.push_back(clone_expr(*item));
    return std::make_unique<ast::TupleExpr>(std::move(items));
  }
  if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
    std::vector<ast::ExprPtr> items;
    for (const auto& item : list->items) items.push_back(clone_expr(*item));
    return std::make_unique<ast::ListExpr>(std::move(items));
  }
  if (auto* comp = dynamic_cast<const ast::ListCompExpr*>(&expr)) {
    auto out = std::make_unique<ast::ListCompExpr>(
        clone_expr(*comp->result),
        comp->target,
        clone_expr(*comp->target_expr),
        clone_expr(*comp->iterable),
        comp->filter == nullptr ? ast::ExprPtr{} : clone_expr(*comp->filter));
    for (const auto& clause : comp->extra_clauses) {
      ast::CompClause cloned;
      cloned.target = clause.target;
      cloned.target_expr = clone_expr(*clause.target_expr);
      cloned.iterable = clone_expr(*clause.iterable);
      cloned.filter = clause.filter == nullptr ? ast::ExprPtr{} : clone_expr(*clause.filter);
      out->extra_clauses.push_back(std::move(cloned));
    }
    return out;
  }
  if (auto* comp = dynamic_cast<const ast::DictCompExpr*>(&expr)) {
    auto out = std::make_unique<ast::DictCompExpr>(
        clone_expr(*comp->key),
        clone_expr(*comp->value),
        comp->target,
        clone_expr(*comp->target_expr),
        clone_expr(*comp->iterable),
        comp->filter == nullptr ? ast::ExprPtr{} : clone_expr(*comp->filter));
    for (const auto& clause : comp->extra_clauses) {
      ast::CompClause cloned;
      cloned.target = clause.target;
      cloned.target_expr = clone_expr(*clause.target_expr);
      cloned.iterable = clone_expr(*clause.iterable);
      cloned.filter = clause.filter == nullptr ? ast::ExprPtr{} : clone_expr(*clause.filter);
      out->extra_clauses.push_back(std::move(cloned));
    }
    return out;
  }
  if (auto* comp = dynamic_cast<const ast::SetCompExpr*>(&expr)) {
    auto out = std::make_unique<ast::SetCompExpr>(
        clone_expr(*comp->result),
        comp->target,
        clone_expr(*comp->target_expr),
        clone_expr(*comp->iterable),
        comp->filter == nullptr ? ast::ExprPtr{} : clone_expr(*comp->filter));
    for (const auto& clause : comp->extra_clauses) {
      ast::CompClause cloned;
      cloned.target = clause.target;
      cloned.target_expr = clone_expr(*clause.target_expr);
      cloned.iterable = clone_expr(*clause.iterable);
      cloned.filter = clause.filter == nullptr ? ast::ExprPtr{} : clone_expr(*clause.filter);
      out->extra_clauses.push_back(std::move(cloned));
    }
    return out;
  }
  if (auto* comp = dynamic_cast<const ast::GeneratorExpr*>(&expr)) {
    auto out = std::make_unique<ast::GeneratorExpr>(
        clone_expr(*comp->result),
        comp->target,
        clone_expr(*comp->target_expr),
        clone_expr(*comp->iterable),
        comp->filter == nullptr ? ast::ExprPtr{} : clone_expr(*comp->filter));
    for (const auto& clause : comp->extra_clauses) {
      ast::CompClause cloned;
      cloned.target = clause.target;
      cloned.target_expr = clone_expr(*clause.target_expr);
      cloned.iterable = clone_expr(*clause.iterable);
      cloned.filter = clause.filter == nullptr ? ast::ExprPtr{} : clone_expr(*clause.filter);
      out->extra_clauses.push_back(std::move(cloned));
    }
    return out;
  }
  return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::None);
}

bool expr_contains_yield(const ast::Expr& expr) {
  if (dynamic_cast<const ast::YieldExpr*>(&expr) != nullptr) {
    return true;
  }
  if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
    return expr_contains_yield(*unary->expr);
  }
  if (auto* await = dynamic_cast<const ast::AwaitExpr*>(&expr)) {
    return expr_contains_yield(*await->expr);
  }
  if (dynamic_cast<const ast::FStringExpr*>(&expr) != nullptr) {
    return false;
  }
  if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    return expr_contains_yield(*binary->lhs) || expr_contains_yield(*binary->rhs);
  }
  if (auto* chain = dynamic_cast<const ast::CompareChainExpr*>(&expr)) {
    if (expr_contains_yield(*chain->first)) return true;
    for (const auto& comparison : chain->comparisons) {
      if (expr_contains_yield(*comparison.second)) return true;
    }
    return false;
  }
  if (auto* conditional = dynamic_cast<const ast::ConditionalExpr*>(&expr)) {
    return expr_contains_yield(*conditional->then_expr) ||
           expr_contains_yield(*conditional->condition) ||
           expr_contains_yield(*conditional->else_expr);
  }
  if (auto* named = dynamic_cast<const ast::NamedExpr*>(&expr)) {
    return expr_contains_yield(*named->value);
  }
  if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&expr)) {
    return expr_contains_yield(*starred->expr);
  }
  if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    if (expr_contains_yield(*call->callee)) return true;
    for (const auto& arg : call->args) {
      if (expr_contains_yield(*arg)) return true;
    }
    for (const auto& arg : call->call_args) {
      if (expr_contains_yield(*arg.value)) return true;
    }
    return false;
  }
  if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
    return expr_contains_yield(*subscript->object) || expr_contains_yield(*subscript->index);
  }
  if (auto* slice = dynamic_cast<const ast::SliceExpr*>(&expr)) {
    return expr_contains_yield(*slice->start) || expr_contains_yield(*slice->stop) || expr_contains_yield(*slice->step);
  }
  if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
    return expr_contains_yield(*attr->object);
  }
  if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    for (const auto& item : tuple->items) {
      if (expr_contains_yield(*item)) return true;
    }
    return false;
  }
  if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
    for (const auto& item : list->items) {
      if (expr_contains_yield(*item)) return true;
    }
    return false;
  }
  if (auto* comp = dynamic_cast<const ast::ListCompExpr*>(&expr)) {
    if (expr_contains_yield(*comp->result) ||
        expr_contains_yield(*comp->iterable) ||
        (comp->filter != nullptr && expr_contains_yield(*comp->filter))) {
      return true;
    }
    for (const auto& clause : comp->extra_clauses) {
      if (expr_contains_yield(*clause.iterable) ||
          (clause.filter != nullptr && expr_contains_yield(*clause.filter))) {
        return true;
      }
    }
    return false;
  }
  if (auto* comp = dynamic_cast<const ast::DictCompExpr*>(&expr)) {
    if (expr_contains_yield(*comp->key) ||
        expr_contains_yield(*comp->value) ||
        expr_contains_yield(*comp->iterable) ||
        (comp->filter != nullptr && expr_contains_yield(*comp->filter))) {
      return true;
    }
    for (const auto& clause : comp->extra_clauses) {
      if (expr_contains_yield(*clause.iterable) ||
          (clause.filter != nullptr && expr_contains_yield(*clause.filter))) {
        return true;
      }
    }
    return false;
  }
  if (auto* comp = dynamic_cast<const ast::SetCompExpr*>(&expr)) {
    if (expr_contains_yield(*comp->result) ||
        expr_contains_yield(*comp->iterable) ||
        (comp->filter != nullptr && expr_contains_yield(*comp->filter))) {
      return true;
    }
    for (const auto& clause : comp->extra_clauses) {
      if (expr_contains_yield(*clause.iterable) ||
          (clause.filter != nullptr && expr_contains_yield(*clause.filter))) {
        return true;
      }
    }
    return false;
  }
  if (auto* comp = dynamic_cast<const ast::GeneratorExpr*>(&expr)) {
    if (expr_contains_yield(*comp->result) ||
        expr_contains_yield(*comp->iterable) ||
        (comp->filter != nullptr && expr_contains_yield(*comp->filter))) {
      return true;
    }
    for (const auto& clause : comp->extra_clauses) {
      if (expr_contains_yield(*clause.iterable) ||
          (clause.filter != nullptr && expr_contains_yield(*clause.filter))) {
        return true;
      }
    }
    return false;
  }
  return false;
}

bool stmt_contains_yield(const ast::Stmt& stmt);

bool body_contains_yield(const std::vector<ast::StmtPtr>& body) {
  for (const auto& stmt : body) {
    if (stmt_contains_yield(*stmt)) return true;
  }
  return false;
}

bool stmt_contains_yield(const ast::Stmt& stmt) {
  if (auto* expr = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
    return expr_contains_yield(*expr->expr);
  }
  if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
    return expr_contains_yield(*assign->value);
  }
  if (auto* assign = dynamic_cast<const ast::AnnotatedAssignStmt*>(&stmt)) {
    if (assign->value == nullptr) return false;
    return expr_contains_yield(*assign->target) || expr_contains_yield(*assign->value);
  }
  if (auto* assign = dynamic_cast<const ast::MultiAssignStmt*>(&stmt)) {
    if (expr_contains_yield(*assign->value)) return true;
    for (const auto& target : assign->targets) {
      if (expr_contains_yield(*target)) return true;
    }
    return false;
  }
  if (auto* assign = dynamic_cast<const ast::SubscriptAssignStmt*>(&stmt)) {
    return expr_contains_yield(*assign->object) || expr_contains_yield(*assign->index) ||
           expr_contains_yield(*assign->value);
  }
  if (auto* assign = dynamic_cast<const ast::AttrAssignStmt*>(&stmt)) {
    return expr_contains_yield(*assign->object) || expr_contains_yield(*assign->value);
  }
  if (auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
    return ret->value != nullptr && expr_contains_yield(*ret->value);
  }
  if (auto* ifs = dynamic_cast<const ast::IfStmt*>(&stmt)) {
    return expr_contains_yield(*ifs->condition) || body_contains_yield(ifs->then_body) ||
           body_contains_yield(ifs->else_body);
  }
  if (auto* loop = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
    return expr_contains_yield(*loop->condition) || body_contains_yield(loop->body);
  }
  if (auto* loop = dynamic_cast<const ast::ForStmt*>(&stmt)) {
    return expr_contains_yield(*loop->iterable) || body_contains_yield(loop->body);
  }
  if (auto* block = dynamic_cast<const ast::WithStmt*>(&stmt)) {
    return expr_contains_yield(*block->manager) || body_contains_yield(block->body);
  }
  if (auto* block = dynamic_cast<const ast::TryExceptStmt*>(&stmt)) {
    if (body_contains_yield(block->try_body) || body_contains_yield(block->else_body) ||
        body_contains_yield(block->finally_body)) {
      return true;
    }
    for (const auto& handler : block->handlers) {
      if ((handler.type != nullptr && expr_contains_yield(*handler.type)) || body_contains_yield(handler.body)) {
        return true;
      }
    }
    return false;
  }
  if (auto* match = dynamic_cast<const ast::MatchStmt*>(&stmt)) {
    if (expr_contains_yield(*match->subject)) return true;
    for (const auto& match_case : match->cases) {
      if ((match_case.pattern != nullptr && expr_contains_yield(*match_case.pattern)) ||
          body_contains_yield(match_case.body)) {
        return true;
      }
    }
  }
  return false;
}

void collect_self_attr_slots_expr(
    const ast::Expr& expr,
    const std::string& self_name,
    std::vector<std::string>& slots,
    std::unordered_set<std::string>& seen) {
  if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
    collect_self_attr_slots_expr(*attr->object, self_name, slots, seen);
    return;
  }
  if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
    collect_self_attr_slots_expr(*unary->expr, self_name, slots, seen);
    return;
  }
  if (auto* await = dynamic_cast<const ast::AwaitExpr*>(&expr)) {
    collect_self_attr_slots_expr(*await->expr, self_name, slots, seen);
    return;
  }
  if (auto* yield = dynamic_cast<const ast::YieldExpr*>(&expr)) {
    collect_self_attr_slots_expr(*yield->expr, self_name, slots, seen);
    return;
  }
  if (dynamic_cast<const ast::FStringExpr*>(&expr) != nullptr) {
    return;
  }
  if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    collect_self_attr_slots_expr(*binary->lhs, self_name, slots, seen);
    collect_self_attr_slots_expr(*binary->rhs, self_name, slots, seen);
    return;
  }
  if (auto* chain = dynamic_cast<const ast::CompareChainExpr*>(&expr)) {
    collect_self_attr_slots_expr(*chain->first, self_name, slots, seen);
    for (const auto& comparison : chain->comparisons) {
      collect_self_attr_slots_expr(*comparison.second, self_name, slots, seen);
    }
    return;
  }
  if (auto* conditional = dynamic_cast<const ast::ConditionalExpr*>(&expr)) {
    collect_self_attr_slots_expr(*conditional->then_expr, self_name, slots, seen);
    collect_self_attr_slots_expr(*conditional->condition, self_name, slots, seen);
    collect_self_attr_slots_expr(*conditional->else_expr, self_name, slots, seen);
    return;
  }
  if (auto* named = dynamic_cast<const ast::NamedExpr*>(&expr)) {
    collect_self_attr_slots_expr(*named->value, self_name, slots, seen);
    return;
  }
  if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&expr)) {
    collect_self_attr_slots_expr(*starred->expr, self_name, slots, seen);
    return;
  }
  if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    collect_self_attr_slots_expr(*call->callee, self_name, slots, seen);
    for (const auto& arg : call->args) {
      collect_self_attr_slots_expr(*arg, self_name, slots, seen);
    }
    for (const auto& arg : call->call_args) {
      collect_self_attr_slots_expr(*arg.value, self_name, slots, seen);
    }
    return;
  }
  if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
    collect_self_attr_slots_expr(*subscript->object, self_name, slots, seen);
    collect_self_attr_slots_expr(*subscript->index, self_name, slots, seen);
    return;
  }
  if (auto* slice = dynamic_cast<const ast::SliceExpr*>(&expr)) {
    collect_self_attr_slots_expr(*slice->start, self_name, slots, seen);
    collect_self_attr_slots_expr(*slice->stop, self_name, slots, seen);
    collect_self_attr_slots_expr(*slice->step, self_name, slots, seen);
    return;
  }
  if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    for (const auto& item : tuple->items) {
      collect_self_attr_slots_expr(*item, self_name, slots, seen);
    }
    return;
  }
  if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
    for (const auto& item : list->items) {
      collect_self_attr_slots_expr(*item, self_name, slots, seen);
    }
    return;
  }
  if (auto* set = dynamic_cast<const ast::SetExpr*>(&expr)) {
    for (const auto& item : set->items) {
      collect_self_attr_slots_expr(*item, self_name, slots, seen);
    }
    return;
  }
  if (auto* dict = dynamic_cast<const ast::DictExpr*>(&expr)) {
    for (const auto& entry : dict->entries) {
      collect_self_attr_slots_expr(*entry.first, self_name, slots, seen);
      collect_self_attr_slots_expr(*entry.second, self_name, slots, seen);
    }
    return;
  }
  if (auto* comp = dynamic_cast<const ast::ListCompExpr*>(&expr)) {
    collect_self_attr_slots_expr(*comp->result, self_name, slots, seen);
    collect_self_attr_slots_expr(*comp->iterable, self_name, slots, seen);
    if (comp->filter) {
      collect_self_attr_slots_expr(*comp->filter, self_name, slots, seen);
    }
    for (const auto& clause : comp->extra_clauses) {
      collect_self_attr_slots_expr(*clause.iterable, self_name, slots, seen);
      if (clause.filter) {
        collect_self_attr_slots_expr(*clause.filter, self_name, slots, seen);
      }
    }
    return;
  }
  if (auto* comp = dynamic_cast<const ast::DictCompExpr*>(&expr)) {
    collect_self_attr_slots_expr(*comp->key, self_name, slots, seen);
    collect_self_attr_slots_expr(*comp->value, self_name, slots, seen);
    collect_self_attr_slots_expr(*comp->iterable, self_name, slots, seen);
    if (comp->filter) {
      collect_self_attr_slots_expr(*comp->filter, self_name, slots, seen);
    }
    for (const auto& clause : comp->extra_clauses) {
      collect_self_attr_slots_expr(*clause.iterable, self_name, slots, seen);
      if (clause.filter) {
        collect_self_attr_slots_expr(*clause.filter, self_name, slots, seen);
      }
    }
    return;
  }
  if (auto* comp = dynamic_cast<const ast::SetCompExpr*>(&expr)) {
    collect_self_attr_slots_expr(*comp->result, self_name, slots, seen);
    collect_self_attr_slots_expr(*comp->iterable, self_name, slots, seen);
    if (comp->filter) {
      collect_self_attr_slots_expr(*comp->filter, self_name, slots, seen);
    }
    for (const auto& clause : comp->extra_clauses) {
      collect_self_attr_slots_expr(*clause.iterable, self_name, slots, seen);
      if (clause.filter) {
        collect_self_attr_slots_expr(*clause.filter, self_name, slots, seen);
      }
    }
    return;
  }
  if (auto* comp = dynamic_cast<const ast::GeneratorExpr*>(&expr)) {
    collect_self_attr_slots_expr(*comp->result, self_name, slots, seen);
    collect_self_attr_slots_expr(*comp->iterable, self_name, slots, seen);
    if (comp->filter) {
      collect_self_attr_slots_expr(*comp->filter, self_name, slots, seen);
    }
    for (const auto& clause : comp->extra_clauses) {
      collect_self_attr_slots_expr(*clause.iterable, self_name, slots, seen);
      if (clause.filter) {
        collect_self_attr_slots_expr(*clause.filter, self_name, slots, seen);
      }
    }
    return;
  }
  if (auto* lambda = dynamic_cast<const ast::LambdaExpr*>(&expr)) {
    collect_self_attr_slots_expr(*lambda->body, self_name, slots, seen);
  }
}

void collect_self_attr_slots_stmt(
    const ast::Stmt& stmt,
    const std::string& self_name,
    std::vector<std::string>& slots,
    std::unordered_set<std::string>& seen) {
  if (auto* assign = dynamic_cast<const ast::AttrAssignStmt*>(&stmt)) {
    if (auto* name = dynamic_cast<const ast::NameExpr*>(assign->object.get())) {
      if (name->name == self_name) {
        add_slot_name(assign->name, slots, seen);
      }
    }
    collect_self_attr_slots_expr(*assign->object, self_name, slots, seen);
    collect_self_attr_slots_expr(*assign->value, self_name, slots, seen);
    return;
  }
  if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*assign->value, self_name, slots, seen);
    return;
  }
  if (auto* assign = dynamic_cast<const ast::AnnotatedAssignStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*assign->target, self_name, slots, seen);
    if (assign->value != nullptr) {
      collect_self_attr_slots_expr(*assign->value, self_name, slots, seen);
    }
    return;
  }
  if (auto* assign = dynamic_cast<const ast::SubscriptAssignStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*assign->object, self_name, slots, seen);
    collect_self_attr_slots_expr(*assign->index, self_name, slots, seen);
    collect_self_attr_slots_expr(*assign->value, self_name, slots, seen);
    return;
  }
  if (auto* del = dynamic_cast<const ast::DelStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*del->target, self_name, slots, seen);
    return;
  }
  if (auto* assert_stmt = dynamic_cast<const ast::AssertStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*assert_stmt->condition, self_name, slots, seen);
    if (assert_stmt->message != nullptr) {
      collect_self_attr_slots_expr(*assert_stmt->message, self_name, slots, seen);
    }
    return;
  }
  if (auto* expr = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*expr->expr, self_name, slots, seen);
    return;
  }
  if (auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*ret->value, self_name, slots, seen);
    return;
  }
  if (auto* raise = dynamic_cast<const ast::RaiseStmt*>(&stmt)) {
    if (raise->value != nullptr) {
      collect_self_attr_slots_expr(*raise->value, self_name, slots, seen);
    }
    if (raise->cause != nullptr) {
      collect_self_attr_slots_expr(*raise->cause, self_name, slots, seen);
    }
    return;
  }
  if (auto* ifs = dynamic_cast<const ast::IfStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*ifs->condition, self_name, slots, seen);
    for (const auto& child : ifs->then_body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    for (const auto& child : ifs->else_body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    return;
  }
  if (auto* loop = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*loop->condition, self_name, slots, seen);
    for (const auto& child : loop->body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    return;
  }
  if (auto* loop = dynamic_cast<const ast::ForStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*loop->iterable, self_name, slots, seen);
    for (const auto& child : loop->body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    return;
  }
  if (auto* try_except = dynamic_cast<const ast::TryExceptStmt*>(&stmt)) {
    for (const auto& child : try_except->try_body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    for (const auto& handler : try_except->handlers) {
      if (handler.type != nullptr) {
        collect_self_attr_slots_expr(*handler.type, self_name, slots, seen);
      }
      for (const auto& child : handler.body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    }
    for (const auto& child : try_except->else_body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    for (const auto& child : try_except->finally_body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    return;
  }
  if (auto* with = dynamic_cast<const ast::WithStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*with->manager, self_name, slots, seen);
    for (const auto& child : with->body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    return;
  }
  if (auto* match = dynamic_cast<const ast::MatchStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*match->subject, self_name, slots, seen);
    for (const auto& match_case : match->cases) {
      if (match_case.pattern != nullptr) {
        collect_self_attr_slots_expr(*match_case.pattern, self_name, slots, seen);
      }
      for (const auto& child : match_case.body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    }
  }
}

void collect_global_names(const std::vector<ast::StmtPtr>& body, sema::NameSet& names) {
  for (const auto& stmt : body) {
    if (auto* global = dynamic_cast<const ast::GlobalStmt*>(stmt.get())) {
      for (const auto& name : global->names) {
        names.insert(name);
      }
    } else if (auto* ifs = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
      collect_global_names(ifs->then_body, names);
      collect_global_names(ifs->else_body, names);
    } else if (auto* try_except = dynamic_cast<const ast::TryExceptStmt*>(stmt.get())) {
      collect_global_names(try_except->try_body, names);
      for (const auto& handler : try_except->handlers) {
        collect_global_names(handler.body, names);
      }
      collect_global_names(try_except->else_body, names);
      collect_global_names(try_except->finally_body, names);
    } else if (auto* with = dynamic_cast<const ast::WithStmt*>(stmt.get())) {
      collect_global_names(with->body, names);
    } else if (auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
      collect_global_names(loop->body, names);
    } else if (auto* loop = dynamic_cast<const ast::ForStmt*>(stmt.get())) {
      collect_global_names(loop->body, names);
    } else if (auto* match = dynamic_cast<const ast::MatchStmt*>(stmt.get())) {
      for (const auto& match_case : match->cases) {
        collect_global_names(match_case.body, names);
      }
    }
  }
}

class FunctionLowerer {
public:
  FunctionLowerer(
      ir::Module& module,
      std::string name,
      std::vector<std::string> params,
      std::vector<ir::Param> signature,
      std::vector<std::string> free_vars,
      const std::vector<ast::StmtPtr>& body,
      bool is_generator,
      uint32_t first_line = 0,
      bool is_module = false,
      std::string instance_slot_self = {},
      std::unordered_map<std::string, uint32_t> instance_slots = {},
      std::unordered_map<std::string, ClassInfo> class_infos = {},
      std::unordered_map<std::string, uint32_t> module_global_slots = {},
      std::unordered_set<uint32_t> imported_module_slots = {},
      std::string qualname_prefix = {})
      : module_(module),
        is_module_(is_module),
        instance_slot_self_(std::move(instance_slot_self)),
        instance_slots_(std::move(instance_slots)),
        class_infos_(std::move(class_infos)),
        module_global_slots_(std::move(module_global_slots)),
        imported_module_slots_(std::move(imported_module_slots)),
        qualname_prefix_(std::move(qualname_prefix)) {
    fn_.name = std::move(name);
    fn_.qualname = qualname_prefix_.empty() ? fn_.name : qualname_prefix_ + "." + fn_.name;
    fn_.is_generator = is_generator;
    fn_.first_line = first_line;
    fn_.params = std::move(params);
    fn_.signature = std::move(signature);
    fn_.free_vars = std::move(free_vars);
    for (size_t i = 0; i < fn_.free_vars.size(); ++i) {
      free_indices_[fn_.free_vars[i]] = static_cast<uint32_t>(i);
    }

    const auto locals = is_module_ ? std::vector<std::string>{} : sema::local_names_for(fn_.params, body);
    for (const auto& local : locals) {
      ensure_local(local);
    }
    local_name_set_.insert(locals.begin(), locals.end());
    collect_global_names(body, global_names_);

    prepare_captured_locals(body);
  }

  ir::Function finish() {
    emit_return_none();
    return std::move(fn_);
  }

  void lower_body(const std::vector<ast::StmtPtr>& body) {
    for (const auto& stmt : body) {
      lower_stmt(*stmt);
    }
  }

private:
  uint32_t new_reg() {
    return fn_.register_count++;
  }

  uint32_t add_const(Value value) {
    fn_.constants.push_back(std::move(value));
    return static_cast<uint32_t>(fn_.constants.size() - 1);
  }

  uint32_t add_name(const std::string& name) {
    auto it = name_ids_.find(name);
    if (it != name_ids_.end()) {
      return it->second;
    }
    const auto id = static_cast<uint32_t>(fn_.names.size());
    fn_.names.push_back(name);
    name_ids_[name] = id;
    return id;
  }

  uint32_t add_raw_block(std::string language, std::string provider, std::string body) {
    fn_.raw_blocks.push_back(ir::Function::RawBlock{std::move(language), std::move(provider), std::move(body)});
    return static_cast<uint32_t>(fn_.raw_blocks.size() - 1);
  }

  uint32_t add_call_args(std::vector<uint32_t> args) {
    fn_.call_args.push_back(std::move(args));
    return static_cast<uint32_t>(fn_.call_args.size() - 1);
  }

  uint32_t add_call_spec(ir::CallSpec spec) {
    fn_.call_specs.push_back(std::move(spec));
    return static_cast<uint32_t>(fn_.call_specs.size() - 1);
  }

  uint32_t add_function_defaults(std::vector<uint32_t> defaults) {
    fn_.function_defaults.push_back(std::move(defaults));
    return static_cast<uint32_t>(fn_.function_defaults.size() - 1);
  }

  uint32_t add_function_annotations(std::vector<std::pair<std::string, uint32_t>> annotations) {
    fn_.function_annotations.push_back(std::move(annotations));
    return static_cast<uint32_t>(fn_.function_annotations.size() - 1);
  }

  uint32_t add_function_kwdefaults(std::vector<std::pair<std::string, uint32_t>> defaults) {
    fn_.function_kwdefaults.push_back(std::move(defaults));
    return static_cast<uint32_t>(fn_.function_kwdefaults.size() - 1);
  }

  uint32_t emit_type_params_tuple(const std::vector<std::string>& type_params) {
    std::vector<uint32_t> items;
    items.reserve(type_params.size());
    for (const auto& type_param : type_params) {
      const auto reg = new_reg();
      emit(ir::Op::LoadConst, reg, add_const(Value::type_param(type_param)));
      items.push_back(reg);
    }
    const auto tuple = new_reg();
    emit(ir::Op::MakeTuple, tuple, add_tuple_items(std::move(items)));
    return tuple;
  }

  uint32_t add_tuple_items(std::vector<uint32_t> items) {
    fn_.tuple_items.push_back(std::move(items));
    return static_cast<uint32_t>(fn_.tuple_items.size() - 1);
  }

  uint32_t add_list_items(std::vector<uint32_t> items) {
    fn_.list_items.push_back(std::move(items));
    return static_cast<uint32_t>(fn_.list_items.size() - 1);
  }

  uint32_t add_set_items(std::vector<uint32_t> items) {
    fn_.set_items.push_back(std::move(items));
    return static_cast<uint32_t>(fn_.set_items.size() - 1);
  }

  uint32_t add_dict_items(std::vector<std::pair<uint32_t, uint32_t>> items) {
    fn_.dict_items.push_back(std::move(items));
    return static_cast<uint32_t>(fn_.dict_items.size() - 1);
  }

  uint32_t add_function_closure(std::vector<uint32_t> cells) {
    fn_.function_closures.push_back(std::move(cells));
    return static_cast<uint32_t>(fn_.function_closures.size() - 1);
  }

  uint32_t add_class_attrs(std::vector<std::pair<std::string, uint32_t>> attrs) {
    fn_.class_attrs.push_back(std::move(attrs));
    return static_cast<uint32_t>(fn_.class_attrs.size() - 1);
  }

  uint32_t add_class_instance_slots(std::vector<std::string> slots) {
    fn_.class_instance_slots.push_back(std::move(slots));
    return static_cast<uint32_t>(fn_.class_instance_slots.size() - 1);
  }

  uint32_t add_range_spec(uint32_t stop_const, uint32_t step_const) {
    fn_.range_specs.push_back(std::make_pair(stop_const, step_const));
    return static_cast<uint32_t>(fn_.range_specs.size() - 1);
  }

  uint32_t add_string_replace_spec(uint32_t old_const, uint32_t new_const) {
    fn_.string_replace_specs.push_back(std::make_pair(old_const, new_const));
    return static_cast<uint32_t>(fn_.string_replace_specs.size() - 1);
  }

  uint32_t ensure_local(const std::string& name) {
    auto it = locals_.find(name);
    if (it != locals_.end()) {
      return it->second;
    }
    const auto slot = static_cast<uint32_t>(fn_.locals.size());
    fn_.locals.push_back(name);
    locals_[name] = slot;
    return slot;
  }

  uint32_t ensure_cell_for_local(const std::string& name) {
    auto it = cell_indices_.find(name);
    if (it != cell_indices_.end()) {
      return it->second;
    }
    const auto local = ensure_local(name);
    const auto cell = static_cast<uint32_t>(fn_.cell_slots.size());
    fn_.cell_slots.push_back(local);
    cell_indices_[name] = cell;
    return cell;
  }

  void prepare_captured_locals(const std::vector<ast::StmtPtr>& body) {
    if (is_module_) {
      return;
    }
    for (const auto& stmt : body) {
      auto* child = dynamic_cast<const ast::FunctionDef*>(stmt.get());
      if (child != nullptr) {
        for (const auto& name : closure_names_for_child(*child)) {
          if (sema::contains(local_name_set_, name)) {
            ensure_cell_for_local(name);
          }
        }
      }
      prepare_captured_locals_from_stmt(*stmt);
    }
  }

  void prepare_captured_locals_from_expr(const ast::Expr& expr) {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> local_targets;
    collect_expression_captures(expr, local_targets, names, seen);
    for (const auto& name : names) {
      if (sema::contains(local_name_set_, name)) {
        ensure_cell_for_local(name);
      }
    }
  }

  void prepare_captured_locals_from_stmt(const ast::Stmt& stmt) {
    if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*assign->value);
    } else if (auto* assign = dynamic_cast<const ast::AttrAssignStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*assign->object);
      prepare_captured_locals_from_expr(*assign->value);
    } else if (auto* assign = dynamic_cast<const ast::SubscriptAssignStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*assign->object);
      prepare_captured_locals_from_expr(*assign->index);
      prepare_captured_locals_from_expr(*assign->value);
    } else if (auto* expr = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*expr->expr);
    } else if (auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*ret->value);
    } else if (auto* assign = dynamic_cast<const ast::AnnotatedAssignStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*assign->target);
      if (assign->value != nullptr) {
        prepare_captured_locals_from_expr(*assign->value);
      }
    } else if (auto* assign = dynamic_cast<const ast::UnpackAssignStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*assign->value);
    } else if (auto* assign = dynamic_cast<const ast::MultiAssignStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*assign->value);
    } else if (auto* assign = dynamic_cast<const ast::AugAssignStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*assign->target);
      prepare_captured_locals_from_expr(*assign->value);
    } else if (auto* ifs = dynamic_cast<const ast::IfStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*ifs->condition);
      prepare_captured_locals(ifs->then_body);
      prepare_captured_locals(ifs->else_body);
    } else if (auto* loop = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*loop->condition);
      prepare_captured_locals(loop->body);
    } else if (auto* loop = dynamic_cast<const ast::ForStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*loop->iterable);
      prepare_captured_locals(loop->body);
    } else if (auto* with = dynamic_cast<const ast::WithStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*with->manager);
      prepare_captured_locals(with->body);
    } else if (auto* try_except = dynamic_cast<const ast::TryExceptStmt*>(&stmt)) {
      prepare_captured_locals(try_except->try_body);
      for (const auto& handler : try_except->handlers) {
        if (handler.type != nullptr) {
          prepare_captured_locals_from_expr(*handler.type);
        }
        prepare_captured_locals(handler.body);
      }
      prepare_captured_locals(try_except->else_body);
      prepare_captured_locals(try_except->finally_body);
    } else if (auto* match = dynamic_cast<const ast::MatchStmt*>(&stmt)) {
      prepare_captured_locals_from_expr(*match->subject);
      for (const auto& match_case : match->cases) {
        if (match_case.guard != nullptr) {
          prepare_captured_locals_from_expr(*match_case.guard);
        }
        prepare_captured_locals(match_case.body);
      }
    }
  }

  std::vector<std::string> closure_names_for_child(const ast::FunctionDef& child) const {
    std::vector<std::string> names;
    for (const auto& name : sema::free_candidates_for(child)) {
      if (sema::contains(local_name_set_, name) || free_indices_.find(name) != free_indices_.end()) {
        names.push_back(name);
      }
    }
    return names;
  }

  void add_expression_capture(
      const std::string& name,
      const std::unordered_set<std::string>& local_targets,
      std::vector<std::string>& names,
      std::unordered_set<std::string>& seen) const {
    const auto resolved = resolve_name(name);
    if (local_targets.find(resolved) != local_targets.end()) {
      return;
    }
    if ((sema::contains(local_name_set_, resolved) || free_indices_.find(resolved) != free_indices_.end()) &&
        seen.insert(resolved).second) {
      names.push_back(resolved);
    }
  }

  void collect_expression_captures(
      const ast::Expr& expr,
      const std::unordered_set<std::string>& local_targets,
      std::vector<std::string>& names,
      std::unordered_set<std::string>& seen) const {
    if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
      add_expression_capture(name->name, local_targets, names, seen);
      return;
    }
    if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
      collect_expression_captures(*unary->expr, local_targets, names, seen);
      return;
    }
    if (auto* await = dynamic_cast<const ast::AwaitExpr*>(&expr)) {
      collect_expression_captures(*await->expr, local_targets, names, seen);
      return;
    }
    if (auto* yield = dynamic_cast<const ast::YieldExpr*>(&expr)) {
      collect_expression_captures(*yield->expr, local_targets, names, seen);
      return;
    }
    if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
      collect_expression_captures(*binary->lhs, local_targets, names, seen);
      collect_expression_captures(*binary->rhs, local_targets, names, seen);
      return;
    }
    if (auto* chain = dynamic_cast<const ast::CompareChainExpr*>(&expr)) {
      collect_expression_captures(*chain->first, local_targets, names, seen);
      for (const auto& comparison : chain->comparisons) {
        collect_expression_captures(*comparison.second, local_targets, names, seen);
      }
      return;
    }
    if (auto* conditional = dynamic_cast<const ast::ConditionalExpr*>(&expr)) {
      collect_expression_captures(*conditional->then_expr, local_targets, names, seen);
      collect_expression_captures(*conditional->condition, local_targets, names, seen);
      collect_expression_captures(*conditional->else_expr, local_targets, names, seen);
      return;
    }
    if (auto* named = dynamic_cast<const ast::NamedExpr*>(&expr)) {
      collect_expression_captures(*named->value, local_targets, names, seen);
      return;
    }
    if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&expr)) {
      collect_expression_captures(*starred->expr, local_targets, names, seen);
      return;
    }
    if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
      collect_expression_captures(*call->callee, local_targets, names, seen);
      for (const auto& arg : call->args) {
        collect_expression_captures(*arg, local_targets, names, seen);
      }
      for (const auto& arg : call->call_args) {
        collect_expression_captures(*arg.value, local_targets, names, seen);
      }
      return;
    }
    if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
      collect_expression_captures(*subscript->object, local_targets, names, seen);
      collect_expression_captures(*subscript->index, local_targets, names, seen);
      return;
    }
    if (auto* slice = dynamic_cast<const ast::SliceExpr*>(&expr)) {
      collect_expression_captures(*slice->start, local_targets, names, seen);
      collect_expression_captures(*slice->stop, local_targets, names, seen);
      collect_expression_captures(*slice->step, local_targets, names, seen);
      return;
    }
    if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
      collect_expression_captures(*attr->object, local_targets, names, seen);
      return;
    }
    if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
      for (const auto& item : tuple->items) {
        collect_expression_captures(*item, local_targets, names, seen);
      }
      return;
    }
    if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
      for (const auto& item : list->items) {
        collect_expression_captures(*item, local_targets, names, seen);
      }
      return;
    }
    if (auto* dict = dynamic_cast<const ast::DictExpr*>(&expr)) {
      for (const auto& entry : dict->entries) {
        collect_expression_captures(*entry.first, local_targets, names, seen);
        collect_expression_captures(*entry.second, local_targets, names, seen);
      }
      return;
    }
    if (auto* set = dynamic_cast<const ast::SetExpr*>(&expr)) {
      for (const auto& item : set->items) {
        collect_expression_captures(*item, local_targets, names, seen);
      }
      return;
    }
    if (auto* generator = dynamic_cast<const ast::GeneratorExpr*>(&expr)) {
      for (const auto& name : closure_names_for_generator(*generator)) {
        if (local_targets.find(name) == local_targets.end() && seen.insert(name).second) {
          names.push_back(name);
        }
      }
      return;
    }
    if (auto* lambda = dynamic_cast<const ast::LambdaExpr*>(&expr)) {
      std::unordered_set<std::string> lambda_targets;
      for (const auto& param : lambda->signature) {
        if (!param.name.empty()) {
          lambda_targets.insert(resolve_name(param.name));
        }
      }
      if (lambda_targets.empty()) {
        for (const auto& param : lambda->params) {
          lambda_targets.insert(resolve_name(param));
        }
      }
      collect_expression_captures(*lambda->body, lambda_targets, names, seen);
      return;
    }
  }

  std::vector<std::string> closure_names_for_generator(const ast::GeneratorExpr& comp) const {
    std::vector<std::string> names;
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> targets;
    for (const auto& name : assignment_names(*comp.target_expr)) {
      targets.insert(resolve_name(name));
    }
    collect_expression_captures(*comp.iterable, targets, names, seen);
    if (comp.filter != nullptr) {
      collect_expression_captures(*comp.filter, targets, names, seen);
    }
    for (const auto& clause : comp.extra_clauses) {
      collect_expression_captures(*clause.iterable, targets, names, seen);
      for (const auto& name : assignment_names(*clause.target_expr)) {
        targets.insert(resolve_name(name));
      }
      if (clause.filter != nullptr) {
        collect_expression_captures(*clause.filter, targets, names, seen);
      }
    }
    collect_expression_captures(*comp.result, targets, names, seen);
    return names;
  }

  bool is_cell_local(const std::string& name) const {
    return cell_indices_.find(name) != cell_indices_.end();
  }

  bool module_global_slot(const std::string& name, uint32_t& slot) const {
    auto it = module_global_slots_.find(name);
    if (it == module_global_slots_.end()) {
      return false;
    }
    slot = it->second;
    return true;
  }

  bool imported_module_slot(const std::string& name, uint32_t& slot) const {
    return module_global_slot(name, slot) && imported_module_slots_.find(slot) != imported_module_slots_.end();
  }

  void lower_import_binding(const std::string& name, const std::string& bind_name) {
    const auto reg = new_reg();
    emit(ir::Op::ImportModule, reg, add_name(name));
    const auto root_dot = name.find('.');
    const auto root_name = root_dot == std::string::npos ? name : name.substr(0, root_dot);
    if (root_dot != std::string::npos && bind_name == root_name) {
      const auto bind_reg = new_reg();
      emit(ir::Op::ImportModule, bind_reg, add_name(bind_name));
      store_named_value(bind_name, bind_reg);
    } else {
      store_named_value(bind_name, reg);
    }
    uint32_t import_slot = 0;
    if (module_global_slot(bind_name, import_slot)) {
      imported_module_slots_.insert(import_slot);
    }
  }

  bool direct_local_slot(const std::string& name, uint32_t& slot) const {
    const auto resolved = resolve_name(name);
    if (is_module_ || sema::contains(global_names_, resolved) || is_cell_local(resolved)) {
      return false;
    }
    auto local = locals_.find(resolved);
    if (local == locals_.end()) {
      return false;
    }
    slot = local->second;
    return true;
  }

  void emit(ir::Op op, uint32_t dst = 0, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0) {
    fn_.code.push_back(ir::Instr{op, dst, a, b, c});
    fn_.source_lines.push_back(current_source_line_);
  }

  size_t emit_jump(ir::Op op, uint32_t cond = 0) {
    fn_.code.push_back(ir::Instr{op, 0, cond, 0, 0});
    fn_.source_lines.push_back(current_source_line_);
    return fn_.code.size() - 1;
  }

  void patch_jump(size_t at, uint32_t target) {
    fn_.code[at].dst = target;
  }

  void patch_iter_done(size_t at, uint32_t target) {
    fn_.code[at].b = target;
  }

  bool emit_compare_op(const std::string& op, uint32_t dst, uint32_t lhs, uint32_t rhs) {
    if (op == "is" || op == "is not") {
      emit(ir::Op::Is, dst, lhs, rhs, op == "is not" ? 1u : 0u);
      return true;
    }
    if (op == "in" || op == "not in") {
      emit(ir::Op::Contains, dst, lhs, rhs, op == "not in" ? 1u : 0u);
      return true;
    }

    uint32_t cmp = 0;
    if (op == "==") cmp = static_cast<uint32_t>(ir::CompareOp::Eq);
    else if (op == "!=") cmp = static_cast<uint32_t>(ir::CompareOp::Ne);
    else if (op == "<") cmp = static_cast<uint32_t>(ir::CompareOp::Lt);
    else if (op == "<=") cmp = static_cast<uint32_t>(ir::CompareOp::Le);
    else if (op == ">") cmp = static_cast<uint32_t>(ir::CompareOp::Gt);
    else if (op == ">=") cmp = static_cast<uint32_t>(ir::CompareOp::Ge);
    else return false;

    emit(ir::Op::Compare, dst, lhs, rhs, cmp);
    return true;
  }

  void emit_return_none() {
    const auto reg = new_reg();
    emit(ir::Op::LoadConst, reg, add_const(Value::none()));
    lower_active_finalizers();
    emit(ir::Op::Return, 0, reg);
  }

  void lower_finalizer_body(const std::vector<ast::StmtPtr>& body) {
    const auto saved = std::move(active_finalizers_);
    active_finalizers_.clear();
    lower_body(body);
    active_finalizers_ = std::move(saved);
  }

  void lower_active_finalizers() {
    const auto saved = active_finalizers_;
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
      lower_finalizer_body(**it);
    }
  }

  void store_named_value(const std::string& name, uint32_t reg) {
    const auto resolved = resolve_name(name);
    if ((is_module_ && hidden_locals_.find(resolved) == hidden_locals_.end()) ||
        (!is_module_ && sema::contains(global_names_, resolved))) {
      uint32_t slot = 0;
      if (module_global_slot(resolved, slot)) {
        emit(ir::Op::StoreModuleSlot, slot, reg);
      } else {
        emit(ir::Op::StoreGlobal, add_name(resolved), reg);
      }
    } else if (auto free_it = free_indices_.find(name); free_it != free_indices_.end()) {
      emit(ir::Op::StoreFree, free_it->second, reg);
    } else if (is_cell_local(resolved)) {
      emit(ir::Op::StoreCell, cell_indices_[resolved], reg);
    } else {
      emit(ir::Op::StoreLocal, ensure_local(resolved), reg);
    }
  }

  void delete_named_value(const std::string& name) {
    const auto resolved = resolve_name(name);
    if ((is_module_ && hidden_locals_.find(resolved) == hidden_locals_.end()) ||
        (!is_module_ && sema::contains(global_names_, resolved))) {
      uint32_t slot = 0;
      if (module_global_slot(resolved, slot)) {
        emit(ir::Op::DeleteModuleSlot, slot);
      } else {
        emit(ir::Op::DeleteGlobal, add_name(resolved));
      }
    } else if (auto free_it = free_indices_.find(name); free_it != free_indices_.end()) {
      const auto invalid = new_reg();
      emit(ir::Op::LoadConst, invalid, add_const(Value::invalid()));
      emit(ir::Op::StoreFree, free_it->second, invalid);
    } else if (is_cell_local(resolved)) {
      const auto invalid = new_reg();
      emit(ir::Op::LoadConst, invalid, add_const(Value::invalid()));
      emit(ir::Op::StoreCell, cell_indices_[resolved], invalid);
    } else {
      emit(ir::Op::DeleteLocal, ensure_local(resolved));
    }
  }

  std::string resolve_name(const std::string& name) const {
    auto it = name_aliases_.find(name);
    if (it != name_aliases_.end()) {
      return it->second;
    }
    return name;
  }

  KnownValueType known_type_for_expr(const ast::Expr& expr) const {
    if (auto* literal = dynamic_cast<const ast::LiteralExpr*>(&expr)) {
      return literal->kind == ast::LiteralExpr::Kind::String ? KnownValueType::String : KnownValueType::Unknown;
    }
    if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
      const auto resolved = resolve_name(name->name);
      auto it = local_known_types_.find(resolved);
      return it == local_known_types_.end() ? KnownValueType::Unknown : it->second;
    }
    return KnownValueType::Unknown;
  }

  void update_known_local_type(const std::string& name, const ast::Expr& expr) {
    const auto resolved = resolve_name(name);
    if (locals_.find(resolved) == locals_.end()) {
      return;
    }
    const auto type = known_type_for_expr(expr);
    if (type == KnownValueType::Unknown) {
      local_known_types_.erase(resolved);
    } else {
      local_known_types_[resolved] = type;
    }
  }

  void lower_for_loop(const ast::ForStmt& loop) {
    if (loop.is_async) {
      lower_async_for_loop(loop);
      return;
    }
    const auto* target_name = dynamic_cast<const ast::NameExpr*>(loop.target_expr.get());
    const std::string target = target_name != nullptr ? target_name->name : loop.target;
    if (target_name != nullptr && try_lower_const_range_for(target, *loop.iterable, loop.body)) {
      return;
    }
    const auto iterable_reg = lower_expr(*loop.iterable);
    const auto iterator_reg = new_reg();
    emit(ir::Op::GetIter, iterator_reg, iterable_reg);
    const auto start = static_cast<uint32_t>(fn_.code.size());
    const auto item_reg = new_reg();
    emit(ir::Op::IterNext, item_reg, iterator_reg, 0);
    const auto iter_next = fn_.code.size() - 1;
    if (loop.target_expr != nullptr) {
      lower_unpack_assign(*loop.target_expr, item_reg);
    } else {
      store_named_value(target, item_reg);
    }
    loop_continue_targets_.push_back(start);
    loop_break_jumps_.push_back({});
    lower_body(loop.body);
    auto break_jumps = std::move(loop_break_jumps_.back());
    loop_break_jumps_.pop_back();
    loop_continue_targets_.pop_back();
    emit(ir::Op::Jump, start);
    patch_iter_done(iter_next, static_cast<uint32_t>(fn_.code.size()));
    lower_body(loop.else_body);
    for (const auto jump : break_jumps) {
      patch_jump(jump, static_cast<uint32_t>(fn_.code.size()));
    }
  }

  void lower_async_for_loop(const ast::ForStmt& loop) {
    const auto* target_name = dynamic_cast<const ast::NameExpr*>(loop.target_expr.get());
    const std::string target = target_name != nullptr ? target_name->name : loop.target;
    const auto iterable_reg = lower_expr(*loop.iterable);
    const auto iterator_reg = emit_call_method(iterable_reg, "__aiter__", {});
    const auto start = static_cast<uint32_t>(fn_.code.size());
    const auto setup_next = emit_jump(ir::Op::SetupExcept);
    const auto next_awaitable = emit_call_method(iterator_reg, "__anext__", {});
    const auto item_reg = emit_await_value(next_awaitable);
    emit(ir::Op::PopExcept);
    if (loop.target_expr != nullptr) {
      lower_unpack_assign(*loop.target_expr, item_reg);
    } else {
      store_named_value(target, item_reg);
    }
    loop_continue_targets_.push_back(start);
    loop_break_jumps_.push_back({});
    lower_body(loop.body);
    auto break_jumps = std::move(loop_break_jumps_.back());
    loop_break_jumps_.pop_back();
    loop_continue_targets_.pop_back();
    emit(ir::Op::Jump, start);

    patch_jump(setup_next, static_cast<uint32_t>(fn_.code.size()));
    const auto stop_type = new_reg();
    emit(ir::Op::LoadGlobal, stop_type, add_name(resolve_name("StopAsyncIteration")));
    const auto matched = new_reg();
    emit(ir::Op::MatchException, matched, stop_type);
    const auto not_stop_async = emit_jump(ir::Op::JumpIfFalse, matched);
    emit(ir::Op::ClearException);
    lower_body(loop.else_body);
    const auto done = emit_jump(ir::Op::Jump);
    patch_jump(not_stop_async, static_cast<uint32_t>(fn_.code.size()));
    emit(ir::Op::Reraise);
    patch_jump(done, static_cast<uint32_t>(fn_.code.size()));
    for (const auto jump : break_jumps) {
      patch_jump(jump, static_cast<uint32_t>(fn_.code.size()));
    }
  }

  bool parse_int_literal(const ast::Expr& expr, int64_t& value) const {
    auto* lit = dynamic_cast<const ast::LiteralExpr*>(&expr);
    if (lit == nullptr || lit->kind != ast::LiteralExpr::Kind::Int) {
      return false;
    }
    value = parse_integer_literal(lit->text);
    return true;
  }

  bool try_parse_const_range_call(const ast::Expr& iterable, int64_t& start, int64_t& stop, int64_t& step) const {
    auto* call = dynamic_cast<const ast::CallExpr*>(&iterable);
    if (call == nullptr) {
      return false;
    }
    auto* callee = dynamic_cast<const ast::NameExpr*>(call->callee.get());
    if (callee == nullptr || resolve_name(callee->name) != "range") {
      return false;
    }
    if (call->args.size() == 1) {
      start = 0;
      step = 1;
      return parse_int_literal(*call->args[0], stop);
    }
    if (call->args.size() == 2) {
      step = 1;
      return parse_int_literal(*call->args[0], start) && parse_int_literal(*call->args[1], stop);
    }
    if (call->args.size() == 3) {
      return parse_int_literal(*call->args[0], start) &&
             parse_int_literal(*call->args[1], stop) &&
             parse_int_literal(*call->args[2], step) &&
             step != 0;
    }
    return false;
  }

  bool try_lower_const_range_for(
      const std::string& target,
      const ast::Expr& iterable,
      const std::vector<ast::StmtPtr>& body) {
    uint32_t target_slot = 0;
    if (!direct_local_slot(target, target_slot)) {
      return false;
    }
    int64_t start = 0;
    int64_t stop = 0;
    int64_t step = 1;
    if (!try_parse_const_range_call(iterable, start, stop, step)) {
      return false;
    }
    const auto state_name = "#range." + std::to_string(next_hidden_local_++) + "." + target;
    hidden_locals_.insert(state_name);
    const auto state_slot = ensure_local(state_name);
    const auto start_reg = new_reg();
    emit(ir::Op::LoadConst, start_reg, add_const(Value::int64(start)));
    emit(ir::Op::StoreLocal, state_slot, start_reg);
    const auto loop_start = static_cast<uint32_t>(fn_.code.size());
    emit(ir::Op::ForRangeConstLocalNext, 0, target_slot, state_slot,
         add_range_spec(add_const(Value::int64(stop)), add_const(Value::int64(step))));
    const auto next = fn_.code.size() - 1;
    loop_continue_targets_.push_back(loop_start);
    loop_break_jumps_.push_back({});
    lower_body(body);
    auto break_jumps = std::move(loop_break_jumps_.back());
    loop_break_jumps_.pop_back();
    loop_continue_targets_.pop_back();
    emit(ir::Op::Jump, loop_start);
    patch_jump(next, static_cast<uint32_t>(fn_.code.size()));
    for (const auto jump : break_jumps) {
      patch_jump(jump, static_cast<uint32_t>(fn_.code.size()));
    }
    return true;
  }

  void lower_try_except_core(const ast::TryExceptStmt& stmt) {
    const auto setup = emit_jump(ir::Op::SetupExcept);
    lower_body(stmt.try_body);
    emit(ir::Op::PopExcept);
    lower_body(stmt.else_body);
    const auto skip_except = emit_jump(ir::Op::Jump);
    patch_jump(setup, static_cast<uint32_t>(fn_.code.size()));
    std::vector<size_t> handler_done_jumps;
    for (const auto& handler : stmt.handlers) {
      size_t next_handler = 0;
      if (handler.type != nullptr) {
        const auto type_reg = lower_expr(*handler.type);
        const auto matched = new_reg();
        emit(ir::Op::MatchException, matched, type_reg);
        next_handler = emit_jump(ir::Op::JumpIfFalse, matched);
      }
      if (!handler.name.empty()) {
        const auto exc_reg = new_reg();
        emit(ir::Op::LoadException, exc_reg);
        store_named_value(handler.name, exc_reg);
      }
      lower_body(handler.body);
      emit(ir::Op::ClearException);
      handler_done_jumps.push_back(emit_jump(ir::Op::Jump));
      if (handler.type != nullptr) {
        patch_jump(next_handler, static_cast<uint32_t>(fn_.code.size()));
      }
    }
    emit(ir::Op::Reraise);
    for (const auto jump : handler_done_jumps) {
      patch_jump(jump, static_cast<uint32_t>(fn_.code.size()));
    }
    patch_jump(skip_except, static_cast<uint32_t>(fn_.code.size()));
  }

  void lower_try_except(const ast::TryExceptStmt& stmt) {
    if (stmt.finally_body.empty()) {
      lower_try_except_core(stmt);
      return;
    }

    const auto setup = emit_jump(ir::Op::SetupExcept);
    active_finalizers_.push_back(&stmt.finally_body);
    lower_try_except_core(stmt);
    active_finalizers_.pop_back();
    emit(ir::Op::PopExcept);
    lower_finalizer_body(stmt.finally_body);
    const auto done = emit_jump(ir::Op::Jump);
    patch_jump(setup, static_cast<uint32_t>(fn_.code.size()));
    lower_finalizer_body(stmt.finally_body);
    emit(ir::Op::Reraise);
    patch_jump(done, static_cast<uint32_t>(fn_.code.size()));
  }

  uint32_t emit_call_method(uint32_t object, const std::string& name, std::vector<uint32_t> args) {
    const auto dst = new_reg();
    emit(ir::Op::CallMethod, dst, object, add_name(name), add_call_args(std::move(args)));
    return dst;
  }

  uint32_t emit_call_value(uint32_t callee, std::vector<uint32_t> args) {
    const auto dst = new_reg();
    emit(ir::Op::Call, dst, callee, add_call_args(std::move(args)));
    return dst;
  }

  uint32_t emit_await_value(uint32_t value) {
    const auto dst = new_reg();
    emit(ir::Op::Await, dst, value);
    return dst;
  }

  uint32_t apply_decorators(uint32_t object_reg, const std::vector<ast::ExprPtr>& decorators) {
    uint32_t current = object_reg;
    for (auto it = decorators.rbegin(); it != decorators.rend(); ++it) {
      const auto decorator = lower_expr(**it);
      current = emit_call_value(decorator, {current});
    }
    return current;
  }

  void lower_with(const ast::WithStmt& stmt) {
    const auto manager = lower_expr(*stmt.manager);
    uint32_t entered = emit_call_method(manager, stmt.is_async ? "__aenter__" : "__enter__", {});
    if (stmt.is_async) {
      entered = emit_await_value(entered);
    }
    if (!stmt.target.empty()) {
      store_named_value(stmt.target, entered);
    } else {
      emit(ir::Op::Pop, 0, entered);
    }
    const auto setup = emit_jump(ir::Op::SetupWith, manager);
    lower_body(stmt.body);
    emit(ir::Op::PopExcept);
    const auto none_type = new_reg();
    const auto none_value = new_reg();
    const auto none_tb = new_reg();
    const auto none_const = add_const(Value::none());
    emit(ir::Op::LoadConst, none_type, none_const);
    emit(ir::Op::LoadConst, none_value, none_const);
    emit(ir::Op::LoadConst, none_tb, none_const);
    uint32_t exit_result =
        emit_call_method(manager, stmt.is_async ? "__aexit__" : "__exit__", {none_type, none_value, none_tb});
    if (stmt.is_async) {
      exit_result = emit_await_value(exit_result);
    }
    emit(ir::Op::Pop, 0, exit_result);
    const auto done = emit_jump(ir::Op::Jump);
    patch_jump(setup, static_cast<uint32_t>(fn_.code.size()));
    const auto exc_type = new_reg();
    const auto exc_value = new_reg();
    const auto exc_tb = new_reg();
    emit(ir::Op::LoadExceptionType, exc_type);
    emit(ir::Op::LoadException, exc_value);
    emit(ir::Op::LoadConst, exc_tb, none_const);
    uint32_t handled =
        emit_call_method(manager, stmt.is_async ? "__aexit__" : "__exit__", {exc_type, exc_value, exc_tb});
    if (stmt.is_async) {
      handled = emit_await_value(handled);
    }
    const auto rereraise = emit_jump(ir::Op::JumpIfFalse, handled);
    emit(ir::Op::ClearException);
    const auto suppressed = emit_jump(ir::Op::Jump);
    patch_jump(rereraise, static_cast<uint32_t>(fn_.code.size()));
    emit(ir::Op::Reraise);
    patch_jump(done, static_cast<uint32_t>(fn_.code.size()));
    patch_jump(suppressed, static_cast<uint32_t>(fn_.code.size()));
  }

  struct PatternCapture {
    std::string name;
    uint32_t source = 0;
  };

  uint32_t lower_pattern_to_bool(
      const ast::Expr& pattern,
      uint32_t subject,
      std::vector<PatternCapture>* captures = nullptr) {
    if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&pattern)) {
      if (binary->op == "|") {
        std::vector<PatternCapture> lhs_captures;
        const auto lhs = lower_pattern_to_bool(*binary->lhs, subject, captures == nullptr ? nullptr : &lhs_captures);
        const auto result = new_reg();
        std::vector<PatternCapture> merged_captures;
        if (captures != nullptr) {
          merged_captures.reserve(lhs_captures.size());
          for (const auto& capture : lhs_captures) {
            merged_captures.push_back(PatternCapture{capture.name, new_reg()});
          }
        }
        const auto lhs_failed = emit_jump(ir::Op::JumpIfFalse, lhs);
        for (size_t i = 0; i < lhs_captures.size() && i < merged_captures.size(); ++i) {
          emit(ir::Op::Move, merged_captures[i].source, lhs_captures[i].source);
        }
        emit(ir::Op::LoadConst, result, add_const(Value::boolean(true)));
        const auto done = emit_jump(ir::Op::Jump);
        patch_jump(lhs_failed, static_cast<uint32_t>(fn_.code.size()));
        std::vector<PatternCapture> rhs_captures;
        const auto rhs = lower_pattern_to_bool(*binary->rhs, subject, captures == nullptr ? nullptr : &rhs_captures);
        emit(ir::Op::LoadConst, result, add_const(Value::boolean(false)));
        const auto rhs_failed = emit_jump(ir::Op::JumpIfFalse, rhs);
        if (captures != nullptr) {
          for (auto& merged : merged_captures) {
            auto found = std::find_if(
                rhs_captures.begin(),
                rhs_captures.end(),
                [&](const PatternCapture& capture) { return capture.name == merged.name; });
            if (found != rhs_captures.end()) {
              emit(ir::Op::Move, merged.source, found->source);
            }
          }
        }
        emit(ir::Op::LoadConst, result, add_const(Value::boolean(true)));
        patch_jump(rhs_failed, static_cast<uint32_t>(fn_.code.size()));
        patch_jump(done, static_cast<uint32_t>(fn_.code.size()));
        if (captures != nullptr) {
          captures->insert(captures->end(), merged_captures.begin(), merged_captures.end());
        }
        return result;
      }
    }

    std::vector<size_t> fail_jumps;
    std::vector<PatternCapture> ignored_captures;
    emit_pattern_checks(pattern, subject, fail_jumps, captures == nullptr ? ignored_captures : *captures);
    const auto result = new_reg();
    emit(ir::Op::LoadConst, result, add_const(Value::boolean(true)));
    const auto done = emit_jump(ir::Op::Jump);
    const auto fail_target = static_cast<uint32_t>(fn_.code.size());
    for (const auto jump : fail_jumps) {
      patch_jump(jump, fail_target);
    }
    emit(ir::Op::LoadConst, result, add_const(Value::boolean(false)));
    patch_jump(done, static_cast<uint32_t>(fn_.code.size()));
    return result;
  }

  void emit_pattern_checks(
      const ast::Expr& pattern,
      uint32_t subject,
      std::vector<size_t>& fail_jumps,
      std::vector<PatternCapture>& captures) {
    if (auto* name = dynamic_cast<const ast::NameExpr*>(&pattern)) {
      if (name->name != "_") {
        captures.push_back(PatternCapture{name->name, subject});
      }
      return;
    }
    if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&pattern)) {
      emit_sequence_pattern_checks(tuple->items, subject, fail_jumps, captures);
      return;
    }
    if (auto* list = dynamic_cast<const ast::ListExpr*>(&pattern)) {
      emit_sequence_pattern_checks(list->items, subject, fail_jumps, captures);
      return;
    }
    if (auto* dict = dynamic_cast<const ast::DictExpr*>(&pattern)) {
      emit_mapping_pattern_checks(*dict, subject, fail_jumps, captures);
      return;
    }
    if (auto* call = dynamic_cast<const ast::CallExpr*>(&pattern)) {
      emit_class_pattern_checks(*call, subject, fail_jumps, captures);
      return;
    }
    const auto value = lower_expr(pattern);
    const auto matched = new_reg();
    emit(ir::Op::Compare, matched, subject, value, static_cast<uint32_t>(ir::CompareOp::Eq));
    fail_jumps.push_back(emit_jump(ir::Op::JumpIfFalse, matched));
  }

  void emit_always_fail_pattern(std::vector<size_t>& fail_jumps) {
    const auto matched = new_reg();
    emit(ir::Op::LoadConst, matched, add_const(Value::boolean(false)));
    fail_jumps.push_back(emit_jump(ir::Op::JumpIfFalse, matched));
  }

  void emit_class_pattern_checks(
      const ast::CallExpr& pattern,
      uint32_t subject,
      std::vector<size_t>& fail_jumps,
      std::vector<PatternCapture>& captures) {
    const auto klass = lower_expr(*pattern.callee);
    const auto isinstance_fn = new_reg();
    emit(ir::Op::LoadGlobal, isinstance_fn, add_name("isinstance"));
    const auto is_match = emit_call_value(isinstance_fn, {subject, klass});
    fail_jumps.push_back(emit_jump(ir::Op::JumpIfFalse, is_match));

    const auto* class_name = dynamic_cast<const ast::NameExpr*>(pattern.callee.get());
    const ClassInfo* class_info = nullptr;
    if (class_name != nullptr) {
      const auto found = class_infos_.find(class_name->name);
      if (found != class_infos_.end()) {
        class_info = &found->second;
      }
    }

    auto match_positional = [&](const ast::Expr& item, size_t index) {
      uint32_t attr = 0;
      if (class_info != nullptr && index < class_info->match_args.size()) {
        attr = new_reg();
        emit(ir::Op::LoadAttr, attr, subject, add_name(class_info->match_args[index]));
      } else {
        const auto match_args = new_reg();
        emit(ir::Op::LoadAttr, match_args, klass, add_name("__match_args__"));
        const auto index_reg = new_reg();
        emit(ir::Op::LoadConst, index_reg, add_const(Value::int64(static_cast<int64_t>(index))));
        const auto attr_name = new_reg();
        emit(ir::Op::GetItem, attr_name, match_args, index_reg);
        const auto getattr_fn = new_reg();
        emit(ir::Op::LoadGlobal, getattr_fn, add_name("getattr"));
        attr = emit_call_value(getattr_fn, {subject, attr_name});
      }
      emit_pattern_checks(item, attr, fail_jumps, captures);
    };

    if (!pattern.call_args.empty()) {
      size_t positional_index = 0;
      for (const auto& arg : pattern.call_args) {
        if (arg.star || arg.kw_star || arg.value == nullptr) {
          emit_always_fail_pattern(fail_jumps);
          continue;
        }
        if (arg.name.empty()) {
          match_positional(*arg.value, positional_index++);
          continue;
        }
        const auto attr = new_reg();
        emit(ir::Op::LoadAttr, attr, subject, add_name(arg.name));
        emit_pattern_checks(*arg.value, attr, fail_jumps, captures);
      }
      return;
    }

    for (size_t i = 0; i < pattern.args.size(); ++i) {
      match_positional(*pattern.args[i], i);
    }
  }

  void emit_sequence_pattern_checks(
      const std::vector<ast::ExprPtr>& items,
      uint32_t subject,
      std::vector<size_t>& fail_jumps,
      std::vector<PatternCapture>& captures) {
    size_t star_index = items.size();
    for (size_t i = 0; i < items.size(); ++i) {
      if (dynamic_cast<const ast::StarredExpr*>(items[i].get()) != nullptr) {
        star_index = i;
        break;
      }
    }
    const bool has_star = star_index != items.size();
    const size_t before_count = has_star ? star_index : items.size();
    const size_t after_count = has_star ? items.size() - star_index - 1 : 0;
    const size_t fixed_count = before_count + after_count;
    const auto length = new_reg();
    const auto expected = new_reg();
    const auto length_ok = new_reg();
    emit(ir::Op::Len, length, subject);
    emit(ir::Op::LoadConst, expected, add_const(Value::int64(static_cast<int64_t>(fixed_count))));
    emit(ir::Op::Compare, length_ok, length, expected,
         static_cast<uint32_t>(has_star ? ir::CompareOp::Ge : ir::CompareOp::Eq));
    fail_jumps.push_back(emit_jump(ir::Op::JumpIfFalse, length_ok));

    if (has_star) {
      const uint32_t output_count = static_cast<uint32_t>(fixed_count + 1);
      const auto first_output = new_reg();
      for (uint32_t i = 1; i < output_count; ++i) {
        (void)new_reg();
      }
      emit(ir::Op::UnpackSequence, first_output, subject, static_cast<uint32_t>(before_count),
           static_cast<uint32_t>(after_count) | 0x80000000u);
      for (size_t i = 0; i < before_count; ++i) {
        emit_pattern_checks(*items[i], first_output + static_cast<uint32_t>(i), fail_jumps, captures);
      }
      auto* starred = dynamic_cast<const ast::StarredExpr*>(items[star_index].get());
      if (starred != nullptr) {
        emit_pattern_checks(*starred->expr, first_output + static_cast<uint32_t>(before_count), fail_jumps, captures);
      }
      for (size_t i = 0; i < after_count; ++i) {
        emit_pattern_checks(
            *items[star_index + 1 + i],
            first_output + static_cast<uint32_t>(before_count + 1 + i),
            fail_jumps,
            captures);
      }
      return;
    }

    for (size_t i = 0; i < items.size(); ++i) {
      const auto index = new_reg();
      const auto item = new_reg();
      emit(ir::Op::LoadConst, index, add_const(Value::int64(static_cast<int64_t>(i))));
      emit(ir::Op::GetItem, item, subject, index);
      emit_pattern_checks(*items[i], item, fail_jumps, captures);
    }
  }

  void emit_mapping_pattern_checks(
      const ast::DictExpr& dict,
      uint32_t subject,
      std::vector<size_t>& fail_jumps,
      std::vector<PatternCapture>& captures) {
    for (const auto& entry : dict.entries) {
      const auto key = lower_expr(*entry.first);
      const auto present = new_reg();
      emit(ir::Op::Contains, present, key, subject);
      fail_jumps.push_back(emit_jump(ir::Op::JumpIfFalse, present));
      const auto value = new_reg();
      emit(ir::Op::GetItem, value, subject, key);
      emit_pattern_checks(*entry.second, value, fail_jumps, captures);
    }
  }

  void lower_match(const ast::MatchStmt& stmt) {
    const auto subject = lower_expr(*stmt.subject);
    std::vector<size_t> done_jumps;
    for (const auto& match_case : stmt.cases) {
      size_t next_case = 0;
      if (!match_case.wildcard) {
        std::vector<PatternCapture> captures;
        const auto matched = lower_pattern_to_bool(*match_case.pattern, subject, &captures);
        next_case = emit_jump(ir::Op::JumpIfFalse, matched);
        for (const auto& capture : captures) {
          store_named_value(capture.name, capture.source);
        }
        if (!match_case.as_name.empty() && match_case.as_name != "_") {
          store_named_value(match_case.as_name, subject);
        }
        if (match_case.guard != nullptr) {
          const auto guard = lower_expr(*match_case.guard);
          next_case = emit_jump(ir::Op::JumpIfFalse, guard);
        }
      } else if (match_case.guard != nullptr) {
        const auto guard = lower_expr(*match_case.guard);
        next_case = emit_jump(ir::Op::JumpIfFalse, guard);
      }
      lower_body(match_case.body);
      done_jumps.push_back(emit_jump(ir::Op::Jump));
      if (!match_case.wildcard || match_case.guard != nullptr) {
        patch_jump(next_case, static_cast<uint32_t>(fn_.code.size()));
      }
    }
    for (const auto jump : done_jumps) {
      patch_jump(jump, static_cast<uint32_t>(fn_.code.size()));
    }
  }

  bool try_emit_direct_local_assign(const ast::AssignStmt& assign) {
    uint32_t dst_slot = 0;
    if (!direct_local_slot(assign.name, dst_slot)) {
      return false;
    }
    if (auto* name = dynamic_cast<const ast::NameExpr*>(assign.value.get())) {
      uint32_t src_slot = 0;
      if (direct_local_slot(name->name, src_slot)) {
        emit(ir::Op::MoveLocal, dst_slot, src_slot);
        return true;
      }
    }
    if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(assign.value.get())) {
      if (binary->op == "+") {
        auto* lhs = dynamic_cast<const ast::NameExpr*>(binary->lhs.get());
        if (lhs != nullptr) {
          uint32_t lhs_slot = 0;
          if (direct_local_slot(lhs->name, lhs_slot)) {
            if (auto* rhs_name = dynamic_cast<const ast::NameExpr*>(binary->rhs.get())) {
              uint32_t rhs_slot = 0;
              if (direct_local_slot(rhs_name->name, rhs_slot)) {
                emit(ir::Op::AddLocalLocal, dst_slot, lhs_slot, rhs_slot);
                return true;
              }
            }
            auto* rhs = dynamic_cast<const ast::LiteralExpr*>(binary->rhs.get());
            if (rhs != nullptr &&
                (rhs->kind == ast::LiteralExpr::Kind::Int || rhs->kind == ast::LiteralExpr::Kind::Double)) {
              emit(ir::Op::AddLocalConst, dst_slot, lhs_slot, add_const(literal_value(*rhs)));
              return true;
            }
          }
        }
      }
    }
    return false;
  }

  bool try_emit_local_const_condition_jump(const ast::Expr& condition, size_t& jump) {
    auto* binary = dynamic_cast<const ast::BinaryExpr*>(&condition);
    if (binary == nullptr) {
      return false;
    }
    auto* lhs = dynamic_cast<const ast::NameExpr*>(binary->lhs.get());
    auto* rhs = dynamic_cast<const ast::LiteralExpr*>(binary->rhs.get());
    if (lhs == nullptr || rhs == nullptr ||
        (rhs->kind != ast::LiteralExpr::Kind::Int && rhs->kind != ast::LiteralExpr::Kind::Double)) {
      return false;
    }
    uint32_t local_slot = 0;
    if (!direct_local_slot(lhs->name, local_slot)) {
      return false;
    }
    uint32_t cmp = 0;
    if (binary->op == "==") cmp = static_cast<uint32_t>(ir::CompareOp::Eq);
    else if (binary->op == "!=") cmp = static_cast<uint32_t>(ir::CompareOp::Ne);
    else if (binary->op == "<") cmp = static_cast<uint32_t>(ir::CompareOp::Lt);
    else if (binary->op == "<=") cmp = static_cast<uint32_t>(ir::CompareOp::Le);
    else if (binary->op == ">") cmp = static_cast<uint32_t>(ir::CompareOp::Gt);
    else if (binary->op == ">=") cmp = static_cast<uint32_t>(ir::CompareOp::Ge);
    else return false;
    jump = emit_jump(ir::Op::JumpIfLocalConstFalse, local_slot);
    fn_.code[jump].b = add_const(literal_value(*rhs));
    fn_.code[jump].c = cmp;
    return true;
  }

  uint32_t lower_function_value(
      const ast::FunctionDef& fn,
      std::string instance_slot_self = {},
      std::unordered_map<std::string, uint32_t> instance_slots = {},
      std::string qualname_parent = {}) {
    const auto free_vars = closure_names_for_child(fn);
    for (const auto& name : free_vars) {
      if (sema::contains(local_name_set_, name)) {
        ensure_cell_for_local(name);
      }
    }
    auto signature = lower_signature_metadata(fn);
    std::vector<uint32_t> default_regs;
    std::vector<std::pair<std::string, uint32_t>> kwdefault_regs;
    std::vector<std::pair<std::string, uint32_t>> annotation_regs;
    if (!fn.signature.empty()) {
      for (size_t i = 0; i < fn.signature.size(); ++i) {
        if (fn.signature[i].default_value != nullptr) {
          signature[i].default_reg = static_cast<uint32_t>(default_regs.size());
          const auto default_reg = lower_expr(*fn.signature[i].default_value);
          default_regs.push_back(default_reg);
          if (fn.signature[i].kind == ast::FunctionDef::Param::Kind::KeywordOnly) {
            kwdefault_regs.push_back(std::make_pair(fn.signature[i].name, default_reg));
          }
        }
        if (fn.signature[i].annotation != nullptr) {
          annotation_regs.push_back(std::make_pair(fn.signature[i].name, lower_expr(*fn.signature[i].annotation)));
        }
      }
    }
    if (fn.return_annotation != nullptr) {
      annotation_regs.push_back(std::make_pair("return", lower_expr(*fn.return_annotation)));
    }
    FunctionLowerer child_lowerer(
        module_, fn.name, fn.params, std::move(signature), free_vars, fn.body, body_contains_yield(fn.body), fn.line, false,
        std::move(instance_slot_self), std::move(instance_slots), class_infos_, module_global_slots_,
        imported_module_slots_,
        qualname_parent.empty() ? (is_module_ || fn_.qualname.empty() ? std::string{} : fn_.qualname + ".<locals>")
                                : std::move(qualname_parent));
    child_lowerer.fn_.type_params = fn.type_params;
    child_lowerer.lower_body(fn.body);
    module_.functions.push_back(child_lowerer.finish());
    const uint32_t function_id = static_cast<uint32_t>(module_.functions.size() - 1);

    std::vector<uint32_t> closure_regs;
    for (const auto& name : free_vars) {
      const auto reg = new_reg();
      auto cell = cell_indices_.find(name);
      if (cell != cell_indices_.end()) {
        emit(ir::Op::LoadCellObject, reg, cell->second);
      } else {
        emit(ir::Op::LoadFreeObject, reg, free_indices_[name]);
      }
      closure_regs.push_back(reg);
    }

    const auto reg = new_reg();
    emit(ir::Op::MakeFunction, reg, function_id, add_function_closure(std::move(closure_regs)),
         default_regs.empty() ? UINT32_MAX : add_function_defaults(std::move(default_regs)));
    if (!annotation_regs.empty()) {
      emit(ir::Op::SetFunctionAnnotations, reg, add_function_annotations(std::move(annotation_regs)));
    }
    if (!kwdefault_regs.empty()) {
      emit(ir::Op::SetFunctionKwDefaults, reg, add_function_kwdefaults(std::move(kwdefault_regs)));
    }
    return reg;
  }

  uint32_t lower_class_def(const ast::ClassDef& klass, bool store_name = true, std::string qualname_parent = {}) {
    std::vector<std::pair<std::string, uint32_t>> attrs;
    const std::string& parent_qualname = qualname_parent.empty() ? qualname_prefix_ : qualname_parent;
    const std::string class_qualname = parent_qualname.empty() ? klass.name : parent_qualname + "." + klass.name;
    std::vector<std::string> own_instance_slots;
    std::unordered_set<std::string> seen_own_instance_slots;
    std::vector<std::string> match_args;
    bool has_explicit_slots = false;
    for (const auto& stmt : klass.body) {
      auto* assign = dynamic_cast<const ast::AssignStmt*>(stmt.get());
      if (assign != nullptr && assign->name == "__slots__" && assign->value != nullptr) {
        has_explicit_slots = collect_literal_slots_from_expr(*assign->value, own_instance_slots, seen_own_instance_slots);
      }
      if (assign != nullptr && assign->name == "__match_args__" && assign->value != nullptr) {
        std::vector<std::string> parsed_match_args;
        if (collect_literal_string_sequence(*assign->value, parsed_match_args)) {
          match_args = std::move(parsed_match_args);
        }
      }
    }
    if (!has_explicit_slots && klass.bases.empty()) {
      for (const auto& stmt : klass.body) {
        if (auto* fn = dynamic_cast<const ast::FunctionDef*>(stmt.get())) {
          if (!fn->params.empty()) {
            for (const auto& child : fn->body) {
              collect_self_attr_slots_stmt(*child, fn->params[0], own_instance_slots, seen_own_instance_slots);
            }
          }
        }
      }
    }

    std::vector<std::string> lowered_instance_slots;
    std::unordered_set<std::string> seen_lowered_instance_slots;
    auto append_lowered_slot = [&](const std::string& name) {
      if (seen_lowered_instance_slots.insert(name).second) {
        lowered_instance_slots.push_back(name);
      }
    };

    bool known_base_layout = true;
    for (const auto& base_expr : klass.bases) {
      auto* base_name = dynamic_cast<const ast::NameExpr*>(base_expr.get());
      if (base_name == nullptr) {
        known_base_layout = false;
        break;
      }
      auto base_info = class_infos_.find(base_name->name);
      if (base_info == class_infos_.end()) {
        known_base_layout = false;
        break;
      }
      for (const auto& slot_name : base_info->second.slot_names) {
        append_lowered_slot(slot_name);
      }
    }
    if (known_base_layout) {
      for (const auto& slot_name : own_instance_slots) {
        append_lowered_slot(slot_name);
      }
    }

    ClassInfo class_info;
    class_info.match_args = match_args;
    if (known_base_layout) {
      class_info.slot_names = lowered_instance_slots;
      for (size_t i = 0; i < lowered_instance_slots.size(); ++i) {
        class_info.slots[lowered_instance_slots[i]] = static_cast<uint32_t>(i);
      }
    }
    class_infos_[klass.name] = class_info;

    std::unordered_map<std::string, std::string> saved_aliases;
    std::unordered_set<std::string> erased_aliases;
    std::unordered_set<std::string> class_aliases;
    auto bind_class_attr_alias = [&](const std::string& name, uint32_t reg) {
      const std::string hidden_name = "#class." + klass.name + "." + name;
      if (class_aliases.insert(name).second) {
        if (name_aliases_.find(name) == name_aliases_.end()) {
          erased_aliases.insert(name);
        } else {
          saved_aliases[name] = name_aliases_[name];
        }
      }
      hidden_locals_.insert(hidden_name);
      name_aliases_[name] = hidden_name;
      store_named_value(name, reg);
    };

    if (!klass.type_params.empty()) {
      const auto attr_reg = emit_type_params_tuple(klass.type_params);
      attrs.push_back(std::make_pair("__type_params__", attr_reg));
      bind_class_attr_alias("__type_params__", attr_reg);
    }

    for (const auto& stmt : klass.body) {
      if (auto* fn = dynamic_cast<const ast::FunctionDef*>(stmt.get())) {
        const auto attr_reg = apply_decorators(
            lower_function_value(*fn, fn->params.empty() ? std::string{} : fn->params[0], class_info.slots, class_qualname),
            fn->decorators);
        attrs.push_back(std::make_pair(fn->name, attr_reg));
        bind_class_attr_alias(fn->name, attr_reg);
      } else if (auto* assign = dynamic_cast<const ast::AssignStmt*>(stmt.get())) {
        const auto attr_reg = lower_expr(*assign->value);
        attrs.push_back(std::make_pair(assign->name, attr_reg));
        bind_class_attr_alias(assign->name, attr_reg);
      } else if (auto* assign = dynamic_cast<const ast::MultiAssignStmt*>(stmt.get())) {
        const auto attr_reg = lower_expr(*assign->value);
        for (const auto& target : assign->targets) {
          if (auto* name = dynamic_cast<const ast::NameExpr*>(target.get())) {
            attrs.push_back(std::make_pair(name->name, attr_reg));
            bind_class_attr_alias(name->name, attr_reg);
          }
        }
      } else if (auto* assign = dynamic_cast<const ast::AnnotatedAssignStmt*>(stmt.get())) {
        if (assign->value != nullptr) {
          if (auto* name = dynamic_cast<const ast::NameExpr*>(assign->target.get())) {
            const auto attr_reg = lower_expr(*assign->value);
            attrs.push_back(std::make_pair(name->name, attr_reg));
            bind_class_attr_alias(name->name, attr_reg);
          }
        }
      } else if (auto* nested = dynamic_cast<const ast::ClassDef*>(stmt.get())) {
        const auto attr_reg = lower_class_def(*nested, false, class_qualname);
        attrs.push_back(std::make_pair(nested->name, attr_reg));
        bind_class_attr_alias(nested->name, attr_reg);
      }
    }

    for (const auto& name : erased_aliases) {
      name_aliases_.erase(name);
    }
    for (const auto& item : saved_aliases) {
      name_aliases_[item.first] = item.second;
    }

    const auto reg = new_reg();
    emit(ir::Op::MakeClass, reg, add_name(klass.name), add_class_attrs(std::move(attrs)),
         add_class_instance_slots(std::move(own_instance_slots)));
    for (const auto& base_expr : klass.bases) {
      const auto base = lower_expr(*base_expr);
      emit(ir::Op::SetClassBase, reg, base);
    }
    for (const auto& keyword : klass.keywords) {
      const auto ignored = lower_expr(*keyword.second);
      emit(ir::Op::Pop, 0, ignored);
    }
    const auto decorated_reg = apply_decorators(reg, klass.decorators);
    if (store_name) {
      store_named_value(klass.name, decorated_reg);
    }
    return decorated_reg;
  }

  bool is_instance_slot_target(const ast::Expr& object, const std::string& name) const {
    if (instance_slot_self_.empty()) {
      return false;
    }
    auto slot = instance_slots_.find(name);
    if (slot == instance_slots_.end()) {
      return false;
    }
    auto* object_name = dynamic_cast<const ast::NameExpr*>(&object);
    return object_name != nullptr && object_name->name == instance_slot_self_;
  }

  bool lower_assign_target(const ast::Expr& target, uint32_t value_reg) {
    if (auto* name = dynamic_cast<const ast::NameExpr*>(&target)) {
      store_named_value(name->name, value_reg);
      return true;
    }
    if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&target)) {
      const auto object = lower_expr(*attr->object);
      if (is_instance_slot_target(*attr->object, attr->name)) {
        emit(ir::Op::StoreInstanceSlot, object, instance_slots_[attr->name], value_reg);
      } else {
        emit(ir::Op::StoreAttr, object, add_name(attr->name), value_reg);
      }
      return true;
    }
    if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&target)) {
      const auto object = lower_expr(*subscript->object);
      const auto index = lower_expr(*subscript->index);
      emit(ir::Op::SetItem, object, index, value_reg);
      return true;
    }
    if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&target)) {
      return lower_assign_target(*starred->expr, value_reg);
    }
    if (dynamic_cast<const ast::TupleExpr*>(&target) != nullptr || dynamic_cast<const ast::ListExpr*>(&target) != nullptr) {
      lower_unpack_assign(target, value_reg);
      return true;
    }
    return false;
  }

  void lower_unpack_assign(const ast::Expr& target, uint32_t value_reg) {
    std::vector<const ast::Expr*> items;
    if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&target)) {
      for (const auto& item : tuple->items) items.push_back(item.get());
    } else if (auto* list = dynamic_cast<const ast::ListExpr*>(&target)) {
      for (const auto& item : list->items) items.push_back(item.get());
    } else {
      lower_assign_target(target, value_reg);
      return;
    }

    size_t star_index = items.size();
    for (size_t i = 0; i < items.size(); ++i) {
      if (dynamic_cast<const ast::StarredExpr*>(items[i]) != nullptr) {
        star_index = i;
        break;
      }
    }
    const bool has_star = star_index != items.size();
    const uint32_t before_count = has_star ? static_cast<uint32_t>(star_index) : static_cast<uint32_t>(items.size());
    const uint32_t after_count = has_star ? static_cast<uint32_t>(items.size() - star_index - 1) : 0;
    const uint32_t output_count = before_count + after_count + (has_star ? 1u : 0u);
    const uint32_t first_output = new_reg();
    for (uint32_t i = 1; i < output_count; ++i) {
      (void)new_reg();
    }
    emit(ir::Op::UnpackSequence, first_output, value_reg, before_count, after_count | (has_star ? 0x80000000u : 0u));

    for (uint32_t i = 0; i < before_count; ++i) {
      lower_assign_target(*items[i], first_output + i);
    }
    if (has_star) {
      lower_assign_target(*items[star_index], first_output + before_count);
      for (uint32_t i = 0; i < after_count; ++i) {
        lower_assign_target(*items[star_index + 1 + i], first_output + before_count + 1 + i);
      }
    }
  }

  ir::Op binary_op_for_aug_assign(const std::string& op) const {
    if (op == "+") return ir::Op::Add;
    if (op == "-") return ir::Op::Sub;
    if (op == "*") return ir::Op::Mul;
    if (op == "/") return ir::Op::Div;
    if (op == "//") return ir::Op::FloorDiv;
    if (op == "%") return ir::Op::Mod;
    if (op == "**") return ir::Op::Pow;
    if (op == "&") return ir::Op::BitAnd;
    if (op == "|") return ir::Op::BitOr;
    if (op == "^") return ir::Op::BitXor;
    if (op == "<<") return ir::Op::Shl;
    if (op == ">>") return ir::Op::Shr;
    return ir::Op::Add;
  }

  void lower_aug_assign(const ast::AugAssignStmt& assign) {
    const auto rhs = lower_expr(*assign.value);
    const auto result = new_reg();
    const auto op = binary_op_for_aug_assign(assign.op);
    if (auto* name = dynamic_cast<const ast::NameExpr*>(assign.target.get())) {
      const auto current = lower_expr(*assign.target);
      emit(op, result, current, rhs);
      store_named_value(name->name, result);
      return;
    }
    if (auto* attr = dynamic_cast<const ast::AttrExpr*>(assign.target.get())) {
      const auto object = lower_expr(*attr->object);
      const auto current = new_reg();
      if (is_instance_slot_target(*attr->object, attr->name)) {
        emit(ir::Op::LoadInstanceSlot, current, object, instance_slots_[attr->name]);
        emit(op, result, current, rhs);
        emit(ir::Op::StoreInstanceSlot, object, instance_slots_[attr->name], result);
      } else {
        emit(ir::Op::LoadAttr, current, object, add_name(attr->name));
        emit(op, result, current, rhs);
        emit(ir::Op::StoreAttr, object, add_name(attr->name), result);
      }
      return;
    }
    if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(assign.target.get())) {
      const auto object = lower_expr(*subscript->object);
      const auto index = lower_expr(*subscript->index);
      const auto current = new_reg();
      emit(ir::Op::GetItem, current, object, index);
      emit(op, result, current, rhs);
      emit(ir::Op::SetItem, object, index, result);
    }
  }

  void lower_stmt(const ast::Stmt& stmt) {
    struct SourceLineScope {
      uint32_t& current;
      uint32_t saved;

      ~SourceLineScope() {
        current = saved;
      }
    } source_line_scope{current_source_line_, current_source_line_};
    if (stmt.line != 0) {
      current_source_line_ = stmt.line;
    }
    if (dynamic_cast<const ast::GlobalStmt*>(&stmt) != nullptr) {
      return;
    }
    if (dynamic_cast<const ast::NonlocalStmt*>(&stmt) != nullptr) {
      return;
    }
    if (dynamic_cast<const ast::PassStmt*>(&stmt) != nullptr) {
      return;
    }
    if (dynamic_cast<const ast::BreakStmt*>(&stmt) != nullptr) {
      if (loop_break_jumps_.empty()) {
        return;
      }
      lower_active_finalizers();
      loop_break_jumps_.back().push_back(emit_jump(ir::Op::Jump));
      return;
    }
    if (dynamic_cast<const ast::ContinueStmt*>(&stmt) != nullptr) {
      if (loop_continue_targets_.empty()) {
        return;
      }
      lower_active_finalizers();
      emit(ir::Op::Jump, loop_continue_targets_.back());
      return;
    }
    if (auto* del = dynamic_cast<const ast::DelStmt*>(&stmt)) {
      if (auto* name = dynamic_cast<const ast::NameExpr*>(del->target.get())) {
        delete_named_value(name->name);
        return;
      }
      if (auto* attr = dynamic_cast<const ast::AttrExpr*>(del->target.get())) {
        const auto object = lower_expr(*attr->object);
        emit(ir::Op::DeleteAttr, object, add_name(attr->name));
        return;
      }
      if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(del->target.get())) {
        const auto object = lower_expr(*subscript->object);
        const auto index = lower_expr(*subscript->index);
        emit(ir::Op::DeleteItem, object, index);
        return;
      }
      return;
    }
    if (auto* assert_stmt = dynamic_cast<const ast::AssertStmt*>(&stmt)) {
      const auto cond = lower_expr(*assert_stmt->condition);
      const auto ok = emit_jump(ir::Op::JumpIfFalse, cond);
      const auto done = emit_jump(ir::Op::Jump);
      patch_jump(ok, static_cast<uint32_t>(fn_.code.size()));
      const auto klass = new_reg();
      emit(ir::Op::LoadGlobal, klass, add_name("AssertionError"));
      std::vector<uint32_t> args;
      if (assert_stmt->message != nullptr) {
        args.push_back(lower_expr(*assert_stmt->message));
      }
      const auto exc = new_reg();
      emit(ir::Op::Call, exc, klass, add_call_args(std::move(args)));
      emit(ir::Op::Raise, 0, exc);
      patch_jump(done, static_cast<uint32_t>(fn_.code.size()));
      return;
    }
    if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
      update_known_local_type(assign->name, *assign->value);
      if (try_emit_direct_local_assign(*assign)) {
        return;
      }
      store_named_value(assign->name, lower_expr(*assign->value));
      return;
    }
    if (auto* assign = dynamic_cast<const ast::AnnotatedAssignStmt*>(&stmt)) {
      if (assign->value == nullptr) {
        return;
      }
      const auto value = lower_expr(*assign->value);
      lower_assign_target(*assign->target, value);
      return;
    }
    if (auto* assign = dynamic_cast<const ast::UnpackAssignStmt*>(&stmt)) {
      const auto value = lower_expr(*assign->value);
      lower_assign_target(*assign->target, value);
      return;
    }
    if (auto* assign = dynamic_cast<const ast::MultiAssignStmt*>(&stmt)) {
      const auto value = lower_expr(*assign->value);
      for (auto it = assign->targets.rbegin(); it != assign->targets.rend(); ++it) {
        lower_assign_target(**it, value);
      }
      return;
    }
    if (auto* assign = dynamic_cast<const ast::AugAssignStmt*>(&stmt)) {
      lower_aug_assign(*assign);
      return;
    }
    if (auto* assign = dynamic_cast<const ast::SubscriptAssignStmt*>(&stmt)) {
      const auto object = lower_expr(*assign->object);
      const auto index = lower_expr(*assign->index);
      const auto value = lower_expr(*assign->value);
      emit(ir::Op::SetItem, object, index, value);
      return;
    }
    if (auto* assign = dynamic_cast<const ast::AttrAssignStmt*>(&stmt)) {
      const auto object = lower_expr(*assign->object);
      const auto value = lower_expr(*assign->value);
      if (is_instance_slot_target(*assign->object, assign->name)) {
        emit(ir::Op::StoreInstanceSlot, object, instance_slots_[assign->name], value);
      } else {
        emit(ir::Op::StoreAttr, object, add_name(assign->name), value);
      }
      return;
    }
    if (auto* import = dynamic_cast<const ast::ImportStmt*>(&stmt)) {
      lower_import_binding(import->name, import->bind_name);
      return;
    }
    if (auto* import = dynamic_cast<const ast::ImportManyStmt*>(&stmt)) {
      for (const auto& binding : import->names) {
        lower_import_binding(binding.name, binding.as_name);
      }
      return;
    }
    if (auto* import = dynamic_cast<const ast::FromImportStmt*>(&stmt)) {
      if (import->names.size() == 1 && import->names[0].name == "*") {
        emit(ir::Op::ImportStar, add_name(import->module));
        return;
      }
      for (const auto& binding : import->names) {
        const auto reg = new_reg();
        emit(ir::Op::ImportFrom, reg, add_name(import->module), add_name(binding.name));
        store_named_value(binding.as_name, reg);
      }
      return;
    }
    if (auto* raw = dynamic_cast<const ast::RawBlockStmt*>(&stmt)) {
      emit(ir::Op::RawBlock, add_raw_block(raw->language, raw->provider, raw->body));
      return;
    }
    if (auto* expr_stmt = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
      const auto reg = lower_expr(*expr_stmt->expr);
      emit(ir::Op::Pop, 0, reg);
      return;
    }
    if (auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
      const auto reg = lower_expr(*ret->value);
      lower_active_finalizers();
      emit(ir::Op::Return, 0, reg);
      return;
    }
    if (auto* raise = dynamic_cast<const ast::RaiseStmt*>(&stmt)) {
      if (raise->value == nullptr) {
        emit(ir::Op::Reraise);
        return;
      }
      if (raise->cause != nullptr) {
        const auto cause = lower_expr(*raise->cause);
        emit(ir::Op::SetExceptionCause, 0, cause);
      }
      const auto reg = lower_expr(*raise->value);
      emit(ir::Op::Raise, 0, reg);
      return;
    }
    if (auto* ifs = dynamic_cast<const ast::IfStmt*>(&stmt)) {
      size_t jf = 0;
      if (!try_emit_local_const_condition_jump(*ifs->condition, jf)) {
        const auto cond = lower_expr(*ifs->condition);
        jf = emit_jump(ir::Op::JumpIfFalse, cond);
      }
      lower_body(ifs->then_body);
      const auto jend = emit_jump(ir::Op::Jump);
      patch_jump(jf, static_cast<uint32_t>(fn_.code.size()));
      lower_body(ifs->else_body);
      patch_jump(jend, static_cast<uint32_t>(fn_.code.size()));
      return;
    }
    if (auto* try_except = dynamic_cast<const ast::TryExceptStmt*>(&stmt)) {
      lower_try_except(*try_except);
      return;
    }
    if (auto* with = dynamic_cast<const ast::WithStmt*>(&stmt)) {
      lower_with(*with);
      return;
    }
    if (auto* loop = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
      const auto start = static_cast<uint32_t>(fn_.code.size());
      size_t jf = 0;
      if (!try_emit_local_const_condition_jump(*loop->condition, jf)) {
        const auto cond = lower_expr(*loop->condition);
        jf = emit_jump(ir::Op::JumpIfFalse, cond);
      }
      loop_continue_targets_.push_back(start);
      loop_break_jumps_.push_back({});
      lower_body(loop->body);
      auto break_jumps = std::move(loop_break_jumps_.back());
      loop_break_jumps_.pop_back();
      loop_continue_targets_.pop_back();
      emit(ir::Op::Jump, start);
      patch_jump(jf, static_cast<uint32_t>(fn_.code.size()));
      lower_body(loop->else_body);
      for (const auto jump : break_jumps) {
        patch_jump(jump, static_cast<uint32_t>(fn_.code.size()));
      }
      return;
    }
    if (auto* loop = dynamic_cast<const ast::ForStmt*>(&stmt)) {
      lower_for_loop(*loop);
      return;
    }
    if (auto* match = dynamic_cast<const ast::MatchStmt*>(&stmt)) {
      lower_match(*match);
      return;
    }
    if (auto* fn = dynamic_cast<const ast::FunctionDef*>(&stmt)) {
      store_named_value(fn->name, apply_decorators(lower_function_value(*fn), fn->decorators));
      return;
    }
    if (auto* klass = dynamic_cast<const ast::ClassDef*>(&stmt)) {
      lower_class_def(*klass);
      return;
    }
  }

  uint32_t lower_compare_chain(const ast::CompareChainExpr& chain) {
    const auto result = new_reg();
    const auto false_const = add_const(Value::boolean(false));
    const auto true_const = add_const(Value::boolean(true));
    std::vector<size_t> false_jumps;

    auto lhs = lower_expr(*chain.first);
    for (const auto& comparison : chain.comparisons) {
      const auto rhs = lower_expr(*comparison.second);
      const auto cmp = new_reg();
      emit_compare_op(comparison.first, cmp, lhs, rhs);
      false_jumps.push_back(emit_jump(ir::Op::JumpIfFalse, cmp));
      lhs = rhs;
    }

    emit(ir::Op::LoadConst, result, true_const);
    const auto done = emit_jump(ir::Op::Jump);
    const auto false_label = static_cast<uint32_t>(fn_.code.size());
    emit(ir::Op::LoadConst, result, false_const);
    const auto end = static_cast<uint32_t>(fn_.code.size());

    for (const auto jump : false_jumps) {
      patch_jump(jump, false_label);
    }
    patch_jump(done, end);
    return result;
  }

  ast::ExprPtr parse_embedded_expression(const std::string& source) {
    auto parsed = parse_source("__x = " + source);
    if (!parsed.errors.empty() || parsed.module.body.empty()) {
      return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::String, "{" + source + "}");
    }
    auto* assign = dynamic_cast<ast::AssignStmt*>(parsed.module.body[0].get());
    if (assign == nullptr || assign->value == nullptr) {
      return std::make_unique<ast::LiteralExpr>(ast::LiteralExpr::Kind::String, "{" + source + "}");
    }
    return clone_expr(*assign->value);
  }

  uint32_t lower_fstring(const ast::FStringExpr& fstring) {
    uint32_t result = UINT32_MAX;
    bool has_part = false;
    for (const auto& part : fstring.parts) {
      uint32_t value = 0;
      if (part.is_expr) {
        auto expr = parse_embedded_expression(part.text);
        std::vector<ast::ExprPtr> args;
        args.push_back(std::move(expr));
        auto call = ast::CallExpr(std::make_unique<ast::NameExpr>(PythonNames::builtin_str), std::move(args));
        value = lower_expr(call);
      } else {
        value = new_reg();
        emit(ir::Op::LoadConst, value, add_const(Value::string(part.text)));
      }
      if (!has_part) {
        result = value;
        has_part = true;
        continue;
      }
      const auto joined = new_reg();
      emit(ir::Op::Add, joined, result, value);
      result = joined;
    }
    if (!has_part) {
      result = new_reg();
      emit(ir::Op::LoadConst, result, add_const(Value::string("")));
    }
    return result;
  }

  bool has_starred_item(const std::vector<ast::ExprPtr>& items) const {
    for (const auto& item : items) {
      if (dynamic_cast<const ast::StarredExpr*>(item.get()) != nullptr) {
        return true;
      }
    }
    return false;
  }

  uint32_t lower_list_with_unpack(const std::vector<ast::ExprPtr>& items) {
    const auto dst = new_reg();
    emit(ir::Op::MakeList, dst, add_list_items({}));
    for (const auto& item : items) {
      if (auto* starred = dynamic_cast<const ast::StarredExpr*>(item.get())) {
        emit(ir::Op::ListExtend, dst, lower_expr(*starred->expr));
      } else {
        emit(ir::Op::ListAppend, dst, lower_expr(*item));
      }
    }
    return dst;
  }

  uint32_t lower_tuple_with_unpack(const std::vector<ast::ExprPtr>& items) {
    const auto list = lower_list_with_unpack(items);
    const auto tuple = new_reg();
    emit(ir::Op::TupleFromList, tuple, list);
    return tuple;
  }

  uint32_t lower_set_with_unpack(const std::vector<ast::ExprPtr>& items) {
    const auto dst = new_reg();
    emit(ir::Op::MakeSet, dst, add_set_items({}));
    for (const auto& item : items) {
      if (auto* starred = dynamic_cast<const ast::StarredExpr*>(item.get())) {
        emit(ir::Op::SetUpdate, dst, lower_expr(*starred->expr));
      } else {
        emit(ir::Op::SetAdd, dst, lower_expr(*item));
      }
    }
    return dst;
  }

  template <typename Body>
  uint32_t lower_comprehension_loop(
      const ast::Expr& target_expr,
      const ast::Expr& iterable,
      const ast::Expr* filter,
      uint32_t dst,
      Body body) {
    struct SavedAlias {
      std::string name;
      bool had_alias = false;
      std::string value;
    };

    std::vector<SavedAlias> saved_aliases;
    for (const auto& name : assignment_names(target_expr)) {
      const auto hidden_name = "#comp." + std::to_string(next_hidden_local_++) + "." + name;
      hidden_locals_.insert(hidden_name);
      ensure_local(hidden_name);
      const auto old_alias = name_aliases_.find(name);
      saved_aliases.push_back(SavedAlias{
          name,
          old_alias != name_aliases_.end(),
          old_alias == name_aliases_.end() ? std::string{} : old_alias->second});
      name_aliases_[name] = hidden_name;
    }

    const auto* target_name = dynamic_cast<const ast::NameExpr*>(&target_expr);
    uint32_t single_hidden_slot = 0;
    const bool has_single_name_target = target_name != nullptr;
    if (has_single_name_target) {
      single_hidden_slot = ensure_local(name_aliases_[target_name->name]);
    }

    int64_t range_start = 0;
    int64_t range_stop = 0;
    int64_t range_step = 1;
    size_t loop_exit = 0;
    uint32_t start = 0;
    bool fused_range = false;
    if (has_single_name_target && try_parse_const_range_call(iterable, range_start, range_stop, range_step)) {
      fused_range = true;
      const auto state_name = "#range." + std::to_string(next_hidden_local_++) + "." + target_name->name;
      hidden_locals_.insert(state_name);
      const auto state_slot = ensure_local(state_name);
      const auto start_reg = new_reg();
      emit(ir::Op::LoadConst, start_reg, add_const(Value::int64(range_start)));
      emit(ir::Op::StoreLocal, state_slot, start_reg);
      start = static_cast<uint32_t>(fn_.code.size());
      emit(ir::Op::ForRangeConstLocalNext, 0, single_hidden_slot, state_slot,
           add_range_spec(add_const(Value::int64(range_stop)), add_const(Value::int64(range_step))));
      loop_exit = fn_.code.size() - 1;
    } else {
      const auto iterable_reg = lower_expr(iterable);
      const auto iterator_reg = new_reg();
      emit(ir::Op::GetIter, iterator_reg, iterable_reg);
      start = static_cast<uint32_t>(fn_.code.size());
      const auto item_reg = new_reg();
      emit(ir::Op::IterNext, item_reg, iterator_reg, 0);
      loop_exit = fn_.code.size() - 1;
      lower_unpack_assign(target_expr, item_reg);
    }
    size_t skip_append = 0;
    const bool has_filter = filter != nullptr;
    if (has_filter) {
      const auto filter_reg = lower_expr(*filter);
      skip_append = emit_jump(ir::Op::JumpIfFalse, filter_reg);
    }
    body();
    if (has_filter) {
      patch_jump(skip_append, static_cast<uint32_t>(fn_.code.size()));
    }
    emit(ir::Op::Jump, start);
    if (fused_range) {
      patch_jump(loop_exit, static_cast<uint32_t>(fn_.code.size()));
    } else {
      patch_iter_done(loop_exit, static_cast<uint32_t>(fn_.code.size()));
    }

    for (auto it = saved_aliases.rbegin(); it != saved_aliases.rend(); ++it) {
      if (it->had_alias) {
        name_aliases_[it->name] = it->value;
      } else {
        name_aliases_.erase(it->name);
      }
    }
    return dst;
  }

  struct LowerCompClause {
    std::string target;
    const ast::Expr* target_expr = nullptr;
    const ast::Expr* iterable = nullptr;
    const ast::Expr* filter = nullptr;
  };

  template <typename Body>
  uint32_t lower_comprehension_clauses(
      const std::vector<LowerCompClause>& clauses,
      size_t index,
      uint32_t dst,
      Body body) {
    if (index >= clauses.size()) {
      body();
      return dst;
    }
    const auto& clause = clauses[index];
    return lower_comprehension_loop(
        *clause.target_expr,
        *clause.iterable,
        clause.filter,
        dst,
        [&]() { lower_comprehension_clauses(clauses, index + 1, dst, body); });
  }

  std::vector<LowerCompClause> list_comp_clauses(const ast::ListCompExpr& comp) {
    std::vector<LowerCompClause> clauses;
    clauses.push_back(LowerCompClause{comp.target, comp.target_expr.get(), comp.iterable.get(), comp.filter.get()});
    for (const auto& clause : comp.extra_clauses) {
      clauses.push_back(LowerCompClause{clause.target, clause.target_expr.get(), clause.iterable.get(), clause.filter.get()});
    }
    return clauses;
  }

  std::vector<LowerCompClause> dict_comp_clauses(const ast::DictCompExpr& comp) {
    std::vector<LowerCompClause> clauses;
    clauses.push_back(LowerCompClause{comp.target, comp.target_expr.get(), comp.iterable.get(), comp.filter.get()});
    for (const auto& clause : comp.extra_clauses) {
      clauses.push_back(LowerCompClause{clause.target, clause.target_expr.get(), clause.iterable.get(), clause.filter.get()});
    }
    return clauses;
  }

  std::vector<LowerCompClause> set_comp_clauses(const ast::SetCompExpr& comp) {
    std::vector<LowerCompClause> clauses;
    clauses.push_back(LowerCompClause{comp.target, comp.target_expr.get(), comp.iterable.get(), comp.filter.get()});
    for (const auto& clause : comp.extra_clauses) {
      clauses.push_back(LowerCompClause{clause.target, clause.target_expr.get(), clause.iterable.get(), clause.filter.get()});
    }
    return clauses;
  }

  std::vector<LowerCompClause> generator_comp_clauses(const ast::GeneratorExpr& comp) {
    std::vector<LowerCompClause> clauses;
    clauses.push_back(LowerCompClause{comp.target, comp.target_expr.get(), comp.iterable.get(), comp.filter.get()});
    for (const auto& clause : comp.extra_clauses) {
      clauses.push_back(LowerCompClause{clause.target, clause.target_expr.get(), clause.iterable.get(), clause.filter.get()});
    }
    return clauses;
  }

  uint32_t lower_generator_expr(const ast::GeneratorExpr& comp) {
    const auto free_vars = closure_names_for_generator(comp);
    for (const auto& name : free_vars) {
      if (sema::contains(local_name_set_, name)) {
        ensure_cell_for_local(name);
      }
    }
    FunctionLowerer child_lowerer(
        module_, "#genexpr", {}, {}, free_vars, std::vector<ast::StmtPtr>{}, true, 0, false,
        instance_slot_self_, instance_slots_, class_infos_, module_global_slots_, imported_module_slots_);
    const auto clauses = child_lowerer.generator_comp_clauses(comp);
    child_lowerer.lower_comprehension_clauses(
        clauses, 0, 0, [&]() {
          const auto value = child_lowerer.lower_expr(*comp.result);
          child_lowerer.emit(ir::Op::Yield, 0, value);
        });
    module_.functions.push_back(child_lowerer.finish());
    const uint32_t function_id = static_cast<uint32_t>(module_.functions.size() - 1);
    const auto callee = new_reg();
    std::vector<uint32_t> closure_regs;
    for (const auto& name : free_vars) {
      const auto reg = new_reg();
      auto cell = cell_indices_.find(name);
      if (cell != cell_indices_.end()) {
        emit(ir::Op::LoadCellObject, reg, cell->second);
      } else {
        emit(ir::Op::LoadFreeObject, reg, free_indices_[name]);
      }
      closure_regs.push_back(reg);
    }
    emit(ir::Op::MakeFunction, callee, function_id, add_function_closure(std::move(closure_regs)), UINT32_MAX);
    const auto dst = new_reg();
    emit(ir::Op::Call, dst, callee, add_call_args({}));
    return dst;
  }

  uint32_t lower_expr(const ast::Expr& expr) {
    if (auto* lit = dynamic_cast<const ast::LiteralExpr*>(&expr)) {
      const auto reg = new_reg();
      switch (lit->kind) {
        case ast::LiteralExpr::Kind::None:
          emit(ir::Op::LoadConst, reg, add_const(Value::none()));
          break;
        case ast::LiteralExpr::Kind::Bool:
          emit(ir::Op::LoadConst, reg, add_const(Value::boolean(lit->bool_value)));
          break;
        case ast::LiteralExpr::Kind::Int:
          emit(ir::Op::LoadConst, reg, add_const(Value::int64(parse_integer_literal(lit->text))));
          break;
        case ast::LiteralExpr::Kind::Double:
          emit(ir::Op::LoadConst, reg, add_const(Value::number(std::strtod(lit->text.c_str(), nullptr))));
          break;
        case ast::LiteralExpr::Kind::String:
          emit(ir::Op::LoadConst, reg, add_const(Value::string(lit->text)));
          break;
        case ast::LiteralExpr::Kind::Bytes:
          emit(ir::Op::LoadConst, reg, add_const(Value::bytes(lit->text)));
          break;
        case ast::LiteralExpr::Kind::Ellipsis:
          emit(ir::Op::LoadConst, reg, add_const(Value::none()));
          break;
      }
      return reg;
    }
    if (auto* fstring = dynamic_cast<const ast::FStringExpr*>(&expr)) {
      return lower_fstring(*fstring);
    }
    if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
      const auto reg = new_reg();
      const auto resolved = resolve_name(name->name);
      auto local = locals_.find(resolved);
      if (local != locals_.end()) {
        if (is_cell_local(resolved)) {
          emit(ir::Op::LoadCell, reg, cell_indices_[resolved]);
        } else {
          emit(ir::Op::LoadLocal, reg, local->second);
        }
      } else if (auto free_it = free_indices_.find(resolved); free_it != free_indices_.end()) {
        emit(ir::Op::LoadFree, reg, free_it->second);
      } else {
        uint32_t slot = 0;
        if (module_global_slot(resolved, slot)) {
          emit(ir::Op::LoadModuleSlot, reg, slot);
        } else {
          emit(ir::Op::LoadGlobal, reg, add_name(resolved));
        }
      }
      return reg;
    }
    if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
      const auto src = lower_expr(*unary->expr);
      const auto reg = new_reg();
      if (unary->op == "not") emit(ir::Op::Not, reg, src);
      else if (unary->op == "~") emit(ir::Op::Invert, reg, src);
      else emit(ir::Op::Neg, reg, src);
      return reg;
    }
    if (auto* await = dynamic_cast<const ast::AwaitExpr*>(&expr)) {
      const auto src = lower_expr(*await->expr);
      const auto reg = new_reg();
      emit(ir::Op::Await, reg, src);
      return reg;
    }
    if (auto* yield = dynamic_cast<const ast::YieldExpr*>(&expr)) {
      const auto src = lower_expr(*yield->expr);
      if (yield->from) {
        const auto iterator = new_reg();
        emit(ir::Op::GetIter, iterator, src);
        const auto start = static_cast<uint32_t>(fn_.code.size());
        const auto item = new_reg();
        emit(ir::Op::IterNext, item, iterator, 0);
        const auto iter_next = fn_.code.size() - 1;
        emit(ir::Op::Yield, 0, item);
        emit(ir::Op::Jump, start);
        patch_iter_done(iter_next, static_cast<uint32_t>(fn_.code.size()));
      } else {
        emit(ir::Op::Yield, 0, src);
      }
      const auto reg = new_reg();
      emit(ir::Op::LoadConst, reg, add_const(Value::none()));
      return reg;
    }
    if (auto* chain = dynamic_cast<const ast::CompareChainExpr*>(&expr)) {
      return lower_compare_chain(*chain);
    }
    if (auto* bin = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
      const auto lhs = lower_expr(*bin->lhs);
      if (bin->op == "and") {
        const auto reg = new_reg();
        emit(ir::Op::Move, reg, lhs);
        const auto done_if_false = emit_jump(ir::Op::JumpIfFalse, lhs);
        const auto rhs = lower_expr(*bin->rhs);
        emit(ir::Op::Move, reg, rhs);
        patch_jump(done_if_false, static_cast<uint32_t>(fn_.code.size()));
        return reg;
      }
      if (bin->op == "or") {
        const auto reg = new_reg();
        emit(ir::Op::Move, reg, lhs);
        const auto rhs_jump = emit_jump(ir::Op::JumpIfFalse, lhs);
        const auto done = emit_jump(ir::Op::Jump);
        patch_jump(rhs_jump, static_cast<uint32_t>(fn_.code.size()));
        const auto rhs = lower_expr(*bin->rhs);
        emit(ir::Op::Move, reg, rhs);
        patch_jump(done, static_cast<uint32_t>(fn_.code.size()));
        return reg;
      }
      if (bin->op == "%") {
        if (auto* lit = dynamic_cast<const ast::LiteralExpr*>(bin->rhs.get())) {
          if (lit->kind == ast::LiteralExpr::Kind::Int) {
            const auto reg = new_reg();
            emit(ir::Op::ModConst, reg, lhs, add_const(literal_value(*lit)));
            return reg;
          }
        }
      }
      const auto rhs = lower_expr(*bin->rhs);
      const auto reg = new_reg();
      if (bin->op == "+") emit(ir::Op::Add, reg, lhs, rhs);
      else if (bin->op == "-") emit(ir::Op::Sub, reg, lhs, rhs);
      else if (bin->op == "*") emit(ir::Op::Mul, reg, lhs, rhs);
      else if (bin->op == "/") emit(ir::Op::Div, reg, lhs, rhs);
      else if (bin->op == "//") emit(ir::Op::FloorDiv, reg, lhs, rhs);
      else if (bin->op == "%") emit(ir::Op::Mod, reg, lhs, rhs);
      else if (bin->op == "**") emit(ir::Op::Pow, reg, lhs, rhs);
      else if (bin->op == "&") emit(ir::Op::BitAnd, reg, lhs, rhs);
      else if (bin->op == "|") emit(ir::Op::BitOr, reg, lhs, rhs);
      else if (bin->op == "^") emit(ir::Op::BitXor, reg, lhs, rhs);
      else if (bin->op == "<<") emit(ir::Op::Shl, reg, lhs, rhs);
      else if (bin->op == ">>") emit(ir::Op::Shr, reg, lhs, rhs);
      else if (bin->op == "and") emit(ir::Op::BoolAnd, reg, lhs, rhs);
      else if (bin->op == "or") emit(ir::Op::BoolOr, reg, lhs, rhs);
      else emit_compare_op(bin->op, reg, lhs, rhs);
      return reg;
    }
    if (auto* conditional = dynamic_cast<const ast::ConditionalExpr*>(&expr)) {
      const auto reg = new_reg();
      const auto condition = lower_expr(*conditional->condition);
      const auto else_jump = emit_jump(ir::Op::JumpIfFalse, condition);
      const auto then_value = lower_expr(*conditional->then_expr);
      emit(ir::Op::Pop, 0, then_value);
      emit(ir::Op::Move, reg, then_value);
      const auto done_jump = emit_jump(ir::Op::Jump);
      patch_jump(else_jump, static_cast<uint32_t>(fn_.code.size()));
      const auto else_value = lower_expr(*conditional->else_expr);
      emit(ir::Op::Pop, 0, else_value);
      emit(ir::Op::Move, reg, else_value);
      patch_jump(done_jump, static_cast<uint32_t>(fn_.code.size()));
      return reg;
    }
    if (auto* named = dynamic_cast<const ast::NamedExpr*>(&expr)) {
      const auto value = lower_expr(*named->value);
      store_named_value(named->name, value);
      return value;
    }
    if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
      if (!call->call_args.empty()) {
        const auto callee = lower_expr(*call->callee);
        ir::CallSpec spec;
        for (const auto& arg : call->call_args) {
          const auto value = lower_expr(*arg.value);
          if (arg.kw_star) {
            spec.kw_star_arg = value;
          } else if (arg.star) {
            spec.star_arg = value;
          } else if (!arg.name.empty()) {
            spec.keywords.push_back(ir::CallKeywordArg{arg.name, value});
          } else {
            spec.positional.push_back(value);
          }
        }
        const auto dst = new_reg();
        emit(ir::Op::CallEx, dst, callee, add_call_spec(std::move(spec)));
        return dst;
      }
      if (auto* name = dynamic_cast<const ast::NameExpr*>(call->callee.get())) {
        if (resolve_name(name->name) == PythonNames::builtin_len && call->args.size() == 1) {
          const auto value = lower_expr(*call->args[0]);
          const auto dst = new_reg();
          emit(ir::Op::Len, dst, value);
          return dst;
        }
      }
      if (auto* attr = dynamic_cast<const ast::AttrExpr*>(call->callee.get())) {
        uint32_t imported_slot = 0;
        auto* module_name = dynamic_cast<const ast::NameExpr*>(attr->object.get());
        if (!is_module_ && module_name != nullptr) {
          const auto resolved = resolve_name(module_name->name);
          if (imported_module_slot(resolved, imported_slot)) {
            std::vector<uint32_t> arg_regs;
            for (const auto& arg : call->args) {
              arg_regs.push_back(lower_expr(*arg));
            }
            const auto dst = new_reg();
            emit(ir::Op::CallModuleMethod, dst, imported_slot, add_name(attr->name), add_call_args(std::move(arg_regs)));
            return dst;
          }
        }
        const auto object = lower_expr(*attr->object);
        std::vector<uint32_t> arg_regs;
        for (const auto& arg : call->args) {
          arg_regs.push_back(lower_expr(*arg));
        }
        const auto dst = new_reg();
        emit(ir::Op::CallMethod, dst, object, add_name(attr->name), add_call_args(std::move(arg_regs)));
        return dst;
      }
      const auto callee = lower_expr(*call->callee);
      std::vector<uint32_t> arg_regs;
      for (const auto& arg : call->args) {
        arg_regs.push_back(lower_expr(*arg));
      }
      const auto dst = new_reg();
      emit(ir::Op::Call, dst, callee, add_call_args(std::move(arg_regs)));
      return dst;
    }
    if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
      const auto object = lower_expr(*subscript->object);
      const auto index = lower_expr(*subscript->index);
      const auto dst = new_reg();
      emit(ir::Op::GetItem, dst, object, index);
      return dst;
    }
    if (auto* slice = dynamic_cast<const ast::SliceExpr*>(&expr)) {
      const auto start = lower_expr(*slice->start);
      const auto stop = lower_expr(*slice->stop);
      const auto step = lower_expr(*slice->step);
      const auto dst = new_reg();
      emit(ir::Op::MakeSlice, dst, start, stop, step);
      return dst;
    }
    if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
      const auto object = lower_expr(*attr->object);
      const auto dst = new_reg();
      if (is_instance_slot_target(*attr->object, attr->name)) {
        emit(ir::Op::LoadInstanceSlot, dst, object, instance_slots_[attr->name]);
      } else {
        emit(ir::Op::LoadAttr, dst, object, add_name(attr->name));
      }
      return dst;
    }
    if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
      if (has_starred_item(tuple->items)) {
        return lower_tuple_with_unpack(tuple->items);
      }
      std::vector<uint32_t> item_regs;
      for (const auto& item : tuple->items) {
        item_regs.push_back(lower_expr(*item));
      }
      const auto dst = new_reg();
      emit(ir::Op::MakeTuple, dst, add_tuple_items(std::move(item_regs)));
      return dst;
    }
    if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
      if (has_starred_item(list->items)) {
        return lower_list_with_unpack(list->items);
      }
      std::vector<uint32_t> item_regs;
      for (const auto& item : list->items) {
        item_regs.push_back(lower_expr(*item));
      }
      const auto dst = new_reg();
      emit(ir::Op::MakeList, dst, add_list_items(std::move(item_regs)));
      return dst;
    }
    if (auto* dict = dynamic_cast<const ast::DictExpr*>(&expr)) {
      std::vector<std::pair<uint32_t, uint32_t>> item_regs;
      for (const auto& entry : dict->entries) {
        const auto key = lower_expr(*entry.first);
        const auto value = lower_expr(*entry.second);
        item_regs.push_back(std::make_pair(key, value));
      }
      const auto dst = new_reg();
      emit(ir::Op::MakeDict, dst, add_dict_items(std::move(item_regs)));
      return dst;
    }
    if (auto* set = dynamic_cast<const ast::SetExpr*>(&expr)) {
      if (has_starred_item(set->items)) {
        return lower_set_with_unpack(set->items);
      }
      std::vector<uint32_t> item_regs;
      for (const auto& item : set->items) {
        item_regs.push_back(lower_expr(*item));
      }
      const auto dst = new_reg();
      emit(ir::Op::MakeSet, dst, add_set_items(std::move(item_regs)));
      return dst;
    }
    if (auto* comp = dynamic_cast<const ast::ListCompExpr*>(&expr)) {
      const auto dst = new_reg();
      emit(ir::Op::MakeList, dst, add_list_items({}));
      const auto clauses = list_comp_clauses(*comp);
      return lower_comprehension_clauses(
          clauses, 0, dst, [&]() {
            emit(ir::Op::ListAppend, dst, lower_expr(*comp->result));
          });
    }
    if (auto* comp = dynamic_cast<const ast::DictCompExpr*>(&expr)) {
      const auto dst = new_reg();
      emit(ir::Op::MakeDict, dst, add_dict_items({}));
      const auto clauses = dict_comp_clauses(*comp);
      return lower_comprehension_clauses(
          clauses, 0, dst, [&]() {
            const auto key = lower_expr(*comp->key);
            const auto value = lower_expr(*comp->value);
            emit(ir::Op::DictSet, dst, key, value);
          });
    }
    if (auto* comp = dynamic_cast<const ast::SetCompExpr*>(&expr)) {
      const auto dst = new_reg();
      emit(ir::Op::MakeSet, dst, add_set_items({}));
      const auto clauses = set_comp_clauses(*comp);
      return lower_comprehension_clauses(
          clauses, 0, dst, [&]() {
            emit(ir::Op::SetAdd, dst, lower_expr(*comp->result));
          });
    }
    if (auto* comp = dynamic_cast<const ast::GeneratorExpr*>(&expr)) {
      return lower_generator_expr(*comp);
    }
    if (auto* lambda = dynamic_cast<const ast::LambdaExpr*>(&expr)) {
      ast::FunctionDef fn;
      fn.name = "<lambda>";
      fn.params = lambda->params;
      if (!lambda->signature.empty()) {
        fn.signature.reserve(lambda->signature.size());
        for (const auto& lambda_param : lambda->signature) {
          ast::FunctionDef::Param param;
          param.name = lambda_param.name;
          switch (lambda_param.kind) {
            case ast::LambdaExpr::Param::Kind::PosOnly:
              param.kind = ast::FunctionDef::Param::Kind::PosOnly;
              break;
            case ast::LambdaExpr::Param::Kind::PosOrKeyword:
              param.kind = ast::FunctionDef::Param::Kind::PosOrKeyword;
              break;
            case ast::LambdaExpr::Param::Kind::VarArgs:
              param.kind = ast::FunctionDef::Param::Kind::VarArgs;
              break;
            case ast::LambdaExpr::Param::Kind::KeywordOnly:
              param.kind = ast::FunctionDef::Param::Kind::KeywordOnly;
              break;
            case ast::LambdaExpr::Param::Kind::KwArgs:
              param.kind = ast::FunctionDef::Param::Kind::KwArgs;
              break;
          }
          if (lambda_param.default_value != nullptr) {
            param.default_value = clone_expr(*lambda_param.default_value);
          }
          fn.signature.push_back(std::move(param));
        }
      } else {
        for (const auto& name : lambda->params) {
        ast::FunctionDef::Param param;
        param.name = name;
        param.kind = ast::FunctionDef::Param::Kind::PosOrKeyword;
        fn.signature.push_back(std::move(param));
        }
      }
      fn.body.push_back(std::make_unique<ast::ReturnStmt>(clone_expr(*lambda->body)));
      return lower_function_value(fn);
    }
    const auto reg = new_reg();
    emit(ir::Op::LoadConst, reg, add_const(Value::none()));
    return reg;
  }

  Value literal_value(const ast::LiteralExpr& lit) {
    switch (lit.kind) {
      case ast::LiteralExpr::Kind::None:
        return Value::none();
      case ast::LiteralExpr::Kind::Bool:
        return Value::boolean(lit.bool_value);
      case ast::LiteralExpr::Kind::Int:
        return Value::int64(parse_integer_literal(lit.text));
      case ast::LiteralExpr::Kind::Double:
        return Value::number(std::strtod(lit.text.c_str(), nullptr));
      case ast::LiteralExpr::Kind::String:
        return Value::string(lit.text);
      case ast::LiteralExpr::Kind::Bytes:
        return Value::bytes(lit.text);
      case ast::LiteralExpr::Kind::Ellipsis:
        return Value::none();
    }
    return Value::none();
  }

  ir::Module& module_;
  bool is_module_ = false;
  std::string instance_slot_self_;
  std::unordered_map<std::string, uint32_t> instance_slots_;
  std::unordered_map<std::string, ClassInfo> class_infos_;
  std::unordered_map<std::string, uint32_t> module_global_slots_;
  std::unordered_set<uint32_t> imported_module_slots_;
  std::string qualname_prefix_;
  uint32_t current_source_line_ = 0;
  ir::Function fn_;
  sema::NameSet local_name_set_;
  sema::NameSet global_names_;
  std::unordered_map<std::string, uint32_t> locals_;
  std::unordered_map<std::string, KnownValueType> local_known_types_;
  std::unordered_map<std::string, uint32_t> cell_indices_;
  std::unordered_map<std::string, uint32_t> free_indices_;
  std::unordered_map<std::string, uint32_t> name_ids_;
  sema::NameSet hidden_locals_;
  std::unordered_map<std::string, std::string> name_aliases_;
  std::vector<const std::vector<ast::StmtPtr>*> active_finalizers_;
  std::vector<std::vector<size_t>> loop_break_jumps_;
  std::vector<uint32_t> loop_continue_targets_;
  uint32_t next_hidden_local_ = 0;
};

} // namespace

LowerResult lower_to_ir(const ast::Module& module_ast) {
  LowerResult result;
  auto global_slots = collect_module_global_slots(module_ast);
  result.module.global_slots = global_slots.names;
  FunctionLowerer lowerer(result.module, "<module>", {}, {}, {}, module_ast.body, false, 1, true, {}, {}, {}, global_slots.slots);
  lowerer.lower_body(module_ast.body);
  result.module.functions.push_back(lowerer.finish());
  result.module.entry = static_cast<uint32_t>(result.module.functions.size() - 1);
  return result;
}

} // namespace xlang3
