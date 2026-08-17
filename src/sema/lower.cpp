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
#include "xlang3/scope_analysis.h"

#include "xlang_module_globals.h"

#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace xlang3 {

namespace {

struct ClassInfo {
  std::unordered_map<std::string, uint32_t> slots;
};

void add_slot_name(const std::string& name, std::vector<std::string>& slots, std::unordered_set<std::string>& seen) {
  if (seen.insert(name).second) {
    slots.push_back(name);
  }
}

void collect_self_attr_slots_expr(
    const ast::Expr& expr,
    const std::string& self_name,
    std::vector<std::string>& slots,
    std::unordered_set<std::string>& seen) {
  if (auto* attr = dynamic_cast<const ast::AttrExpr*>(&expr)) {
    if (auto* name = dynamic_cast<const ast::NameExpr*>(attr->object.get())) {
      if (name->name == self_name) {
        add_slot_name(attr->name, slots, seen);
      }
    }
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
  if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    collect_self_attr_slots_expr(*binary->lhs, self_name, slots, seen);
    collect_self_attr_slots_expr(*binary->rhs, self_name, slots, seen);
    return;
  }
  if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    collect_self_attr_slots_expr(*call->callee, self_name, slots, seen);
    for (const auto& arg : call->args) {
      collect_self_attr_slots_expr(*arg, self_name, slots, seen);
    }
    return;
  }
  if (auto* subscript = dynamic_cast<const ast::SubscriptExpr*>(&expr)) {
    collect_self_attr_slots_expr(*subscript->object, self_name, slots, seen);
    collect_self_attr_slots_expr(*subscript->index, self_name, slots, seen);
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
  if (auto* assign = dynamic_cast<const ast::SubscriptAssignStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*assign->object, self_name, slots, seen);
    collect_self_attr_slots_expr(*assign->index, self_name, slots, seen);
    collect_self_attr_slots_expr(*assign->value, self_name, slots, seen);
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
    collect_self_attr_slots_expr(*raise->value, self_name, slots, seen);
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
    for (const auto& child : try_except->finally_body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
    return;
  }
  if (auto* with = dynamic_cast<const ast::WithStmt*>(&stmt)) {
    collect_self_attr_slots_expr(*with->manager, self_name, slots, seen);
    for (const auto& child : with->body) collect_self_attr_slots_stmt(*child, self_name, slots, seen);
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
      collect_global_names(try_except->finally_body, names);
    } else if (auto* with = dynamic_cast<const ast::WithStmt*>(stmt.get())) {
      collect_global_names(with->body, names);
    } else if (auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
      collect_global_names(loop->body, names);
    } else if (auto* loop = dynamic_cast<const ast::ForStmt*>(stmt.get())) {
      collect_global_names(loop->body, names);
    }
  }
}

class FunctionLowerer {
public:
  FunctionLowerer(
      ir::Module& module,
      std::string name,
      std::vector<std::string> params,
      std::vector<std::string> free_vars,
      const std::vector<ast::StmtPtr>& body,
      bool is_module = false,
      std::string instance_slot_self = {},
      std::unordered_map<std::string, uint32_t> instance_slots = {},
      std::unordered_map<std::string, ClassInfo> class_infos = {},
      std::unordered_map<std::string, uint32_t> module_global_slots = {},
      std::unordered_set<uint32_t> imported_module_slots = {})
      : module_(module),
        is_module_(is_module),
        instance_slot_self_(std::move(instance_slot_self)),
        instance_slots_(std::move(instance_slots)),
        class_infos_(std::move(class_infos)),
        module_global_slots_(std::move(module_global_slots)),
        imported_module_slots_(std::move(imported_module_slots)) {
    fn_.name = std::move(name);
    fn_.params = std::move(params);
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
      if (child == nullptr) {
        continue;
      }
      for (const auto& name : closure_names_for_child(*child)) {
        if (sema::contains(local_name_set_, name)) {
          ensure_cell_for_local(name);
        }
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
  }

  size_t emit_jump(ir::Op op, uint32_t cond = 0) {
    fn_.code.push_back(ir::Instr{op, 0, cond, 0, 0});
    return fn_.code.size() - 1;
  }

  void patch_jump(size_t at, uint32_t target) {
    fn_.code[at].dst = target;
  }

  void patch_iter_done(size_t at, uint32_t target) {
    fn_.code[at].b = target;
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

  std::string resolve_name(const std::string& name) const {
    auto it = name_aliases_.find(name);
    if (it != name_aliases_.end()) {
      return it->second;
    }
    return name;
  }

  void lower_for_loop(const std::string& target, const ast::Expr& iterable, const std::vector<ast::StmtPtr>& body) {
    if (try_lower_const_range_for(target, iterable, body)) {
      return;
    }
    const auto iterable_reg = lower_expr(iterable);
    const auto iterator_reg = new_reg();
    emit(ir::Op::GetIter, iterator_reg, iterable_reg);
    const auto start = static_cast<uint32_t>(fn_.code.size());
    const auto item_reg = new_reg();
    emit(ir::Op::IterNext, item_reg, iterator_reg, 0);
    const auto iter_next = fn_.code.size() - 1;
    store_named_value(target, item_reg);
    lower_body(body);
    emit(ir::Op::Jump, start);
    patch_iter_done(iter_next, static_cast<uint32_t>(fn_.code.size()));
  }

  bool parse_int_literal(const ast::Expr& expr, int64_t& value) const {
    auto* lit = dynamic_cast<const ast::LiteralExpr*>(&expr);
    if (lit == nullptr || lit->kind != ast::LiteralExpr::Kind::Int) {
      return false;
    }
    value = std::strtoll(lit->text.c_str(), nullptr, 10);
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
    lower_body(body);
    emit(ir::Op::Jump, loop_start);
    patch_jump(next, static_cast<uint32_t>(fn_.code.size()));
    return true;
  }

  void lower_try_except_core(const ast::TryExceptStmt& stmt) {
    const auto setup = emit_jump(ir::Op::SetupExcept);
    lower_body(stmt.try_body);
    emit(ir::Op::PopExcept);
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
      emit(ir::Op::ClearException);
      lower_body(handler.body);
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

  void lower_with(const ast::WithStmt& stmt) {
    const auto manager = lower_expr(*stmt.manager);
    const auto entered = emit_call_method(manager, "__enter__", {});
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
    const auto exit_result = emit_call_method(manager, "__exit__", {none_type, none_value, none_tb});
    emit(ir::Op::Pop, 0, exit_result);
    const auto done = emit_jump(ir::Op::Jump);
    patch_jump(setup, static_cast<uint32_t>(fn_.code.size()));
    const auto exc_type = new_reg();
    const auto exc_value = new_reg();
    const auto exc_tb = new_reg();
    emit(ir::Op::LoadExceptionType, exc_type);
    emit(ir::Op::LoadException, exc_value);
    emit(ir::Op::LoadConst, exc_tb, none_const);
    const auto handled = emit_call_method(manager, "__exit__", {exc_type, exc_value, exc_tb});
    const auto rereraise = emit_jump(ir::Op::JumpIfFalse, handled);
    emit(ir::Op::ClearException);
    const auto suppressed = emit_jump(ir::Op::Jump);
    patch_jump(rereraise, static_cast<uint32_t>(fn_.code.size()));
    emit(ir::Op::Reraise);
    patch_jump(done, static_cast<uint32_t>(fn_.code.size()));
    patch_jump(suppressed, static_cast<uint32_t>(fn_.code.size()));
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
      std::unordered_map<std::string, uint32_t> instance_slots = {}) {
    const auto free_vars = closure_names_for_child(fn);
    FunctionLowerer child_lowerer(
        module_, fn.name, fn.params, free_vars, fn.body, false,
        std::move(instance_slot_self), std::move(instance_slots), class_infos_, module_global_slots_,
        imported_module_slots_);
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
    emit(ir::Op::MakeFunction, reg, function_id, add_function_closure(std::move(closure_regs)));
    return reg;
  }

  void lower_class_def(const ast::ClassDef& klass) {
    std::vector<std::pair<std::string, uint32_t>> attrs;
    std::vector<std::string> instance_slots;
    std::unordered_set<std::string> seen_instance_slots;
    for (const auto& stmt : klass.body) {
      if (auto* fn = dynamic_cast<const ast::FunctionDef*>(stmt.get())) {
        if (!fn->params.empty()) {
          for (const auto& child : fn->body) {
            collect_self_attr_slots_stmt(*child, fn->params[0], instance_slots, seen_instance_slots);
          }
        }
      }
    }

    ClassInfo class_info;
    for (size_t i = 0; i < instance_slots.size(); ++i) {
      class_info.slots[instance_slots[i]] = static_cast<uint32_t>(i);
    }
    class_infos_[klass.name] = class_info;
    for (const auto& stmt : klass.body) {
      if (auto* fn = dynamic_cast<const ast::FunctionDef*>(stmt.get())) {
        attrs.push_back(std::make_pair(
            fn->name,
            lower_function_value(*fn, fn->params.empty() ? std::string{} : fn->params[0], class_info.slots)));
      } else if (auto* assign = dynamic_cast<const ast::AssignStmt*>(stmt.get())) {
        attrs.push_back(std::make_pair(assign->name, lower_expr(*assign->value)));
      }
    }
    const auto reg = new_reg();
    emit(ir::Op::MakeClass, reg, add_name(klass.name), add_class_attrs(std::move(attrs)),
         add_class_instance_slots(std::move(instance_slots)));
    store_named_value(klass.name, reg);
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

  void lower_stmt(const ast::Stmt& stmt) {
    if (dynamic_cast<const ast::GlobalStmt*>(&stmt) != nullptr) {
      return;
    }
    if (dynamic_cast<const ast::NonlocalStmt*>(&stmt) != nullptr) {
      return;
    }
    if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
      if (try_emit_direct_local_assign(*assign)) {
        return;
      }
      store_named_value(assign->name, lower_expr(*assign->value));
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
      const auto reg = new_reg();
      emit(ir::Op::ImportModule, reg, add_name(import->name));
      const auto root_dot = import->name.find('.');
      const auto root_name = root_dot == std::string::npos ? import->name : import->name.substr(0, root_dot);
      if (root_dot != std::string::npos && import->bind_name == root_name) {
        const auto bind_reg = new_reg();
        emit(ir::Op::ImportModule, bind_reg, add_name(import->bind_name));
        store_named_value(import->bind_name, bind_reg);
      } else {
        store_named_value(import->bind_name, reg);
      }
      uint32_t import_slot = 0;
      if (module_global_slot(import->bind_name, import_slot)) {
        imported_module_slots_.insert(import_slot);
      }
      return;
    }
    if (auto* import = dynamic_cast<const ast::FromImportStmt*>(&stmt)) {
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
      lower_body(loop->body);
      emit(ir::Op::Jump, start);
      patch_jump(jf, static_cast<uint32_t>(fn_.code.size()));
      return;
    }
    if (auto* loop = dynamic_cast<const ast::ForStmt*>(&stmt)) {
      lower_for_loop(loop->target, *loop->iterable, loop->body);
      return;
    }
    if (auto* fn = dynamic_cast<const ast::FunctionDef*>(&stmt)) {
      store_named_value(fn->name, lower_function_value(*fn));
      return;
    }
    if (auto* klass = dynamic_cast<const ast::ClassDef*>(&stmt)) {
      lower_class_def(*klass);
      return;
    }
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
          emit(ir::Op::LoadConst, reg, add_const(Value::int64(std::strtoll(lit->text.c_str(), nullptr, 10))));
          break;
        case ast::LiteralExpr::Kind::Double:
          emit(ir::Op::LoadConst, reg, add_const(Value::number(std::strtod(lit->text.c_str(), nullptr))));
          break;
        case ast::LiteralExpr::Kind::String:
          emit(ir::Op::LoadConst, reg, add_const(Value::string(lit->text)));
          break;
      }
      return reg;
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
      emit(unary->op == "not" ? ir::Op::Not : ir::Op::Neg, reg, src);
      return reg;
    }
    if (auto* await = dynamic_cast<const ast::AwaitExpr*>(&expr)) {
      const auto src = lower_expr(*await->expr);
      const auto reg = new_reg();
      emit(ir::Op::Await, reg, src);
      return reg;
    }
    if (auto* bin = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
      const auto lhs = lower_expr(*bin->lhs);
      const auto rhs = lower_expr(*bin->rhs);
      const auto reg = new_reg();
      if (bin->op == "+") emit(ir::Op::Add, reg, lhs, rhs);
      else if (bin->op == "-") emit(ir::Op::Sub, reg, lhs, rhs);
      else if (bin->op == "*") emit(ir::Op::Mul, reg, lhs, rhs);
      else if (bin->op == "/") emit(ir::Op::Div, reg, lhs, rhs);
      else if (bin->op == "%") emit(ir::Op::Mod, reg, lhs, rhs);
      else if (bin->op == "and") emit(ir::Op::BoolAnd, reg, lhs, rhs);
      else if (bin->op == "or") emit(ir::Op::BoolOr, reg, lhs, rhs);
      else {
        uint32_t cmp = 0;
        if (bin->op == "==") cmp = static_cast<uint32_t>(ir::CompareOp::Eq);
        else if (bin->op == "!=") cmp = static_cast<uint32_t>(ir::CompareOp::Ne);
        else if (bin->op == "<") cmp = static_cast<uint32_t>(ir::CompareOp::Lt);
        else if (bin->op == "<=") cmp = static_cast<uint32_t>(ir::CompareOp::Le);
        else if (bin->op == ">") cmp = static_cast<uint32_t>(ir::CompareOp::Gt);
        else if (bin->op == ">=") cmp = static_cast<uint32_t>(ir::CompareOp::Ge);
        emit(ir::Op::Compare, reg, lhs, rhs, cmp);
      }
      return reg;
    }
    if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
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
      std::vector<uint32_t> item_regs;
      for (const auto& item : tuple->items) {
        item_regs.push_back(lower_expr(*item));
      }
      const auto dst = new_reg();
      emit(ir::Op::MakeTuple, dst, add_tuple_items(std::move(item_regs)));
      return dst;
    }
    if (auto* list = dynamic_cast<const ast::ListExpr*>(&expr)) {
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
      const auto iterable_reg = lower_expr(*comp->iterable);
      const auto iterator_reg = new_reg();
      emit(ir::Op::GetIter, iterator_reg, iterable_reg);

      const auto hidden_name = "#comp." + std::to_string(next_hidden_local_++) + "." + comp->target;
      hidden_locals_.insert(hidden_name);
      ensure_local(hidden_name);
      const auto old_alias = name_aliases_.find(comp->target);
      const bool had_alias = old_alias != name_aliases_.end();
      const std::string old_value = had_alias ? old_alias->second : std::string{};
      name_aliases_[comp->target] = hidden_name;

      const auto start = static_cast<uint32_t>(fn_.code.size());
      const auto item_reg = new_reg();
      emit(ir::Op::IterNext, item_reg, iterator_reg, 0);
      const auto iter_next = fn_.code.size() - 1;
      store_named_value(comp->target, item_reg);
      size_t skip_append = 0;
      bool has_filter = comp->filter != nullptr;
      if (has_filter) {
        const auto filter_reg = lower_expr(*comp->filter);
        skip_append = emit_jump(ir::Op::JumpIfFalse, filter_reg);
      }
      const auto value_reg = lower_expr(*comp->result);
      emit(ir::Op::ListAppend, dst, value_reg);
      if (has_filter) {
        patch_jump(skip_append, static_cast<uint32_t>(fn_.code.size()));
      }
      emit(ir::Op::Jump, start);
      patch_iter_done(iter_next, static_cast<uint32_t>(fn_.code.size()));

      if (had_alias) {
        name_aliases_[comp->target] = old_value;
      } else {
        name_aliases_.erase(comp->target);
      }
      return dst;
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
        return Value::int64(std::strtoll(lit.text.c_str(), nullptr, 10));
      case ast::LiteralExpr::Kind::Double:
        return Value::number(std::strtod(lit.text.c_str(), nullptr));
      case ast::LiteralExpr::Kind::String:
        return Value::string(lit.text);
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
  ir::Function fn_;
  sema::NameSet local_name_set_;
  sema::NameSet global_names_;
  std::unordered_map<std::string, uint32_t> locals_;
  std::unordered_map<std::string, uint32_t> cell_indices_;
  std::unordered_map<std::string, uint32_t> free_indices_;
  std::unordered_map<std::string, uint32_t> name_ids_;
  sema::NameSet hidden_locals_;
  std::unordered_map<std::string, std::string> name_aliases_;
  std::vector<const std::vector<ast::StmtPtr>*> active_finalizers_;
  uint32_t next_hidden_local_ = 0;
};

} // namespace

LowerResult lower_to_ir(const ast::Module& module_ast) {
  LowerResult result;
  auto global_slots = collect_module_global_slots(module_ast);
  result.module.global_slots = global_slots.names;
  FunctionLowerer lowerer(result.module, "<module>", {}, {}, module_ast.body, true, {}, {}, {}, global_slots.slots);
  lowerer.lower_body(module_ast.body);
  result.module.functions.push_back(lowerer.finish());
  result.module.entry = static_cast<uint32_t>(result.module.functions.size() - 1);
  return result;
}

} // namespace xlang3
