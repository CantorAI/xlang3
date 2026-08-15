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

#include "xlang3/attribute.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
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
  return run_function(module, module.entry, {}, empty_closure, Value::invalid(), nullptr);
}

RuntimeResult Interpreter::run_module(const ir::Module& module, Value globals_module) {
  return run_module(module, std::move(globals_module), nullptr);
}

RuntimeResult Interpreter::run_module(
    const ir::Module& module,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner) {
  static const std::vector<Value> empty_closure;
  return run_function(module, module.entry, {}, empty_closure, std::move(globals_module), std::move(module_owner));
}

RuntimeResult Interpreter::run_function(
    const ir::Module& module,
    uint32_t function_id,
    const std::vector<Value>& args,
    const std::vector<Value>& fn_obj_closure,
    Value globals_module,
    std::shared_ptr<const ir::Module> module_owner) {
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
  std::vector<uint32_t> exception_handlers;
  auto raise_runtime_error = [&](const std::string& message) -> bool {
    if (exception_handlers.empty()) {
      result.errors.push_back(message);
      return false;
    }
    ip = exception_handlers.back();
    exception_handlers.pop_back();
    return true;
  };
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
        if (value_as_module(globals_module) != nullptr) {
          std::string error;
          if (module_get_attr(globals_module, name, regs[in.dst], error)) {
            break;
          }
          if (const auto* builtin = runtime_.find_builtin(name)) {
            regs[in.dst] = *builtin;
          } else {
            if (raise_runtime_error("name '" + name + "' is not defined")) continue;
            return result;
          }
        } else if (auto it = globals_.find(name); it != globals_.end()) {
          regs[in.dst] = it->second;
        } else if (const auto* builtin = runtime_.find_builtin(name)) {
          regs[in.dst] = *builtin;
        } else {
          if (raise_runtime_error("name '" + name + "' is not defined")) continue;
          return result;
        }
        break;
      }
      case ir::Op::StoreGlobal:
        if (in.dst >= fn.names.size() || in.a >= regs.size()) {
          result.errors.push_back("invalid global store");
          return result;
        }
        if (value_as_module(globals_module) != nullptr) {
          std::string error;
          if (!module_set_attr(globals_module, fn.names[in.dst], regs[in.a], error)) {
            result.errors.push_back(error);
            return result;
          }
        } else {
          globals_[fn.names[in.dst]] = regs[in.a];
        }
        break;
      case ir::Op::ImportModule: {
        if (in.a >= fn.names.size()) {
          result.errors.push_back("invalid module name");
          return result;
        }
        std::string error;
        if (!runtime_.import_module(fn.names[in.a], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::ImportFrom: {
        if (in.a >= fn.names.size() || in.b >= fn.names.size()) {
          result.errors.push_back("invalid from import");
          return result;
        }
        std::string error;
        if (!runtime_.import_from(fn.names[in.a], fn.names[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::LoadAttr: {
        if (in.b >= fn.names.size()) {
          result.errors.push_back("invalid attribute name");
          return result;
        }
        std::string error;
        if (!attribute_get(regs[in.a], fn.names[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::StoreAttr: {
        if (in.a >= fn.names.size()) {
          result.errors.push_back("invalid attribute name");
          return result;
        }
        std::string error;
        if (!attribute_set(regs[in.dst], fn.names[in.a], regs[in.b], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::MakeClass: {
        if (in.a >= fn.names.size() || in.b >= fn.class_attrs.size()) {
          result.errors.push_back("invalid class data");
          return result;
        }
        std::vector<std::pair<std::string, Value>> attrs;
        attrs.reserve(fn.class_attrs[in.b].size());
        for (const auto& attr : fn.class_attrs[in.b]) {
          if (attr.second >= regs.size()) {
            result.errors.push_back("invalid class attr register");
            return result;
          }
          attrs.push_back(std::make_pair(attr.first, regs[attr.second]));
        }
        regs[in.dst] = Value::class_object(fn.names[in.a], std::move(attrs));
        break;
      }
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
        regs[in.dst] = Value::function(in.a, std::move(closure), globals_module, module_owner);
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
      case ir::Op::MakeDict: {
        if (in.a >= fn.dict_items.size()) {
          result.errors.push_back("invalid dict item list");
          return result;
        }
        std::vector<std::pair<Value, Value>> items;
        items.reserve(fn.dict_items[in.a].size());
        for (const auto& pair : fn.dict_items[in.a]) {
          if (pair.first >= regs.size() || pair.second >= regs.size()) {
            result.errors.push_back("invalid dict item register");
            return result;
          }
          items.push_back(std::make_pair(regs[pair.first], regs[pair.second]));
        }
        regs[in.dst] = Value::dict(std::move(items));
        break;
      }
      case ir::Op::MakeSet: {
        if (in.a >= fn.set_items.size()) {
          result.errors.push_back("invalid set item list");
          return result;
        }
        std::vector<Value> items;
        items.reserve(fn.set_items[in.a].size());
        for (const auto reg : fn.set_items[in.a]) {
          if (reg >= regs.size()) {
            result.errors.push_back("invalid set item register");
            return result;
          }
          items.push_back(regs[reg]);
        }
        regs[in.dst] = Value::set(std::move(items));
        break;
      }
      case ir::Op::ListAppend: {
        std::string error;
        if (!sequence_list_append(regs[in.dst], regs[in.a], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::GetItem: {
        std::string error;
        if (!sequence_get_item(regs[in.a], regs[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::SetItem: {
        std::string error;
        if (!sequence_set_item(regs[in.dst], regs[in.a], regs[in.b], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::GetIter: {
        std::string error;
        if (!sequence_get_iter(regs[in.a], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::IterNext: {
        std::string error;
        bool done = false;
        if (!sequence_iter_next(regs[in.a], done, regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
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
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::Sub: {
        std::string error;
        if (!value_sub(regs[in.a], regs[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::Mul: {
        std::string error;
        if (!value_mul(regs[in.a], regs[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
          return result;
        }
        break;
      }
      case ir::Op::Div: {
        std::string error;
        if (!value_div(regs[in.a], regs[in.b], regs[in.dst], error)) {
          if (raise_runtime_error(error)) continue;
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
          if (raise_runtime_error(error)) continue;
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
          if (raise_runtime_error("unsupported operand for unary -")) continue;
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
      case ir::Op::SetupExcept:
        exception_handlers.push_back(in.dst);
        break;
      case ir::Op::PopExcept:
        if (exception_handlers.empty()) {
          result.errors.push_back("invalid exception handler pop");
          return result;
        }
        exception_handlers.pop_back();
        break;
      case ir::Op::Raise:
        if (in.a >= regs.size()) {
          result.errors.push_back("invalid raise value");
          return result;
        }
        if (exception_handlers.empty()) {
          result.errors.push_back("uncaught exception: " + value_to_string(regs[in.a]));
          return result;
        }
        ip = exception_handlers.back();
        exception_handlers.pop_back();
        continue;
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
        auto call_callable_value = [&](const Value& function_value, const std::vector<Value>& values, Value& out) -> bool {
          if (auto* native = value_as_native_function(function_value)) {
            std::string error;
            Value native_result;
            if (native->callback == nullptr ||
                !native->callback(runtime_, values.data(), static_cast<uint32_t>(values.size()), native_result, error)) {
              if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
              return false;
            }
            out = std::move(native_result);
            return true;
          }

          auto* fn_obj = value_as_function(function_value);
          if (fn_obj == nullptr) {
            if (raise_runtime_error("object is not callable")) return false;
            return false;
          }
          const ir::Module* call_module = &module;
          auto call_module_owner = module_owner;
          if (fn_obj->module != nullptr) {
            call_module = fn_obj->module.get();
            call_module_owner = fn_obj->module;
          }
          auto call_result =
              run_function(*call_module, fn_obj->function_id, values, fn_obj->closure, fn_obj->globals_module,
                           std::move(call_module_owner));
          if (!call_result.errors.empty()) {
            if (raise_runtime_error(call_result.errors.front())) return false;
            result.errors.insert(result.errors.end(), call_result.errors.begin(), call_result.errors.end());
            return false;
          }
          out = std::move(call_result.value);
          return true;
        };

        if (auto* fn_obj = value_as_function(callee)) {
          (void)fn_obj;
          if (!call_callable_value(callee, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else if (auto* bound = value_as_bound_method(callee)) {
          std::vector<Value> bound_args;
          bound_args.reserve(call_args.size() + 1);
          bound_args.push_back(bound->self);
          bound_args.insert(bound_args.end(), call_args.begin(), call_args.end());
          if (!call_callable_value(bound->function, bound_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else if (auto* klass = value_as_class(callee)) {
          (void)klass;
          Value instance = Value::instance(callee);
          Value init;
          std::string error;
          if (object_get_attr(instance, "__init__", init, error)) {
            auto* bound_init = value_as_bound_method(init);
            if (bound_init == nullptr) {
              if (raise_runtime_error("__init__ is not callable")) continue;
              return result;
            }
            std::vector<Value> init_args;
            init_args.reserve(call_args.size() + 1);
            init_args.push_back(bound_init->self);
            init_args.insert(init_args.end(), call_args.begin(), call_args.end());
            Value ignored;
            if (!call_callable_value(bound_init->function, init_args, ignored)) {
              if (!result.errors.empty()) return result;
              continue;
            }
          } else if (!call_args.empty()) {
            if (raise_runtime_error("class construction expected no arguments")) continue;
            return result;
          }
          regs[in.dst] = std::move(instance);
        } else if (auto* native = value_as_native_function(callee)) {
          std::string error;
          Value native_result;
          if (native->callback == nullptr ||
              !native->callback(runtime_, call_args.data(), static_cast<uint32_t>(call_args.size()), native_result, error)) {
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) continue;
            return result;
          }
          regs[in.dst] = std::move(native_result);
        } else if (callee.tag == ValueTag::Object) {
          if (raise_runtime_error("object is not callable")) continue;
          return result;
        } else if (callee.tag == ValueTag::Invalid) {
          if (raise_runtime_error("invalid callee")) continue;
          return result;
        } else {
          if (raise_runtime_error("object is not callable")) continue;
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
