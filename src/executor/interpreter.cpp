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

#include <cstddef>
#include <new>
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

XLANG3_HOT_INLINE bool value_is_number(const Value& value) {
  return value.tag == ValueTag::Int64 || value.tag == ValueTag::Double;
}

XLANG3_HOT_INLINE double value_to_double_fast(const Value& value) {
  return value.tag == ValueTag::Int64 ? static_cast<double>(value.as.i64) : value.as.f64;
}

XLANG3_HOT_INLINE bool fast_add(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 + rhs.as.i64);
    return true;
  }
  if (value_is_number(lhs) && value_is_number(rhs)) {
    value_set_number(out, value_to_double_fast(lhs) + value_to_double_fast(rhs));
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool fast_sub(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 - rhs.as.i64);
    return true;
  }
  if (value_is_number(lhs) && value_is_number(rhs)) {
    value_set_number(out, value_to_double_fast(lhs) - value_to_double_fast(rhs));
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool fast_mul(const Value& lhs, const Value& rhs, Value& out) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    value_set_int64(out, lhs.as.i64 * rhs.as.i64);
    return true;
  }
  if (value_is_number(lhs) && value_is_number(rhs)) {
    value_set_number(out, value_to_double_fast(lhs) * value_to_double_fast(rhs));
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE bool fast_div(const Value& lhs, const Value& rhs, Value& out, bool& divide_by_zero) {
  divide_by_zero = false;
  if (!value_is_number(lhs) || !value_is_number(rhs)) {
    return false;
  }
  const double divisor = value_to_double_fast(rhs);
  if (divisor == 0.0) {
    divide_by_zero = true;
    return false;
  }
  value_set_number(out, value_to_double_fast(lhs) / divisor);
  return true;
}

XLANG3_HOT_INLINE bool fast_compare(ir::CompareOp op, const Value& lhs, const Value& rhs, Value& out) {
  if (!value_is_number(lhs) || !value_is_number(rhs)) {
    return false;
  }
  const double a = value_to_double_fast(lhs);
  const double b = value_to_double_fast(rhs);
  bool compare_result = false;
  switch (op) {
    case ir::CompareOp::Eq: compare_result = a == b; break;
    case ir::CompareOp::Ne: compare_result = a != b; break;
    case ir::CompareOp::Lt: compare_result = a < b; break;
    case ir::CompareOp::Le: compare_result = a <= b; break;
    case ir::CompareOp::Gt: compare_result = a > b; break;
    case ir::CompareOp::Ge: compare_result = a >= b; break;
  }
  value_set_bool(out, compare_result);
  return true;
}

class SmallValueBuffer {
public:
  SmallValueBuffer(size_t size, const Value& fill) : size_(size) {
    if (size_ <= kInlineCount) {
      data_ = inline_data();
      uses_inline_ = true;
      for (size_t i = 0; i < size_; ++i) {
        new (data_ + i) Value(fill);
      }
      return;
    }
    heap_.assign(size_, fill);
    data_ = heap_.data();
  }

  SmallValueBuffer(const SmallValueBuffer&) = delete;
  SmallValueBuffer& operator=(const SmallValueBuffer&) = delete;

  ~SmallValueBuffer() {
    if (uses_inline_) {
      for (size_t i = 0; i < size_; ++i) {
        data_[i].~Value();
      }
    }
  }

  XLANG3_HOT_INLINE size_t size() const {
    return size_;
  }

  XLANG3_HOT_INLINE Value* data() {
    return data_;
  }

  XLANG3_HOT_INLINE const Value* data() const {
    return data_;
  }

  XLANG3_HOT_INLINE Value& operator[](size_t index) {
    return data_[index];
  }

  XLANG3_HOT_INLINE const Value& operator[](size_t index) const {
    return data_[index];
  }

private:
  static constexpr size_t kInlineCount = 64;

  XLANG3_HOT_INLINE Value* inline_data() {
    return reinterpret_cast<Value*>(inline_storage_);
  }

  size_t size_ = 0;
  Value* data_ = nullptr;
  bool uses_inline_ = false;
  alignas(Value) std::byte inline_storage_[sizeof(Value) * kInlineCount];
  std::vector<Value> heap_;
};

struct CallSiteCache {
  Object* callee_object = nullptr;
  uint8_t kind = 0;
  FunctionObject* function = nullptr;
  NativeFunctionObject* native = nullptr;
};

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
    CallArgsView args,
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

  SmallValueBuffer locals(fn.locals.size(), Value::none());
  SmallValueBuffer cells(fn.cell_slots.size(), Value::invalid());
  SmallValueBuffer regs(fn.register_count, Value::invalid());
  for (size_t i = 0; i < args.size(); ++i) {
    locals[i] = args.get(i);
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
  std::vector<Value> global_value_cache(fn.names.size(), Value::invalid());
  std::vector<uint32_t> global_slot_cache(fn.names.size(), 0);
  std::vector<uint64_t> global_cache_versions(fn.names.size(), 0);
  std::vector<uint8_t> global_cache_kind(fn.names.size(), 0);
  std::vector<CallSiteCache> call_site_cache;
  std::vector<Value> native_call_args;
  uint32_t max_call_arg_count = 0;
  for (const auto& arg_regs : fn.call_args) {
    if (arg_regs.size() > max_call_arg_count) {
      max_call_arg_count = static_cast<uint32_t>(arg_regs.size());
    }
  }
  if (max_call_arg_count != 0) {
    native_call_args.reserve(static_cast<size_t>(max_call_arg_count) + 1);
    call_site_cache.resize(fn.code.size());
  }
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
        auto* globals_module_obj = value_as_module(globals_module);
        const uint64_t globals_version = globals_module_obj != nullptr ? globals_module_obj->version : globals_version_;
        if (global_cache_kind[in.a] != 0) {
          if (globals_module_obj != nullptr && global_cache_kind[in.a] == 1) {
            const auto slot = global_slot_cache[in.a];
            if (slot < globals_module_obj->slots.size()) {
              regs[in.dst] = globals_module_obj->slots[slot];
              break;
            }
          } else if (global_cache_kind[in.a] == 2 && global_cache_versions[in.a] == globals_version) {
            regs[in.dst] = global_value_cache[in.a];
            break;
          }
        }
        const auto& name = fn.names[in.a];
        if (globals_module_obj != nullptr) {
          std::string error;
          uint32_t slot = 0;
          if (module_find_attr_slot(globals_module, name, slot, error)) {
            regs[in.dst] = globals_module_obj->slots[slot];
            global_slot_cache[in.a] = slot;
            global_cache_kind[in.a] = 1;
            break;
          }
          if (const auto* builtin = runtime_.find_builtin(name)) {
            regs[in.dst] = *builtin;
            global_value_cache[in.a] = regs[in.dst];
            global_cache_versions[in.a] = globals_module_obj->version;
            global_cache_kind[in.a] = 2;
          } else {
            if (raise_runtime_error("name '" + name + "' is not defined")) continue;
            return result;
          }
        } else if (auto it = globals_.find(name); it != globals_.end()) {
          regs[in.dst] = it->second;
          global_value_cache[in.a] = regs[in.dst];
          global_cache_versions[in.a] = globals_version_;
          global_cache_kind[in.a] = 2;
        } else if (const auto* builtin = runtime_.find_builtin(name)) {
          regs[in.dst] = *builtin;
          global_value_cache[in.a] = regs[in.dst];
          global_cache_versions[in.a] = globals_version_;
          global_cache_kind[in.a] = 2;
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
          ++globals_version_;
        }
        if (auto* globals_module_obj = value_as_module(globals_module)) {
          std::string error;
          uint32_t slot = 0;
          if (module_find_attr_slot(globals_module, fn.names[in.dst], slot, error)) {
            global_slot_cache[in.dst] = slot;
            global_cache_kind[in.dst] = 1;
          }
          global_cache_versions[in.dst] = globals_module_obj->version;
        } else {
          global_value_cache[in.dst] = regs[in.a];
          global_cache_versions[in.dst] = globals_version_;
          global_cache_kind[in.dst] = 2;
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
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_add(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_add(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Sub: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_sub(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_sub(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Mul: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        if (!fast_mul(lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_mul(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Div: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        bool divide_by_zero = false;
        if (!fast_div(lhs, rhs, regs[in.dst], divide_by_zero)) {
          if (divide_by_zero) {
            if (raise_runtime_error("division by zero")) continue;
            return result;
          }
          std::string error;
          if (!value_div(lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::BoolAnd:
        value_set_bool(regs[in.dst], value_truthy(regs[in.a]) && value_truthy(regs[in.b]));
        break;
      case ir::Op::BoolOr:
        value_set_bool(regs[in.dst], value_truthy(regs[in.a]) || value_truthy(regs[in.b]));
        break;
      case ir::Op::Compare: {
        const auto& lhs = regs[in.a];
        const auto& rhs = regs[in.b];
        const auto op = static_cast<ir::CompareOp>(in.c);
        if (!fast_compare(op, lhs, rhs, regs[in.dst])) {
          std::string error;
          if (!value_compare(compare_name(op), lhs, rhs, regs[in.dst], error)) {
            if (raise_runtime_error(error)) continue;
            return result;
          }
        }
        break;
      }
      case ir::Op::Not:
        value_set_bool(regs[in.dst], !value_truthy(regs[in.a]));
        break;
      case ir::Op::Neg:
        if (regs[in.a].tag == ValueTag::Int64) {
          value_set_int64(regs[in.dst], -regs[in.a].as.i64);
        } else if (regs[in.a].tag == ValueTag::Double) {
          value_set_number(regs[in.dst], -regs[in.a].as.f64);
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
      case ir::Op::CallMethod: {
        if (in.b >= fn.names.size() || in.c >= fn.call_args.size()) {
          result.errors.push_back("invalid method call");
          return result;
        }
        const auto& name = fn.names[in.b];
        const auto& call_arg_regs = fn.call_args[in.c];
        if (name == "append" && call_arg_regs.size() == 1) {
          if (auto* list = value_as_list(regs[in.a])) {
            list->items.push_back(regs[call_arg_regs[0]]);
            value_set_none(regs[in.dst]);
            break;
          }
        }

        Value method;
        std::string attr_error;
        if (!attribute_get(regs[in.a], name, method, attr_error)) {
          if (raise_runtime_error(attr_error)) continue;
          return result;
        }

        CallArgsView call_args;
        call_args.registers = regs.data();
        call_args.register_args = &call_arg_regs;
        auto materialize_native_args = [&](CallArgsView values) -> const Value* {
          native_call_args.clear();
          native_call_args.reserve(values.size());
          for (size_t i = 0; i < values.size(); ++i) {
            native_call_args.push_back(values.get(i));
          }
          return native_call_args.data();
        };
        auto call_user_function = [&](FunctionObject* fn_obj, CallArgsView values, Value& out) -> bool {
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
        auto call_native_function = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
          std::string error;
          Value native_result;
          const Value* native_args = materialize_native_args(values);
          if (native->callback == nullptr ||
              !native->callback(runtime_, native_args, static_cast<uint32_t>(values.size()), native_result, error)) {
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
            return false;
          }
          out = std::move(native_result);
          return true;
        };

        if (auto* bound = value_as_bound_method(method)) {
          CallArgsView bound_args = call_args;
          bound_args.leading = &bound->self;
          bound_args.leading_count = 1;
          if (auto* native = value_as_native_function(bound->function)) {
            if (!call_native_function(native, bound_args, regs[in.dst])) {
              if (!result.errors.empty()) return result;
              continue;
            }
          } else if (auto* fn_obj = value_as_function(bound->function)) {
            if (!call_user_function(fn_obj, bound_args, regs[in.dst])) {
              if (!result.errors.empty()) return result;
              continue;
            }
          } else {
            if (raise_runtime_error("object is not callable")) continue;
            return result;
          }
        } else if (auto* native = value_as_native_function(method)) {
          if (!call_native_function(native, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else if (auto* fn_obj = value_as_function(method)) {
          if (!call_user_function(fn_obj, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else {
          if (raise_runtime_error("object is not callable")) continue;
          return result;
        }
        break;
      }
      case ir::Op::Call: {
        if (in.b >= fn.call_args.size()) {
          result.errors.push_back("invalid call arg list");
          return result;
        }
        const auto& call_arg_regs = fn.call_args[in.b];
        CallArgsView call_args;
        call_args.registers = regs.data();
        call_args.register_args = &call_arg_regs;
        if (in.a < regs.size() && regs[in.a].tag == ValueTag::Invalid) {
          result.errors.push_back("invalid callee");
          return result;
        }
        const auto& callee = regs[in.a];
        auto materialize_native_args = [&](CallArgsView values) -> const Value* {
          native_call_args.clear();
          native_call_args.reserve(values.size());
          for (size_t i = 0; i < values.size(); ++i) {
            native_call_args.push_back(values.get(i));
          }
          return native_call_args.data();
        };
        auto call_user_function = [&](FunctionObject* fn_obj, CallArgsView values, Value& out) -> bool {
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
        auto call_native_function = [&](NativeFunctionObject* native, CallArgsView values, Value& out) -> bool {
          std::string error;
          Value native_result;
          const Value* native_args = materialize_native_args(values);
          if (native->callback == nullptr ||
              !native->callback(runtime_, native_args, static_cast<uint32_t>(values.size()), native_result, error)) {
            if (raise_runtime_error(error.empty() ? "native function failed" : error)) return false;
            return false;
          }
          out = std::move(native_result);
          return true;
        };
        if (!call_site_cache.empty() && callee.tag == ValueTag::Object && callee.as.obj != nullptr) {
          auto& cache = call_site_cache[ip];
          if (cache.callee_object == callee.as.obj) {
            if (cache.kind == 1) {
              if (!call_user_function(cache.function, call_args, regs[in.dst])) {
                if (!result.errors.empty()) return result;
                continue;
              }
              break;
            }
            if (cache.kind == 2) {
              if (!call_native_function(cache.native, call_args, regs[in.dst])) {
                if (!result.errors.empty()) return result;
                continue;
              }
              break;
            }
          }
        }
        auto call_callable_value = [&](const Value& function_value, CallArgsView values, Value& out) -> bool {
          if (auto* native = value_as_native_function(function_value)) {
            return call_native_function(native, values, out);
          }

          auto* fn_obj = value_as_function(function_value);
          if (fn_obj == nullptr) {
            if (raise_runtime_error("object is not callable")) return false;
            return false;
          }
          return call_user_function(fn_obj, values, out);
        };

        if (auto* fn_obj = value_as_function(callee)) {
          if (!call_site_cache.empty() && callee.tag == ValueTag::Object) {
            auto& cache = call_site_cache[ip];
            cache.callee_object = callee.as.obj;
            cache.kind = 1;
            cache.function = fn_obj;
            cache.native = nullptr;
          }
          if (!call_user_function(fn_obj, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
        } else if (auto* bound = value_as_bound_method(callee)) {
          CallArgsView bound_args = call_args;
          bound_args.leading = &bound->self;
          bound_args.leading_count = 1;
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
            CallArgsView init_args = call_args;
            init_args.leading = &bound_init->self;
            init_args.leading_count = 1;
            Value ignored;
            if (!call_callable_value(bound_init->function, init_args, ignored)) {
              if (!result.errors.empty()) return result;
              continue;
            }
          } else if (call_args.size() != 0) {
            if (raise_runtime_error("class construction expected no arguments")) continue;
            return result;
          }
          regs[in.dst] = std::move(instance);
        } else if (auto* native = value_as_native_function(callee)) {
          if (!call_site_cache.empty() && callee.tag == ValueTag::Object) {
            auto& cache = call_site_cache[ip];
            cache.callee_object = callee.as.obj;
            cache.kind = 2;
            cache.function = nullptr;
            cache.native = native;
          }
          if (!call_native_function(native, call_args, regs[in.dst])) {
            if (!result.errors.empty()) return result;
            continue;
          }
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

  value_set_none(result.value);
  return result;
}

} // namespace xlang3
