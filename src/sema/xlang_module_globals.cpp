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

void collect_module_body(const std::vector<ast::StmtPtr>& body, ModuleGlobalSlots& slots) {
  for (const auto& stmt : body) {
    collect_module_stmt(*stmt, slots);
  }
}

void collect_module_stmt(const ast::Stmt& stmt, ModuleGlobalSlots& slots) {
  if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
    add_module_slot(slots, assign->name);
    return;
  }
  if (auto* assign = dynamic_cast<const ast::AnnotatedAssignStmt*>(&stmt)) {
    if (auto* name = dynamic_cast<const ast::NameExpr*>(assign->target.get())) {
      add_module_slot(slots, name->name);
    }
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
    return;
  }
  if (auto* klass = dynamic_cast<const ast::ClassDef*>(&stmt)) {
    add_module_slot(slots, klass->name);
    return;
  }
  if (auto* loop = dynamic_cast<const ast::ForStmt*>(&stmt)) {
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
    if (!with->target.empty()) {
      add_module_slot(slots, with->target);
    }
    collect_module_body(with->body, slots);
    return;
  }
  if (auto* ifs = dynamic_cast<const ast::IfStmt*>(&stmt)) {
    collect_module_body(ifs->then_body, slots);
    collect_module_body(ifs->else_body, slots);
    return;
  }
  if (auto* loop = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
    collect_module_body(loop->body, slots);
    return;
  }
  if (auto* try_except = dynamic_cast<const ast::TryExceptStmt*>(&stmt)) {
    collect_module_body(try_except->try_body, slots);
    for (const auto& handler : try_except->handlers) {
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
    for (const auto& match_case : match->cases) {
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
