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
#include "xlang_module_globals.h"

namespace xlang3 {
namespace {

void add_module_slot(ModuleGlobalSlots& slots, const std::string& name) {
  if (slots.slots.find(name) != slots.slots.end()) {
    return;
  }
  const auto slot = static_cast<uint32_t>(slots.names.size());
  slots.slots[name] = slot;
  slots.names.push_back(name);
}

void collect_module_stmt(const ast::Stmt& stmt, ModuleGlobalSlots& slots);
void collect_module_target(const ast::Expr& expr, ModuleGlobalSlots& slots);
void collect_module_expr(const ast::Expr& expr, ModuleGlobalSlots& slots);

void collect_comp_clause(const ast::CompClause& clause, ModuleGlobalSlots& slots) {
  collect_module_expr(*clause.iterable, slots);
  if (clause.filter != nullptr) collect_module_expr(*clause.filter, slots);
}

void collect_module_expr(const ast::Expr& expr, ModuleGlobalSlots& slots) {
  if (auto* named = dynamic_cast<const ast::NamedExpr*>(&expr)) {
    add_module_slot(slots, named->name);
    collect_module_expr(*named->value, slots);
  } else if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
    collect_module_expr(*unary->expr, slots);
  } else if (auto* await = dynamic_cast<const ast::AwaitExpr*>(&expr)) {
    collect_module_expr(*await->expr, slots);
  } else if (auto* yield = dynamic_cast<const ast::YieldExpr*>(&expr)) {
    if (yield->expr != nullptr) collect_module_expr(*yield->expr, slots);
  } else if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    collect_module_expr(*binary->lhs, slots);
    collect_module_expr(*binary->rhs, slots);
  } else if (auto* chain = dynamic_cast<const ast::CompareChainExpr*>(&expr)) {
    collect_module_expr(*chain->first, slots);
    for (const auto& comparison : chain->comparisons) collect_module_expr(*comparison.second, slots);
  } else if (auto* conditional = dynamic_cast<const ast::ConditionalExpr*>(&expr)) {
    collect_module_expr(*conditional->then_expr, slots);
    collect_module_expr(*conditional->condition, slots);
    collect_module_expr(*conditional->else_expr, slots);
  } else if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&expr)) {
    collect_module_expr(*starred->expr, slots);
  } else if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    collect_module_expr(*call->callee, slots);
    for (const auto& arg : call->args) collect_module_expr(*arg, slots);
    for (const auto& arg : call->call_args) collect_module_expr(*arg.value, slots);
  } else if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
    collect_module_expr(*subscript->object, slots);
    collect_module_expr(*subscript->index, slots);
  } else if (auto* slice = dynamic_cast<const ast::SliceExpr*>(&expr)) {
    if (slice->start != nullptr) collect_module_expr(*slice->start, slots);
    if (slice->stop != nullptr) collect_module_expr(*slice->stop, slots);
    if (slice->step != nullptr) collect_module_expr(*slice->step, slots);
  } else if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
    collect_module_expr(*attr->object, slots);
  } else if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    for (const auto& item : tuple->items) collect_module_expr(*item, slots);
  } else if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
    for (const auto& item : list->items) collect_module_expr(*item, slots);
  } else if (auto* dict = dynamic_cast<const ast::DictExpr*>(&expr)) {
    for (const auto& entry : dict->entries) {
      if (entry.first != nullptr) collect_module_expr(*entry.first, slots);
      collect_module_expr(*entry.second, slots);
    }
  } else if (auto* set = dynamic_cast<const ast::SetExpr*>(&expr)) {
    for (const auto& item : set->items) collect_module_expr(*item, slots);
  } else if (auto* comp = dynamic_cast<const ast::ListCompExpr*>(&expr)) {
    collect_module_expr(*comp->result, slots);
    collect_module_expr(*comp->iterable, slots);
    if (comp->filter != nullptr) collect_module_expr(*comp->filter, slots);
    for (const auto& clause : comp->extra_clauses) collect_comp_clause(clause, slots);
  } else if (auto* comp = dynamic_cast<const ast::DictCompExpr*>(&expr)) {
    collect_module_expr(*comp->key, slots);
    collect_module_expr(*comp->value, slots);
    collect_module_expr(*comp->iterable, slots);
    if (comp->filter != nullptr) collect_module_expr(*comp->filter, slots);
    for (const auto& clause : comp->extra_clauses) collect_comp_clause(clause, slots);
  } else if (auto* comp = dynamic_cast<const ast::SetCompExpr*>(&expr)) {
    collect_module_expr(*comp->result, slots);
    collect_module_expr(*comp->iterable, slots);
    if (comp->filter != nullptr) collect_module_expr(*comp->filter, slots);
    for (const auto& clause : comp->extra_clauses) collect_comp_clause(clause, slots);
  } else if (auto* comp = dynamic_cast<const ast::GeneratorExpr*>(&expr)) {
    collect_module_expr(*comp->result, slots);
    collect_module_expr(*comp->iterable, slots);
    if (comp->filter != nullptr) collect_module_expr(*comp->filter, slots);
    for (const auto& clause : comp->extra_clauses) collect_comp_clause(clause, slots);
  } else if (auto* lambda = dynamic_cast<const ast::LambdaExpr*>(&expr)) {
    for (const auto& param : lambda->signature) {
      if (param.default_value != nullptr) collect_module_expr(*param.default_value, slots);
    }
  }
  // Lambda bodies execute in their own scope, so only their defaults are
  // evaluated here and may bind names in the containing module.
}

void collect_module_body(const std::vector<ast::StmtPtr>& body, ModuleGlobalSlots& slots) {
  for (const auto& stmt : body) {
    collect_module_stmt(*stmt, slots);
  }
}

void collect_module_stmt(const ast::Stmt& stmt, ModuleGlobalSlots& slots) {
  if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
    add_module_slot(slots, assign->name);
    collect_module_expr(*assign->value, slots);
    return;
  }
  if (auto* assign = dynamic_cast<const ast::AnnotatedAssignStmt*>(&stmt)) {
    if (auto* name = dynamic_cast<const ast::NameExpr*>(assign->target.get())) {
      add_module_slot(slots, name->name);
    }
    if (assign->annotation != nullptr) collect_module_expr(*assign->annotation, slots);
    if (assign->value != nullptr) collect_module_expr(*assign->value, slots);
    return;
  }
  if (auto* assign = dynamic_cast<const ast::UnpackAssignStmt*>(&stmt)) {
    collect_module_target(*assign->target, slots);
    collect_module_expr(*assign->value, slots);
    return;
  }
  if (auto* assign = dynamic_cast<const ast::MultiAssignStmt*>(&stmt)) {
    for (const auto& target : assign->targets) collect_module_target(*target, slots);
    collect_module_expr(*assign->value, slots);
    return;
  }
  if (auto* assign = dynamic_cast<const ast::SubscriptAssignStmt*>(&stmt)) {
    collect_module_expr(*assign->object, slots);
    collect_module_expr(*assign->index, slots);
    collect_module_expr(*assign->value, slots);
    return;
  }
  if (auto* assign = dynamic_cast<const ast::AttrAssignStmt*>(&stmt)) {
    collect_module_expr(*assign->object, slots);
    collect_module_expr(*assign->value, slots);
    return;
  }
  if (auto* assign = dynamic_cast<const ast::AugAssignStmt*>(&stmt)) {
    collect_module_target(*assign->target, slots);
    collect_module_expr(*assign->target, slots);
    collect_module_expr(*assign->value, slots);
    return;
  }
  if (auto* expression = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
    collect_module_expr(*expression->expr, slots);
    return;
  }
  if (auto* assertion = dynamic_cast<const ast::AssertStmt*>(&stmt)) {
    collect_module_expr(*assertion->condition, slots);
    if (assertion->message != nullptr) collect_module_expr(*assertion->message, slots);
    return;
  }
  if (auto* returned = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
    if (returned->value != nullptr) collect_module_expr(*returned->value, slots);
    return;
  }
  if (auto* raised = dynamic_cast<const ast::RaiseStmt*>(&stmt)) {
    if (raised->value != nullptr) collect_module_expr(*raised->value, slots);
    if (raised->cause != nullptr) collect_module_expr(*raised->cause, slots);
    return;
  }
  if (auto* del = dynamic_cast<const ast::DelStmt*>(&stmt)) {
    if (auto* name = dynamic_cast<const ast::NameExpr*>(del->target.get())) {
      add_module_slot(slots, name->name);
    }
    return;
  }
  if (auto* import = dynamic_cast<const ast::ImportStmt*>(&stmt)) {
    add_module_slot(slots, import->bind_name);
    if (import->thru != nullptr) collect_module_expr(*import->thru, slots);
    return;
  }
  if (auto* import = dynamic_cast<const ast::ImportManyStmt*>(&stmt)) {
    for (const auto& binding : import->names) {
      add_module_slot(slots, binding.as_name);
    }
    return;
  }
  if (auto* import = dynamic_cast<const ast::FromImportStmt*>(&stmt)) {
    for (const auto& binding : import->names) {
      if (binding.as_name != "*") {
        add_module_slot(slots, binding.as_name);
      }
    }
    return;
  }
  if (auto* fn = dynamic_cast<const ast::FunctionDef*>(&stmt)) {
    add_module_slot(slots, fn->name);
    for (const auto& decorator : fn->decorators) collect_module_expr(*decorator, slots);
    for (const auto& param : fn->signature) {
      if (param.default_value != nullptr) collect_module_expr(*param.default_value, slots);
      if (param.annotation != nullptr) collect_module_expr(*param.annotation, slots);
    }
    if (fn->return_annotation != nullptr) collect_module_expr(*fn->return_annotation, slots);
    return;
  }
  if (auto* klass = dynamic_cast<const ast::ClassDef*>(&stmt)) {
    add_module_slot(slots, klass->name);
    for (const auto& base : klass->bases) collect_module_expr(*base, slots);
    for (const auto& keyword : klass->keywords) collect_module_expr(*keyword.second, slots);
    for (const auto& decorator : klass->decorators) collect_module_expr(*decorator, slots);
    return;
  }
  if (auto* loop = dynamic_cast<const ast::ForStmt*>(&stmt)) {
    collect_module_expr(*loop->iterable, slots);
    if (loop->target_expr != nullptr) {
      collect_module_target(*loop->target_expr, slots);
    } else if (!loop->target.empty()) {
      add_module_slot(slots, loop->target);
    }
    collect_module_body(loop->body, slots);
    collect_module_body(loop->else_body, slots);
    return;
  }
  if (auto* with = dynamic_cast<const ast::WithStmt*>(&stmt)) {
    collect_module_expr(*with->manager, slots);
    if (with->target_expr != nullptr) {
      collect_module_target(*with->target_expr, slots);
    } else if (!with->target.empty()) {
      add_module_slot(slots, with->target);
    }
    collect_module_body(with->body, slots);
    return;
  }
  if (auto* ifs = dynamic_cast<const ast::IfStmt*>(&stmt)) {
    collect_module_expr(*ifs->condition, slots);
    collect_module_body(ifs->then_body, slots);
    collect_module_body(ifs->else_body, slots);
    return;
  }
  if (auto* loop = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
    collect_module_expr(*loop->condition, slots);
    collect_module_body(loop->body, slots);
    collect_module_body(loop->else_body, slots);
    return;
  }
  if (auto* try_except = dynamic_cast<const ast::TryExceptStmt*>(&stmt)) {
    collect_module_body(try_except->try_body, slots);
    for (const auto& handler : try_except->handlers) {
      if (handler.type != nullptr) collect_module_expr(*handler.type, slots);
      if (!handler.name.empty()) {
        add_module_slot(slots, handler.name);
      }
      collect_module_body(handler.body, slots);
    }
    collect_module_body(try_except->else_body, slots);
    collect_module_body(try_except->finally_body, slots);
    return;
  }
  if (auto* match = dynamic_cast<const ast::MatchStmt*>(&stmt)) {
    collect_module_expr(*match->subject, slots);
    for (const auto& match_case : match->cases) {
      if (match_case.guard != nullptr) collect_module_expr(*match_case.guard, slots);
      collect_module_body(match_case.body, slots);
    }
  }
}

void collect_module_target(const ast::Expr& expr, ModuleGlobalSlots& slots) {
  if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
    add_module_slot(slots, name->name);
  } else if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    for (const auto& item : tuple->items) collect_module_target(*item, slots);
  } else if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
    for (const auto& item : list->items) collect_module_target(*item, slots);
  } else if (auto* starred = dynamic_cast<const ast::StarredExpr*>(&expr)) {
    collect_module_target(*starred->expr, slots);
  }
}

} // namespace

ModuleGlobalSlots collect_module_global_slots(const ast::Module& module) {
  ModuleGlobalSlots slots;
  add_module_slot(slots, "__name__");
  add_module_slot(slots, "__doc__");
  add_module_slot(slots, "__file__");
  add_module_slot(slots, "__package__");
  add_module_slot(slots, "__path__");
  collect_module_body(module.body, slots);
  return slots;
}

} // namespace xlang3
