#include "xlang3/sema.h"

#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace xlang3 {

namespace {

using NameSet = std::unordered_set<std::string>;

bool contains(const NameSet& names, const std::string& name) {
  return names.find(name) != names.end();
}

void add_unique(std::vector<std::string>& names, NameSet& seen, const std::string& name) {
  if (seen.insert(name).second) {
    names.push_back(name);
  }
}

void collect_assigned_names(const std::vector<ast::StmtPtr>& body, std::vector<std::string>& names, NameSet& seen) {
  for (const auto& stmt : body) {
    if (auto* assign = dynamic_cast<const ast::AssignStmt*>(stmt.get())) {
      add_unique(names, seen, assign->name);
    } else if (auto* fn = dynamic_cast<const ast::FunctionDef*>(stmt.get())) {
      add_unique(names, seen, fn->name);
    } else if (auto* ifs = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
      collect_assigned_names(ifs->then_body, names, seen);
      collect_assigned_names(ifs->else_body, names, seen);
    } else if (auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
      collect_assigned_names(loop->body, names, seen);
    }
  }
}

void collect_reads_expr(const ast::Expr& expr, std::vector<std::string>& names, NameSet& seen) {
  if (auto* name = dynamic_cast<const ast::NameExpr*>(&expr)) {
    add_unique(names, seen, name->name);
  } else if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
    collect_reads_expr(*unary->expr, names, seen);
  } else if (auto* binary = dynamic_cast<const ast::BinaryExpr*>(&expr)) {
    collect_reads_expr(*binary->lhs, names, seen);
    collect_reads_expr(*binary->rhs, names, seen);
  } else if (auto* call = dynamic_cast<const ast::CallExpr*>(&expr)) {
    collect_reads_expr(*call->callee, names, seen);
    for (const auto& arg : call->args) {
      collect_reads_expr(*arg, names, seen);
    }
  } else if (auto* tuple = dynamic_cast<const ast::TupleExpr*>(&expr)) {
    for (const auto& item : tuple->items) {
      collect_reads_expr(*item, names, seen);
    }
  }
}

void collect_reads_body(const std::vector<ast::StmtPtr>& body, std::vector<std::string>& names, NameSet& seen) {
  for (const auto& stmt : body) {
    if (auto* assign = dynamic_cast<const ast::AssignStmt*>(stmt.get())) {
      collect_reads_expr(*assign->value, names, seen);
    } else if (auto* expr_stmt = dynamic_cast<const ast::ExprStmt*>(stmt.get())) {
      collect_reads_expr(*expr_stmt->expr, names, seen);
    } else if (auto* ret = dynamic_cast<const ast::ReturnStmt*>(stmt.get())) {
      collect_reads_expr(*ret->value, names, seen);
    } else if (auto* ifs = dynamic_cast<const ast::IfStmt*>(stmt.get())) {
      collect_reads_expr(*ifs->condition, names, seen);
      collect_reads_body(ifs->then_body, names, seen);
      collect_reads_body(ifs->else_body, names, seen);
    } else if (auto* loop = dynamic_cast<const ast::WhileStmt*>(stmt.get())) {
      collect_reads_expr(*loop->condition, names, seen);
      collect_reads_body(loop->body, names, seen);
    }
  }
}

std::vector<std::string> local_names_for(const std::vector<std::string>& params, const std::vector<ast::StmtPtr>& body) {
  std::vector<std::string> names;
  NameSet seen;
  for (const auto& param : params) {
    add_unique(names, seen, param);
  }
  collect_assigned_names(body, names, seen);
  return names;
}

std::vector<std::string> free_candidates_for(const ast::FunctionDef& fn) {
  const auto locals = local_names_for(fn.params, fn.body);
  NameSet local_set(locals.begin(), locals.end());

  std::vector<std::string> reads;
  NameSet seen_reads;
  collect_reads_body(fn.body, reads, seen_reads);

  std::vector<std::string> free_names;
  for (const auto& name : reads) {
    if (!contains(local_set, name)) {
      free_names.push_back(name);
    }
  }
  return free_names;
}

class FunctionLowerer {
public:
  FunctionLowerer(
      ir::Module& module,
      std::string name,
      std::vector<std::string> params,
      std::vector<std::string> free_vars,
      const std::vector<ast::StmtPtr>& body,
      bool is_module = false)
      : module_(module), is_module_(is_module) {
    fn_.name = std::move(name);
    fn_.params = std::move(params);
    fn_.free_vars = std::move(free_vars);
    for (size_t i = 0; i < fn_.free_vars.size(); ++i) {
      free_indices_[fn_.free_vars[i]] = static_cast<uint32_t>(i);
    }

    const auto locals = is_module_ ? std::vector<std::string>{} : local_names_for(fn_.params, body);
    for (const auto& local : locals) {
      ensure_local(local);
    }
    local_name_set_.insert(locals.begin(), locals.end());

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

  uint32_t add_call_args(std::vector<uint32_t> args) {
    fn_.call_args.push_back(std::move(args));
    return static_cast<uint32_t>(fn_.call_args.size() - 1);
  }

  uint32_t add_tuple_items(std::vector<uint32_t> items) {
    fn_.tuple_items.push_back(std::move(items));
    return static_cast<uint32_t>(fn_.tuple_items.size() - 1);
  }

  uint32_t add_function_closure(std::vector<uint32_t> cells) {
    fn_.function_closures.push_back(std::move(cells));
    return static_cast<uint32_t>(fn_.function_closures.size() - 1);
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
        if (contains(local_name_set_, name)) {
          ensure_cell_for_local(name);
        }
      }
    }
  }

  std::vector<std::string> closure_names_for_child(const ast::FunctionDef& child) const {
    std::vector<std::string> names;
    for (const auto& name : free_candidates_for(child)) {
      if (contains(local_name_set_, name) || free_indices_.find(name) != free_indices_.end()) {
        names.push_back(name);
      }
    }
    return names;
  }

  bool is_cell_local(const std::string& name) const {
    return cell_indices_.find(name) != cell_indices_.end();
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

  void emit_return_none() {
    const auto reg = new_reg();
    emit(ir::Op::LoadConst, reg, add_const(Value::none()));
    emit(ir::Op::Return, 0, reg);
  }

  void store_named_value(const std::string& name, uint32_t reg) {
    if (is_module_) {
      emit(ir::Op::StoreGlobal, add_name(name), reg);
    } else if (is_cell_local(name)) {
      emit(ir::Op::StoreCell, cell_indices_[name], reg);
    } else {
      emit(ir::Op::StoreLocal, ensure_local(name), reg);
    }
  }

  void lower_stmt(const ast::Stmt& stmt) {
    if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
      store_named_value(assign->name, lower_expr(*assign->value));
      return;
    }
    if (auto* expr_stmt = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
      const auto reg = lower_expr(*expr_stmt->expr);
      emit(ir::Op::Pop, 0, reg);
      return;
    }
    if (auto* ret = dynamic_cast<const ast::ReturnStmt*>(&stmt)) {
      const auto reg = lower_expr(*ret->value);
      emit(ir::Op::Return, 0, reg);
      return;
    }
    if (auto* ifs = dynamic_cast<const ast::IfStmt*>(&stmt)) {
      const auto cond = lower_expr(*ifs->condition);
      const auto jf = emit_jump(ir::Op::JumpIfFalse, cond);
      lower_body(ifs->then_body);
      const auto jend = emit_jump(ir::Op::Jump);
      patch_jump(jf, static_cast<uint32_t>(fn_.code.size()));
      lower_body(ifs->else_body);
      patch_jump(jend, static_cast<uint32_t>(fn_.code.size()));
      return;
    }
    if (auto* loop = dynamic_cast<const ast::WhileStmt*>(&stmt)) {
      const auto start = static_cast<uint32_t>(fn_.code.size());
      const auto cond = lower_expr(*loop->condition);
      const auto jf = emit_jump(ir::Op::JumpIfFalse, cond);
      lower_body(loop->body);
      emit(ir::Op::Jump, start);
      patch_jump(jf, static_cast<uint32_t>(fn_.code.size()));
      return;
    }
    if (auto* fn = dynamic_cast<const ast::FunctionDef*>(&stmt)) {
      const auto free_vars = closure_names_for_child(*fn);
      FunctionLowerer child_lowerer(module_, fn->name, fn->params, free_vars, fn->body);
      child_lowerer.lower_body(fn->body);
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
      store_named_value(fn->name, reg);
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
      auto local = locals_.find(name->name);
      if (local != locals_.end()) {
        if (is_cell_local(name->name)) {
          emit(ir::Op::LoadCell, reg, cell_indices_[name->name]);
        } else {
          emit(ir::Op::LoadLocal, reg, local->second);
        }
      } else if (auto free_it = free_indices_.find(name->name); free_it != free_indices_.end()) {
        emit(ir::Op::LoadFree, reg, free_it->second);
      } else {
        emit(ir::Op::LoadGlobal, reg, add_name(name->name));
      }
      return reg;
    }
    if (auto* unary = dynamic_cast<const ast::UnaryExpr*>(&expr)) {
      const auto src = lower_expr(*unary->expr);
      const auto reg = new_reg();
      emit(unary->op == "not" ? ir::Op::Not : ir::Op::Neg, reg, src);
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
      const auto callee = lower_expr(*call->callee);
      std::vector<uint32_t> arg_regs;
      for (const auto& arg : call->args) {
        arg_regs.push_back(lower_expr(*arg));
      }
      const auto dst = new_reg();
      emit(ir::Op::Call, dst, callee, add_call_args(std::move(arg_regs)));
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
    const auto reg = new_reg();
    emit(ir::Op::LoadConst, reg, add_const(Value::none()));
    return reg;
  }

  ir::Module& module_;
  bool is_module_ = false;
  ir::Function fn_;
  NameSet local_name_set_;
  std::unordered_map<std::string, uint32_t> locals_;
  std::unordered_map<std::string, uint32_t> cell_indices_;
  std::unordered_map<std::string, uint32_t> free_indices_;
  std::unordered_map<std::string, uint32_t> name_ids_;
};

} // namespace

LowerResult lower_to_ir(const ast::Module& module_ast) {
  LowerResult result;
  FunctionLowerer lowerer(result.module, "<module>", {}, {}, module_ast.body, true);
  lowerer.lower_body(module_ast.body);
  result.module.functions.push_back(lowerer.finish());
  result.module.entry = static_cast<uint32_t>(result.module.functions.size() - 1);
  return result;
}

} // namespace xlang3
