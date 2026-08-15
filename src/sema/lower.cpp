#include "xlang3/sema.h"

#include <cstdlib>
#include <memory>
#include <unordered_map>

namespace xlang3 {

namespace {

class FunctionLowerer {
public:
  FunctionLowerer(ir::Module& module, std::string name, std::vector<std::string> params, bool is_module = false)
      : module_(module), is_module_(is_module) {
    fn_.name = std::move(name);
    fn_.params = std::move(params);
    for (const auto& param : fn_.params) {
      ensure_local(param);
    }
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

  void lower_stmt(const ast::Stmt& stmt) {
    if (auto* assign = dynamic_cast<const ast::AssignStmt*>(&stmt)) {
      const auto reg = lower_expr(*assign->value);
      if (is_module_) {
        emit(ir::Op::StoreGlobal, add_name(assign->name), reg);
      } else {
        emit(ir::Op::StoreLocal, ensure_local(assign->name), reg);
      }
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
      const uint32_t function_id = lower_function(module_, *fn);
      const auto reg = new_reg();
      emit(ir::Op::LoadConst, reg, add_const(Value::function(function_id)));
      if (is_module_) {
        emit(ir::Op::StoreGlobal, add_name(fn->name), reg);
      } else {
        emit(ir::Op::StoreLocal, ensure_local(fn->name), reg);
      }
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
        emit(ir::Op::LoadLocal, reg, local->second);
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
      if (auto* callee_name = dynamic_cast<const ast::NameExpr*>(call->callee.get());
          callee_name != nullptr && callee_name->name == "print") {
        std::vector<uint32_t> arg_regs;
        for (const auto& arg : call->args) {
          arg_regs.push_back(lower_expr(*arg));
        }
        const auto dst = new_reg();
        emit(ir::Op::BuiltinPrint, dst, add_call_args(std::move(arg_regs)));
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
    const auto reg = new_reg();
    emit(ir::Op::LoadConst, reg, add_const(Value::none()));
    return reg;
  }

  static uint32_t lower_function(ir::Module& module, const ast::FunctionDef& fn) {
    FunctionLowerer lowerer(module, fn.name, fn.params);
    lowerer.lower_body(fn.body);
    module.functions.push_back(lowerer.finish());
    return static_cast<uint32_t>(module.functions.size() - 1);
  }

  ir::Module& module_;
  bool is_module_ = false;
  ir::Function fn_;
  std::unordered_map<std::string, uint32_t> locals_;
  std::unordered_map<std::string, uint32_t> name_ids_;
};

} // namespace

LowerResult lower_to_ir(const ast::Module& module_ast) {
  LowerResult result;
  FunctionLowerer lowerer(result.module, "<module>", {}, true);
  lowerer.lower_body(module_ast.body);
  result.module.functions.push_back(lowerer.finish());
  result.module.entry = static_cast<uint32_t>(result.module.functions.size() - 1);
  return result;
}

} // namespace xlang3
