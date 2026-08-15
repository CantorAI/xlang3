#include "xlang3/interpreter.h"

#include <sstream>

namespace xlang3 {

namespace {

std::string compare_name(ir::CompareOp op) {
  switch (op) {
    case ir::CompareOp::Eq: return "==";
    case ir::CompareOp::Ne: return "!=";
    case ir::CompareOp::Lt: return "<";
    case ir::CompareOp::Le: return "<=";
    case ir::CompareOp::Gt: return ">";
    case ir::CompareOp::Ge: return ">=";
  }
  return "?";
}

} // namespace

Interpreter::Interpreter(Runtime& runtime) : runtime_(runtime) {}

RuntimeResult Interpreter::run(const ir::Module& module) {
  return run_function(module, module.entry, {});
}

RuntimeResult Interpreter::run_function(const ir::Module& module, uint32_t function_id, const std::vector<Value>& args) {
  RuntimeResult result;
  if (function_id >= module.functions.size()) {
    result.errors.push_back("invalid function id");
    return result;
  }
  const auto& fn = module.functions[function_id];
  if (args.size() != fn.params.size()) {
    result.errors.push_back("function '" + fn.name + "' expected " + std::to_string(fn.params.size()) +
                            " arguments, got " + std::to_string(args.size()));
    return result;
  }

  std::vector<Value> locals(fn.locals.size(), Value::none());
  std::vector<Value> regs(fn.register_count, Value::invalid());
  for (size_t i = 0; i < args.size(); ++i) {
    locals[i] = args[i];
  }

  size_t ip = 0;
  while (ip < fn.code.size()) {
    const auto& in = fn.code[ip];
    switch (in.op) {
      case ir::Op::LoadConst:
        if (in.a >= fn.constants.size()) {
          result.errors.push_back("invalid constant index");
          return result;
        }
        regs[in.dst] = fn.constants[in.a];
        break;
      case ir::Op::LoadLocal:
        if (in.a >= locals.size()) {
          result.errors.push_back("invalid local slot");
          return result;
        }
        regs[in.dst] = locals[in.a];
        break;
      case ir::Op::StoreLocal:
        if (in.dst >= locals.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid local store");
          return result;
        }
        locals[in.dst] = regs[in.a];
        break;
      case ir::Op::LoadGlobal: {
        if (in.a >= fn.names.size()) {
          result.errors.push_back("invalid global name");
          return result;
        }
        const auto& name = fn.names[in.a];
        auto it = globals_.find(name);
        if (it != globals_.end()) {
          regs[in.dst] = it->second;
        } else if (const auto* builtin = runtime_.find_builtin(name)) {
          regs[in.dst] = *builtin;
        } else {
          result.errors.push_back("name '" + name + "' is not defined");
          return result;
        }
        break;
      }
      case ir::Op::StoreGlobal:
        if (in.dst >= fn.names.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid global store");
          return result;
        }
        globals_[fn.names[in.dst]] = regs[in.a];
        break;
      case ir::Op::Add: {
        std::string error;
        if (!value_add(regs[in.a], regs[in.b], regs[in.dst], error)) {
          result.errors.push_back(error);
          return result;
        }
        break;
      }
      case ir::Op::Sub: {
        std::string error;
        if (!value_sub(regs[in.a], regs[in.b], regs[in.dst], error)) {
          result.errors.push_back(error);
          return result;
        }
        break;
      }
      case ir::Op::Mul: {
        std::string error;
        if (!value_mul(regs[in.a], regs[in.b], regs[in.dst], error)) {
          result.errors.push_back(error);
          return result;
        }
        break;
      }
      case ir::Op::Div: {
        std::string error;
        if (!value_div(regs[in.a], regs[in.b], regs[in.dst], error)) {
          result.errors.push_back(error);
          return result;
        }
        break;
      }
      case ir::Op::BoolAnd:
        regs[in.dst] = Value::boolean(value_truthy(regs[in.a]) && value_truthy(regs[in.b]));
        break;
      case ir::Op::BoolOr:
        regs[in.dst] = Value::boolean(value_truthy(regs[in.a]) || value_truthy(regs[in.b]));
        break;
      case ir::Op::Compare: {
        std::string error;
        if (!value_compare(compare_name(static_cast<ir::CompareOp>(in.c)), regs[in.a], regs[in.b], regs[in.dst], error)) {
          result.errors.push_back(error);
          return result;
        }
        break;
      }
      case ir::Op::Not:
        regs[in.dst] = Value::boolean(!value_truthy(regs[in.a]));
        break;
      case ir::Op::Neg:
        if (regs[in.a].tag == ValueTag::Int64) {
          regs[in.dst] = Value::int64(-regs[in.a].as.i64);
        } else if (regs[in.a].tag == ValueTag::Double) {
          regs[in.dst] = Value::number(-regs[in.a].as.f64);
        } else {
          result.errors.push_back("unsupported operand for unary -");
          return result;
        }
        break;
      case ir::Op::Jump:
        ip = in.dst;
        continue;
      case ir::Op::JumpIfFalse:
        if (!value_truthy(regs[in.a])) {
          ip = in.dst;
          continue;
        }
        break;
      case ir::Op::Call: {
        if (in.b >= fn.call_args.size()) {
          result.errors.push_back("invalid call arg list");
          return result;
        }
        std::vector<Value> call_args;
        for (const auto reg : fn.call_args[in.b]) {
          call_args.push_back(regs[reg]);
        }
        if (in.a < regs.size() && regs[in.a].tag == ValueTag::Invalid) {
          result.errors.push_back("invalid callee");
          return result;
        }
        const auto& callee = regs[in.a];
        if (auto* fn_obj = value_as_function(callee)) {
          auto call_result = run_function(module, fn_obj->function_id, call_args);
          if (!call_result.errors.empty()) {
            return call_result;
          }
          regs[in.dst] = call_result.value;
        } else if (auto* native = value_as_native_function(callee)) {
          std::string error;
          Value native_result;
          if (native->callback == nullptr ||
              !native->callback(runtime_, call_args.data(), static_cast<uint32_t>(call_args.size()), native_result, error)) {
            result.errors.push_back(error.empty() ? "native function failed" : error);
            return result;
          }
          regs[in.dst] = std::move(native_result);
        } else if (callee.tag == ValueTag::Object) {
          result.errors.push_back("object is not callable");
          return result;
        } else if (callee.tag == ValueTag::Invalid) {
          result.errors.push_back("invalid callee");
          return result;
        } else {
          result.errors.push_back("object is not callable");
          return result;
        }
        break;
      }
      case ir::Op::Pop:
        break;
      case ir::Op::Return:
        result.value = regs[in.a];
        return result;
    }
    ++ip;
  }

  result.value = Value::none();
  return result;
}

} // namespace xlang3
