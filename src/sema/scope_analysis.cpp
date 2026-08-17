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
#include "xlang3/scope_analysis.h"

#include <algorithm>

namespace xlang3::sema {

bool contains(const NameSet& names, const std::string& name) {
  return names.find(name) != names.end();
}

namespace {

void add_unique(std::vector<std::string>& names, NameSet& seen, const std::string& name) {
  if (seen.insert(name).second) {
    names.push_back(name);
  }
}

void collect_assigned_names(const std::vector<ast::StmtPtr>& body, std::vector<std::string>& names, NameSet& seen) {
  for (const auto& stmt : body) {
    if (auto* assign = dynamic_cast<const ast::AssignStmt*>(stmt.get())) {
      add_unique(names, seen, assign->name);
    } else if (auto* del = dynamic_cast<const ast::DelStmt*>(stmt.get())) {
      if (auto* name = dynamic_cast<const ast::NameExpr*>(del->target.get())) {
        add_unique(names, seen, name->name);
      }
    } else if (auto* import = dynamic_cast<const ast::ImportStmt*>(stmt.get())) {
      add_unique(names, seen, import->bind_name);
    } else if (auto* import = dynamic_cast<const ast::FromImportStmt*>(stmt.get())) {
      for (const auto& binding : import->names) {
        if (binding.as_name != "*") {
          add_unique(names, seen, binding.as_name);
        }
      }
    } else if (auto* fn = dynamic_cast<const ast::FunctionDef*>(stmt.get())) {
      add_unique(names, seen, fn->name);
    } else if (auto* klass = dynamic_cast<const ast::ClassDef*>(stmt.get())) {
      add_unique(names, seen, klass->name);
    } else if (auto* ifs = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
      collect_assigned_names(ifs->then_body, names, seen);
      collect_assigned_names(ifs->else_body, names, seen);
    } else if (auto* try_except = dynamic_cast<const ast::TryExceptStmt*>(stmt.get())) {
      collect_assigned_names(try_except->try_body, names, seen);
      for (const auto& handler : try_except->handlers) {
        if (!handler.name.empty()) {
          add_unique(names, seen, handler.name);
        }
        collect_assigned_names(handler.body, names, seen);
      }
      collect_assigned_names(try_except->else_body, names, seen);
      collect_assigned_names(try_except->finally_body, names, seen);
    } else if (auto* with = dynamic_cast<const ast::WithStmt*>(stmt.get())) {
      if (!with->target.empty()) {
        add_unique(names, seen, with->target);
      }
      collect_assigned_names(with->body, names, seen);
    } else if (auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
      collect_assigned_names(loop->body, names, seen);
    } else if (auto* loop = dynamic_cast<const ast::ForStmt*>(stmt.get())) {
      add_unique(names, seen, loop->target);
      collect_assigned_names(loop->body, names, seen);
    } else if (auto* match = dynamic_cast<const ast::MatchStmt*>(stmt.get())) {
      for (const auto& match_case : match->cases) {
        collect_assigned_names(match_case.body, names, seen);
      }
    }
  }
}

void collect_nonlocal_names(const std::vector<ast::StmtPtr>& body, NameSet& names) {
  for (const auto& stmt : body) {
    if (auto* nonlocal = dynamic_cast<const ast::NonlocalStmt*>(stmt.get())) {
      for (const auto& name : nonlocal->names) {
        names.insert(name);
      }
    } else if (auto* ifs = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
      collect_nonlocal_names(ifs->then_body, names);
      collect_nonlocal_names(ifs->else_body, names);
    } else if (auto* try_except = dynamic_cast<const ast::TryExceptStmt*>(stmt.get())) {
      collect_nonlocal_names(try_except->try_body, names);
      for (const auto& handler : try_except->handlers) {
        collect_nonlocal_names(handler.body, names);
      }
      collect_nonlocal_names(try_except->else_body, names);
      collect_nonlocal_names(try_except->finally_body, names);
    } else if (auto* with = dynamic_cast<const ast::WithStmt*>(stmt.get())) {
      collect_nonlocal_names(with->body, names);
    } else if (auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
      collect_nonlocal_names(loop->body, names);
    } else if (auto* loop = dynamic_cast<const ast::ForStmt*>(stmt.get())) {
      collect_nonlocal_names(loop->body, names);
    } else if (auto* match = dynamic_cast<const ast::MatchStmt*>(stmt.get())) {
      for (const auto& match_case : match->cases) {
        collect_nonlocal_names(match_case.body, names);
      }
    }
  }
}

void collect_global_names(const std::vector<ast::StmtPtr>& body, NameSet& names) {
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

void collect_reads_expr(const ast::Expr& expr, std::vector<std::string>& names, NameSet& seen) {
  if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
    add_unique(names, seen, name->name);
  } else if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
    collect_reads_expr(*unary->expr, names, seen);
  } else if (auto* await = dynamic_cast<const ast::AwaitExpr*>(&expr)) {
    collect_reads_expr(*await->expr, names, seen);
  } else if (auto* yield = dynamic_cast<const ast::YieldExpr*>(&expr)) {
    collect_reads_expr(*yield->expr, names, seen);
  } else if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    collect_reads_expr(*binary->lhs, names, seen);
    collect_reads_expr(*binary->rhs, names, seen);
  } else if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    collect_reads_expr(*call->callee, names, seen);
    for (const auto& arg : call->args) {
      collect_reads_expr(*arg, names, seen);
    }
    for (const auto& arg : call->call_args) {
      collect_reads_expr(*arg.value, names, seen);
    }
  } else if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
    collect_reads_expr(*subscript->object, names, seen);
    collect_reads_expr(*subscript->index, names, seen);
  } else if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
    collect_reads_expr(*attr->object, names, seen);
  } else if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    for (const auto& item : tuple->items) {
      collect_reads_expr(*item, names, seen);
    }
  } else if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
    for (const auto& item : list->items) {
      collect_reads_expr(*item, names, seen);
    }
  } else if (auto* dict = dynamic_cast<const ast::DictExpr*>(&expr)) {
    for (const auto& entry : dict->entries) {
      collect_reads_expr(*entry.first, names, seen);
      collect_reads_expr(*entry.second, names, seen);
    }
  } else if (auto* set = dynamic_cast<const ast::SetExpr*>(&expr)) {
    for (const auto& item : set->items) {
      collect_reads_expr(*item, names, seen);
    }
  } else if (auto* comp = dynamic_cast<const ast::ListCompExpr*>(&expr)) {
    collect_reads_expr(*comp->iterable, names, seen);
    std::vector<std::string> result_reads;
    NameSet result_seen;
    collect_reads_expr(*comp->result, result_reads, result_seen);
    for (const auto& name : result_reads) {
      if (name != comp->target) {
        add_unique(names, seen, name);
      }
    }
    if (comp->filter != nullptr) {
      std::vector<std::string> filter_reads;
      NameSet filter_seen;
      collect_reads_expr(*comp->filter, filter_reads, filter_seen);
      for (const auto& name : filter_reads) {
        if (name != comp->target) {
          add_unique(names, seen, name);
        }
      }
    }
  } else if (auto* lambda = dynamic_cast<const ast::LambdaExpr*>(&expr)) {
    std::vector<std::string> lambda_reads;
    NameSet lambda_seen;
    collect_reads_expr(*lambda->body, lambda_reads, lambda_seen);
    for (const auto& name : lambda_reads) {
      if (!contains(NameSet(lambda->params.begin(), lambda->params.end()), name)) {
        add_unique(names, seen, name);
      }
    }
  }
}

void collect_reads_body(const std::vector<ast::StmtPtr>& body, std::vector<std::string>& names, NameSet& seen) {
  for (const auto& stmt : body) {
    if (auto* assign = dynamic_cast<const ast::AssignStmt*>(stmt.get())) {
      collect_reads_expr(*assign->value, names, seen);
    } else if (auto* assign = dynamic_cast<const ast::SubscriptAssignStmt*>(stmt.get())) {
      collect_reads_expr(*assign->object, names, seen);
      collect_reads_expr(*assign->index, names, seen);
      collect_reads_expr(*assign->value, names, seen);
    } else if (auto* assign = dynamic_cast<const ast::AttrAssignStmt*>(stmt.get())) {
      collect_reads_expr(*assign->object, names, seen);
      collect_reads_expr(*assign->value, names, seen);
    } else if (auto* del = dynamic_cast<const ast::DelStmt*>(stmt.get())) {
      if (dynamic_cast<const ast::NameExpr*>(del->target.get()) == nullptr) {
        collect_reads_expr(*del->target, names, seen);
      }
    } else if (auto* assert_stmt = dynamic_cast<const ast::AssertStmt*>(stmt.get())) {
      collect_reads_expr(*assert_stmt->condition, names, seen);
      if (assert_stmt->message != nullptr) {
        collect_reads_expr(*assert_stmt->message, names, seen);
      }
    } else if (auto* expr_stmt = dynamic_cast<const ast::ExprStmt*>(stmt.get())) {
      collect_reads_expr(*expr_stmt->expr, names, seen);
    } else if (auto* ret = dynamic_cast<const ast::ReturnStmt*>(stmt.get())) {
      collect_reads_expr(*ret->value, names, seen);
    } else if (auto* raise = dynamic_cast<const ast::RaiseStmt*>(stmt.get())) {
      if (raise->value != nullptr) {
        collect_reads_expr(*raise->value, names, seen);
      }
      if (raise->cause != nullptr) {
        collect_reads_expr(*raise->cause, names, seen);
      }
    } else if (auto* ifs = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
      collect_reads_expr(*ifs->condition, names, seen);
      collect_reads_body(ifs->then_body, names, seen);
      collect_reads_body(ifs->else_body, names, seen);
    } else if (auto* try_except = dynamic_cast<const ast::TryExceptStmt*>(stmt.get())) {
      collect_reads_body(try_except->try_body, names, seen);
      for (const auto& handler : try_except->handlers) {
        if (handler.type != nullptr) {
          collect_reads_expr(*handler.type, names, seen);
        }
        collect_reads_body(handler.body, names, seen);
      }
      collect_reads_body(try_except->else_body, names, seen);
      collect_reads_body(try_except->finally_body, names, seen);
    } else if (auto* with = dynamic_cast<const ast::WithStmt*>(stmt.get())) {
      collect_reads_expr(*with->manager, names, seen);
      collect_reads_body(with->body, names, seen);
    } else if (auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
      collect_reads_expr(*loop->condition, names, seen);
      collect_reads_body(loop->body, names, seen);
    } else if (auto* loop = dynamic_cast<const ast::ForStmt*>(stmt.get())) {
      collect_reads_expr(*loop->iterable, names, seen);
      collect_reads_body(loop->body, names, seen);
    } else if (auto* fn = dynamic_cast<const ast::FunctionDef*>(stmt.get())) {
      for (const auto& param : fn->signature) {
        if (param.default_value != nullptr) {
          collect_reads_expr(*param.default_value, names, seen);
        }
        if (param.annotation != nullptr) {
          collect_reads_expr(*param.annotation, names, seen);
        }
      }
      if (fn->return_annotation != nullptr) {
        collect_reads_expr(*fn->return_annotation, names, seen);
      }
      for (const auto& decorator : fn->decorators) {
        collect_reads_expr(*decorator, names, seen);
      }
    } else if (auto* klass = dynamic_cast<const ast::ClassDef*>(stmt.get())) {
      for (const auto& base : klass->bases) {
        collect_reads_expr(*base, names, seen);
      }
      for (const auto& keyword : klass->keywords) {
        collect_reads_expr(*keyword.second, names, seen);
      }
      for (const auto& decorator : klass->decorators) {
        collect_reads_expr(*decorator, names, seen);
      }
    } else if (auto* match = dynamic_cast<const ast::MatchStmt*>(stmt.get())) {
      collect_reads_expr(*match->subject, names, seen);
      for (const auto& match_case : match->cases) {
        if (match_case.pattern != nullptr) {
          collect_reads_expr(*match_case.pattern, names, seen);
        }
        collect_reads_body(match_case.body, names, seen);
      }
    }
  }
}

} // namespace

std::vector<std::string> local_names_for(const std::vector<std::string>& params, const std::vector<ast::StmtPtr>& body) {
  std::vector<std::string> names;
  NameSet seen;
  NameSet nonlocals;
  NameSet globals;
  collect_nonlocal_names(body, nonlocals);
  collect_global_names(body, globals);
  for (const auto& param : params) {
    if (!contains(nonlocals, param) && !contains(globals, param)) {
      add_unique(names, seen, param);
    }
  }
  collect_assigned_names(body, names, seen);
  names.erase(
      std::remove_if(names.begin(), names.end(), [&](const std::string& name) {
        return contains(nonlocals, name) || contains(globals, name);
      }),
      names.end());
  return names;
}

std::vector<std::string> free_candidates_for(const ast::FunctionDef& fn) {
  const auto locals = local_names_for(fn.params, fn.body);
  NameSet local_set(locals.begin(), locals.end());

  std::vector<std::string> reads;
  NameSet seen_reads;
  collect_reads_body(fn.body, reads, seen_reads);

  std::vector<std::string> free_names;
  NameSet nonlocals;
  NameSet globals;
  collect_nonlocal_names(fn.body, nonlocals);
  collect_global_names(fn.body, globals);
  for (const auto& name : nonlocals) {
    free_names.push_back(name);
  }
  for (const auto& name : reads) {
    if (!contains(local_set, name) && !contains(nonlocals, name) && !contains(globals, name)) {
      free_names.push_back(name);
    }
  }
  return free_names;
}

} // namespace xlang3::sema
