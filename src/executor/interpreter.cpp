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
#include "xlang3/interpreter.h"

#include "xlang3/sequence.h"

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
  static const std::vector<Value> empty_closure;
  return run_function(module, module.entry, {}, empty_closure);
}

RuntimeResult Interpreter::run_function(
    const ir::Module& module,
    uint32_t function_id,
    const std::vector<Value>& args,
    const std::vector<Value>& fn_obj_closure) {
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
  std::vector<Value> cells(fn.cell_slots.size(), Value::invalid());
  std::vector<Value> regs(fn.register_count, Value::invalid());
  for (size_t i = 0; i < args.size(); ++i) {
    locals[i] = args[i];
  }
  for (size_t i = 0; i < fn.cell_slots.size(); ++i) {
    if (fn.cell_slots[i] >= locals.size()) {
      result.errors.push_back("invalid cell local slot");
      return result;
    }
    cells[i] = Value::cell(locals[fn.cell_slots[i]]);
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
      case ir::Op::LoadCell: {
        if (in.a >= cells.size()) {
          result.errors.push_back("invalid cell slot");
          return result;
        }
        auto* cell = value_as_cell(cells[in.a]);
        if (cell == nullptr) {
          result.errors.push_back("invalid cell object");
          return result;
        }
        regs[in.dst] = cell->value;
        break;
      }
      case ir::Op::StoreCell: {
        if (in.dst >= cells.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid cell store");
          return result;
        }
        auto* cell = value_as_cell(cells[in.dst]);
        if (cell == nullptr) {
          result.errors.push_back("invalid cell object");
          return result;
        }
        cell->value = regs[in.a];
        locals[fn.cell_slots[in.dst]] = regs[in.a];
        break;
      }
      case ir::Op::LoadCellObject:
        if (in.a >= cells.size()) {
          result.errors.push_back("invalid cell object slot");
          return result;
        }
        regs[in.dst] = cells[in.a];
        break;
      case ir::Op::LoadFree: {
        if (in.a >= fn_obj_closure.size()) {
          result.errors.push_back("invalid free slot");
          return result;
        }
        auto* cell = value_as_cell(fn_obj_closure[in.a]);
        if (cell == nullptr) {
          result.errors.push_back("invalid free cell");
          return result;
        }
        regs[in.dst] = cell->value;
        break;
      }
      case ir::Op::StoreFree: {
        if (in.dst >= fn_obj_closure.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid free store");
          return result;
        }
        auto* cell = value_as_cell(fn_obj_closure[in.dst]);
        if (cell == nullptr) {
          result.errors.push_back("invalid free cell");
          return result;
        }
        cell->value = regs[in.a];
        break;
      }
      case ir::Op::LoadFreeObject:
        if (in.a >= fn_obj_closure.size()) {
          result.errors.push_back("invalid free object slot");
          return result;
        }
        regs[in.dst] = fn_obj_closure[in.a];
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
      case ir::Op::MakeFunction: {
        if (in.b >= fn.function_closures.size()) {
          result.errors.push_back("invalid function closure list");
          return result;
        }
        std::vector<Value> closure;
        closure.reserve(fn.function_closures[in.b].size());
        for (const auto reg : fn.function_closures[in.b]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid closure register");
            return result;
          }
          closure.push_back(regs[reg]);
        }
        regs[in.dst] = Value::function(in.a, std::move(closure));
        break;
      }
      case ir::Op::MakeTuple: {
        if (in.a >= fn.tuple_items.size()) {
          result.errors.push_back("invalid tuple item list");
          return result;
        }
        std::vector<Value> items;
        items.reserve(fn.tuple_items[in.a].size());
        for (const auto reg : fn.tuple_items[in.a]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid tuple item register");
            return result;
          }
          items.push_back(regs[reg]);
        }
        regs[in.dst] = Value::tuple(std::move(items));
        break;
      }
      case ir::Op::MakeList: {
        if (in.a >= fn.list_items.size()) {
          result.errors.push_back("invalid list item list");
          return result;
        }
        std::vector<Value> items;
        items.reserve(fn.list_items[in.a].size());
        for (const auto reg : fn.list_items[in.a]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid list item register");
            return result;
          }
          items.push_back(regs[reg]);
        }
        regs[in.dst] = Value::list(std::move(items));
        break;
      }
      case ir::Op::ListAppend: {
        std::string error;
        if (!sequence_list_append(regs[in.dst], regs[in.a], error)) {
          result.errors.push_back(error);
          return result;
        }
        break;
      }
      case ir::Op::GetItem: {
        std::string error;
        if (!sequence_get_item(regs[in.a], regs[in.b], regs[in.dst], error)) {
          result.errors.push_back(error);
          return result;
        }
        break;
      }
      case ir::Op::GetIter: {
        std::string error;
        if (!sequence_get_iter(regs[in.a], regs[in.dst], error)) {
          result.errors.push_back(error);
          return result;
        }
        break;
      }
      case ir::Op::IterNext: {
        std::string error;
        bool done = false;
        if (!sequence_iter_next(regs[in.a], done, regs[in.dst], error)) {
          result.errors.push_back(error);
          return result;
        }
        if (done) {
          ip = in.b;
          continue;
        }
        break;
      }
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
          auto call_result = run_function(module, fn_obj->function_id, call_args, fn_obj->closure);
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
