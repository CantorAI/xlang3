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
#pragma once

#include "xlang_frame.h"
#include "xlang_vm_arithmetic.h"
#include "xlang_vm_inline_call.h"
#include "xlang_vm_names.h"
#include "xlang_vm_property_inline.h"
#include "runtime_lock.h"

#include "xlang3/compiler.h"
#include "xlang3/builtins.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

/*
Inline support used by the XlangVM loop and call op helpers.

Keep these helpers header-only: they are hot-path call analysis and frame
support routines. The loop cpp includes this file before entering namespace
xlang3, so the functions live in the normal xlang3 namespace and can be passed
as direct inline call targets to op handlers.
*/

namespace xlang3 {

struct GeneratorVMState {
  std::vector<VMFrame> frames;
  size_t frame_count = 0;
  uint32_t send_target = UINT32_MAX;
};

struct RuntimeDebugPauseState {
  std::vector<VMFrame> frames;
  size_t frame_count = 0;
  RuntimePauseReason reason = RuntimePauseReason::None;
};

XLANG3_HOT_INLINE bool analyze_const_method(const ir::Module& current_module, const FunctionObject& fn_obj, Value& out) {
  const ir::Module* fn_module = nullptr;
  if (!module_for_function(current_module, fn_obj, fn_module)) {
    return false;
  }
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.params.size() != 1 || function.free_vars.size() != 0 || function.cell_slots.size() != 0 ||
      function.code.size() < 2) {
    return false;
  }
  const auto& load_const = function.code[0];
  const auto& ret = function.code[1];
  if (load_const.op != ir::Op::LoadConst || load_const.a >= function.constants.size() ||
      ret.op != ir::Op::Return || ret.a != load_const.dst) {
    return false;
  }
  value_assign_fast(out, function.constants[load_const.a]);
  return true;
}

XLANG3_HOT_INLINE bool execute_inline_small_self_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    const Value& self,
    Value& out,
    bool& supported,
    std::string& error) {
  supported = false;
  const ir::Module* fn_module = nullptr;
  if (!module_for_function(current_module, fn_obj, fn_module)) {
    return false;
  }
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.params.size() != 1 || function.free_vars.size() != 0 || function.cell_slots.size() != 0 ||
      function.register_count > 16 || function.code.size() > 16) {
    return false;
  }

  supported = true;
  std::array<Value, 16> temp_regs;
  for (auto& value : temp_regs) {
    value_set_invalid(value);
  }

  for (size_t local_ip = 0; local_ip < function.code.size(); ++local_ip) {
    const auto& op = function.code[local_ip];
    if (op.dst >= temp_regs.size()) {
      supported = false;
      return false;
    }
    switch (op.op) {
      case ir::Op::LoadLocal:
        if (op.a != 0) {
          supported = false;
          return false;
        }
        value_assign_fast(temp_regs[op.dst], self);
        break;
      case ir::Op::LoadConst:
        if (op.a >= function.constants.size()) {
          supported = false;
          return false;
        }
        value_assign_fast(temp_regs[op.dst], function.constants[op.a]);
        break;
      case ir::Op::LoadInstanceSlot: {
        if (op.a >= temp_regs.size()) {
          supported = false;
          return false;
        }
        auto* instance = value_as_instance(temp_regs[op.a]);
        if (instance == nullptr || op.b >= instance_slot_count(instance)) {
          error = "invalid instance slot load";
          return false;
        }
        const auto& slot = instance_slot_at(instance, op.b);
        if (slot.tag == ValueTag::Invalid) {
          error = "object has no attribute";
          return false;
        }
        value_assign_fast(temp_regs[op.dst], slot);
        break;
      }
      case ir::Op::LoadAttr: {
        if (op.a >= temp_regs.size() || op.b >= function.names.size()) {
          supported = false;
          return false;
        }
        Value descriptor;
        std::string descriptor_error;
        if (object_get_class_attr_for_instance(temp_regs[op.a], function.names[op.b], descriptor, descriptor_error)) {
          if (auto* property = value_as_property(descriptor)) {
            if (auto* getter = value_as_function(property->fget)) {
              auto* instance = value_as_instance(temp_regs[op.a]);
              InlinePropertyAccess property_spec;
              if (instance != nullptr && analyze_property_getter(current_module, *getter, property_spec)) {
                if (!execute_inline_property_getter_spec(*instance, property_spec, temp_regs[op.dst], error)) {
                  return false;
                }
                break;
              }
            }
          }
        }
        if (!object_get_attr(temp_regs[op.a], function.names[op.b], temp_regs[op.dst], error)) {
          return false;
        }
        break;
      }
      case ir::Op::CallMethod: {
        if (op.a >= temp_regs.size() || op.b >= function.names.size() || op.c >= function.call_args.size() ||
            !function.call_args[op.c].empty()) {
          supported = false;
          return false;
        }
        auto* instance = value_as_instance(temp_regs[op.a]);
        if (instance == nullptr) {
          supported = false;
          return false;
        }
        Value method_value;
        std::string method_error;
        if (!object_get_class_attr_for_instance(temp_regs[op.a], function.names[op.b], method_value, method_error)) {
          supported = false;
          return false;
        }
        if (auto* method_fn = value_as_function(method_value)) {
          Value const_value;
          if (analyze_const_method(current_module, *method_fn, const_value)) {
            value_assign_fast(temp_regs[op.dst], const_value);
            break;
          }
          SelfBinaryMethodSpec method_spec;
          if (analyze_self_binary_method(current_module, *method_fn, method_spec)) {
            if (!execute_self_binary_method(*instance, method_spec, temp_regs[op.dst], error)) {
              return false;
            }
            break;
          }
        }
        supported = false;
        return false;
      }
      case ir::Op::Add:
      case ir::Op::Sub:
      case ir::Op::Mul:
      case ir::Op::Div:
      case ir::Op::Mod:
        if (op.a >= temp_regs.size() || op.b >= temp_regs.size()) {
          supported = false;
          return false;
        }
        if (!xlang_vm_execute_binary_op(op.op, temp_regs[op.a], temp_regs[op.b], temp_regs[op.dst], error)) {
          return false;
        }
        break;
      case ir::Op::Return:
        if (op.a >= temp_regs.size()) {
          supported = false;
          return false;
        }
        value_assign_fast(out, temp_regs[op.a]);
        return true;
      default:
        supported = false;
        return false;
    }
  }

  supported = false;
  return false;
}

XLANG3_HOT_INLINE bool analyze_self_slot_const_sum_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    const Value& self,
    uint32_t& slot,
    Value& constant) {
  const ir::Module* fn_module = nullptr;
  if (!module_for_function(current_module, fn_obj, fn_module)) {
    return false;
  }
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.params.size() != 1 || function.free_vars.size() != 0 || function.cell_slots.size() != 0 ||
      function.register_count > 16 || function.code.size() > 16) {
    return false;
  }

  enum class Kind : uint8_t { Unknown, Self, Slot, Const, Sum };
  struct AbstractValue {
    Kind kind;
    uint32_t slot;
    Value constant;
  };

  std::array<AbstractValue, 16> values;
  for (auto& value : values) {
    value.kind = Kind::Unknown;
    value.slot = 0;
    value_set_invalid(value.constant);
  }
  for (const auto& instr : function.code) {
    if (instr.dst >= values.size()) {
      return false;
    }
    switch (instr.op) {
      case ir::Op::LoadLocal:
        if (instr.a != 0) {
          return false;
        }
        values[instr.dst].kind = Kind::Self;
        break;
      case ir::Op::LoadInstanceSlot:
        if (instr.a >= values.size() || values[instr.a].kind != Kind::Self) {
          return false;
        }
        values[instr.dst].kind = Kind::Slot;
        values[instr.dst].slot = instr.b;
        break;
      case ir::Op::LoadAttr: {
        if (instr.a >= values.size() || instr.b >= function.names.size() || values[instr.a].kind != Kind::Self) {
          return false;
        }
        Value descriptor;
        std::string descriptor_error;
        if (!object_get_class_attr_for_instance(self, function.names[instr.b], descriptor, descriptor_error)) {
          return false;
        }
        auto* property = value_as_property(descriptor);
        auto* getter = property == nullptr ? nullptr : value_as_function(property->fget);
        InlinePropertyAccess property_spec;
        if (getter == nullptr || !analyze_property_getter(current_module, *getter, property_spec) ||
            property_spec.has_const) {
          return false;
        }
        values[instr.dst].kind = Kind::Slot;
        values[instr.dst].slot = property_spec.slot;
        break;
      }
      case ir::Op::CallMethod: {
        if (instr.a >= values.size() || instr.b >= function.names.size() || instr.c >= function.call_args.size() ||
            values[instr.a].kind != Kind::Self || !function.call_args[instr.c].empty()) {
          return false;
        }
        Value method_value;
        std::string method_error;
        if (!object_get_class_attr_for_instance(self, function.names[instr.b], method_value, method_error)) {
          return false;
        }
        auto* method = value_as_function(method_value);
        Value const_value;
        if (method == nullptr || !analyze_const_method(current_module, *method, const_value)) {
          return false;
        }
        values[instr.dst].kind = Kind::Const;
        value_assign_fast(values[instr.dst].constant, const_value);
        break;
      }
      case ir::Op::Add: {
        if (instr.a >= values.size() || instr.b >= values.size()) {
          return false;
        }
        const auto& lhs = values[instr.a];
        const auto& rhs = values[instr.b];
        if ((lhs.kind == Kind::Slot || lhs.kind == Kind::Sum) && rhs.kind == Kind::Const) {
          values[instr.dst].kind = Kind::Sum;
          values[instr.dst].slot = lhs.slot;
          if (lhs.kind == Kind::Sum) {
            std::string error;
            if (!xlang_vm_execute_binary_op(ir::Op::Add, lhs.constant, rhs.constant, values[instr.dst].constant, error)) {
              return false;
            }
          } else {
            value_assign_fast(values[instr.dst].constant, rhs.constant);
          }
          break;
        }
        if (lhs.kind == Kind::Const && (rhs.kind == Kind::Slot || rhs.kind == Kind::Sum)) {
          values[instr.dst].kind = Kind::Sum;
          values[instr.dst].slot = rhs.slot;
          if (rhs.kind == Kind::Sum) {
            std::string error;
            if (!xlang_vm_execute_binary_op(ir::Op::Add, lhs.constant, rhs.constant, values[instr.dst].constant, error)) {
              return false;
            }
          } else {
            value_assign_fast(values[instr.dst].constant, lhs.constant);
          }
          break;
        }
        return false;
      }
      case ir::Op::Return:
        if (instr.a >= values.size()) {
          return false;
        }
        if (values[instr.a].kind == Kind::Slot) {
          return false;
        }
        if (values[instr.a].kind == Kind::Sum) {
          slot = values[instr.a].slot;
          value_assign_fast(constant, values[instr.a].constant);
          return true;
        }
        return false;
      default:
        return false;
    }
  }
  return false;
}

XLANG3_HOT_INLINE bool analyze_self_slot_method(
    const ir::Module& current_module,
    const FunctionObject& fn_obj,
    uint32_t& slot) {
  const ir::Module* fn_module = nullptr;
  if (!module_for_function(current_module, fn_obj, fn_module)) {
    return false;
  }
  const auto& function = fn_module->functions[fn_obj.function_id];
  if (function.params.size() != 1 || function.free_vars.size() != 0 || function.cell_slots.size() != 0 ||
      function.code.size() < 3) {
    return false;
  }
  const auto& load_self = function.code[0];
  const auto& load_slot = function.code[1];
  const auto& ret = function.code[2];
  if (load_self.op != ir::Op::LoadLocal || load_self.a != 0 ||
      load_slot.op != ir::Op::LoadInstanceSlot || load_slot.a != load_self.dst ||
      ret.op != ir::Op::Return || ret.a != load_slot.dst) {
    return false;
  }
  slot = load_slot.b;
  return true;
}

XLANG3_HOT_INLINE bool execute_self_slot_method(
    const InstanceObject& instance,
    uint32_t slot,
    Value& out,
    std::string& error) {
  if (slot >= instance_slot_count(&instance)) {
    error = "invalid instance slot load";
    return false;
  }
  const auto& slot_value = instance_slot_at(&instance, slot);
  if (slot_value.tag == ValueTag::Invalid) {
    error = "object has no attribute";
    return false;
  }
  value_assign_fast(out, slot_value);
  return true;
}

XLANG3_HOT_INLINE bool execute_self_slot_const_sum_method(
    const InstanceObject& instance,
    uint32_t slot,
    const Value& constant,
    Value& out,
    std::string& error) {
  if (slot >= instance_slot_count(&instance)) {
    error = "invalid instance slot load";
    return false;
  }
  const auto& slot_value = instance_slot_at(&instance, slot);
  if (slot_value.tag == ValueTag::Invalid) {
    error = "object has no attribute";
    return false;
  }
  return xlang_vm_execute_binary_op(ir::Op::Add, slot_value, constant, out, error);
}

XLANG3_HOT_INLINE void destroy_generator_vm_state(void* state) {
  delete static_cast<GeneratorVMState*>(state);
}


enum class XlangVMBuiltinConstructor : uint8_t {
  Unknown,
  Type,
  Object,
  Str,
  Bool,
  Int,
  Float,
  Range,
  List,
  Tuple,
  Set,
  Dict,
  Bytes,
  ByteArray,
  MemoryView,
  Property,
  ClassMethod,
  StaticMethod,
  Super,
};

struct XlangVMBuiltinConstructorSpec {
  const char* name;
  XlangVMBuiltinConstructor kind;
};

XLANG3_HOT_INLINE XlangVMBuiltinConstructor xlang_vm_find_builtin_constructor(const std::string& name) {
  static constexpr XlangVMBuiltinConstructorSpec specs[] = {
      {XlangVMNames::builtin_type, XlangVMBuiltinConstructor::Type},
      {XlangVMNames::object_type, XlangVMBuiltinConstructor::Object},
      {XlangVMNames::builtin_str, XlangVMBuiltinConstructor::Str},
      {XlangVMNames::builtin_bool, XlangVMBuiltinConstructor::Bool},
      {XlangVMNames::builtin_int, XlangVMBuiltinConstructor::Int},
      {XlangVMNames::builtin_float, XlangVMBuiltinConstructor::Float},
      {XlangVMNames::builtin_range, XlangVMBuiltinConstructor::Range},
      {XlangVMNames::builtin_list, XlangVMBuiltinConstructor::List},
      {XlangVMNames::builtin_tuple, XlangVMBuiltinConstructor::Tuple},
      {XlangVMNames::builtin_set, XlangVMBuiltinConstructor::Set},
      {XlangVMNames::builtin_dict, XlangVMBuiltinConstructor::Dict},
      {XlangVMNames::builtin_bytes, XlangVMBuiltinConstructor::Bytes},
      {XlangVMNames::builtin_bytearray, XlangVMBuiltinConstructor::ByteArray},
      {XlangVMNames::builtin_memoryview, XlangVMBuiltinConstructor::MemoryView},
      {XlangVMNames::builtin_property, XlangVMBuiltinConstructor::Property},
      {XlangVMNames::builtin_classmethod, XlangVMBuiltinConstructor::ClassMethod},
      {XlangVMNames::builtin_staticmethod, XlangVMBuiltinConstructor::StaticMethod},
      {XlangVMNames::builtin_super, XlangVMBuiltinConstructor::Super},
  };
  for (const auto& spec : specs) {
    if (name == spec.name) {
      return spec.kind;
    }
  }
  return XlangVMBuiltinConstructor::Unknown;
}

XLANG3_HOT_INLINE XlangVMBuiltinConstructor xlang_vm_find_inherited_builtin_constructor(const ClassObject& klass) {
  if (class_has_builtin_base_name(const_cast<ClassObject*>(&klass), XlangVMNames::builtin_type)) {
    return XlangVMBuiltinConstructor::Type;
  }
  if (class_has_builtin_base_name(const_cast<ClassObject*>(&klass), XlangVMNames::builtin_int)) {
    return XlangVMBuiltinConstructor::Int;
  }
  if (class_has_builtin_base_name(const_cast<ClassObject*>(&klass), XlangVMNames::builtin_str)) {
    return XlangVMBuiltinConstructor::Str;
  }
  if (class_has_builtin_base_name(const_cast<ClassObject*>(&klass), XlangVMNames::builtin_float)) {
    return XlangVMBuiltinConstructor::Float;
  }
  if (class_has_builtin_base_name(const_cast<ClassObject*>(&klass), XlangVMNames::builtin_bytes)) {
    return XlangVMBuiltinConstructor::Bytes;
  }
  return XlangVMBuiltinConstructor::Unknown;
}

XLANG3_HOT_INLINE bool xlang_vm_infer_super_defining_class(
    Runtime& runtime,
    const Value& self,
    Value& out,
    std::string& error) {
  auto* instance = value_as_instance(self);
  if (instance == nullptr) {
    error = "super(): current self is not an instance";
    return false;
  }
  Value mro_value;
  if (!object_get_attr(instance->klass, "__mro__", mro_value, error)) {
    return false;
  }
  auto* mro = value_as_tuple(mro_value);
  if (mro == nullptr) {
    error = "super(): invalid method resolution order";
    return false;
  }
  const uint32_t current_function_id = runtime.current_frame_function_id();
  const auto* current_module_owner = runtime.current_frame_module_owner();
  Value defining_class;
  for (const auto& class_value : mro->items) {
    auto* klass = value_as_class(class_value);
    if (klass == nullptr) {
      continue;
    }
    for (const auto& attr : klass->attrs) {
      Value function_value;
      if (auto* method = value_as_static_method(attr.second)) {
        value_assign_fast(function_value, method->function);
      } else if (auto* method = value_as_class_method(attr.second)) {
        value_assign_fast(function_value, method->function);
      } else {
        value_assign_fast(function_value, attr.second);
      }
      auto* function = value_as_function(function_value);
      const bool same_module =
          function != nullptr &&
          current_module_owner != nullptr &&
          function->module != nullptr &&
          function->module.get() == current_module_owner->get();
      if (same_module && function->function_id == current_function_id) {
        value_assign_fast(defining_class, class_value);
      }
    }
  }
  if (defining_class.tag != ValueTag::Invalid) {
    value_assign_fast(out, defining_class);
    return true;
  }
  value_assign_fast(out, instance->klass);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_parse_int_text(std::string_view text, int base, int64_t& out) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  if (begin == end) {
    return false;
  }
  bool negative = false;
  if (text[begin] == '+' || text[begin] == '-') {
    negative = text[begin] == '-';
    ++begin;
  }
  if (begin == end || (base != 0 && (base < 2 || base > 36))) {
    return false;
  }
  if (base == 0) {
    base = 10;
    if (end - begin >= 2 && text[begin] == '0') {
      const char marker = static_cast<char>(std::tolower(static_cast<unsigned char>(text[begin + 1])));
      if (marker == 'x') {
        base = 16;
        begin += 2;
      } else if (marker == 'o') {
        base = 8;
        begin += 2;
      } else if (marker == 'b') {
        base = 2;
        begin += 2;
      }
    }
  } else if (end - begin >= 2 && text[begin] == '0') {
    const char marker = static_cast<char>(std::tolower(static_cast<unsigned char>(text[begin + 1])));
    if ((base == 16 && marker == 'x') || (base == 8 && marker == 'o') || (base == 2 && marker == 'b')) {
      begin += 2;
    }
  }
  if (begin == end) {
    return false;
  }
  int64_t result = 0;
  bool saw_digit = false;
  bool previous_separator = false;
  for (size_t i = begin; i < end; ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch == '_') {
      if (!saw_digit || previous_separator || i + 1 == end) {
        return false;
      }
      previous_separator = true;
      continue;
    }
    int digit = -1;
    if (ch >= '0' && ch <= '9') {
      digit = ch - '0';
    } else if (ch >= 'a' && ch <= 'z') {
      digit = ch - 'a' + 10;
    } else if (ch >= 'A' && ch <= 'Z') {
      digit = ch - 'A' + 10;
    }
    if (digit < 0 || digit >= base) {
      return false;
    }
    result = result * base + digit;
    saw_digit = true;
    previous_separator = false;
  }
  out = negative ? -result : result;
  return saw_digit;
}

XLANG3_HOT_INLINE bool call_builtin_type_constructor(
    Runtime& runtime,
    const ClassObject& klass,
    CallArgsView args,
    XlangRuntimeExecutionGuard& execution_lock,
    Value& out,
    std::string& error) {
  auto constructor = xlang_vm_find_builtin_constructor(klass.name);
  if (constructor == XlangVMBuiltinConstructor::Unknown) {
    constructor = xlang_vm_find_inherited_builtin_constructor(klass);
  }
  if (constructor == XlangVMBuiltinConstructor::Unknown) {
    return false;
  }

  if (args.has_keywords() || args.has_expansion()) {
    error = klass.name + "() keyword and expanded arguments are not implemented yet";
    return false;
  }

  if (constructor == XlangVMBuiltinConstructor::Type) {
    if (args.size() == 1) {
      return runtime_type_of_value(runtime, args.get(0), out);
    }
    if (args.size() != 3) {
      error = "type() expected 1 or 3 arguments";
      return false;
    }
    auto* name = value_as_string(args.get(0));
    auto* bases = value_as_tuple(args.get(1));
    auto* namespace_dict = value_as_dict(args.get(2));
    if (name == nullptr) {
      error = "type() argument 1 must be str";
      return false;
    }
    if (bases == nullptr) {
      error = "type() argument 2 must be tuple";
      return false;
    }
    if (namespace_dict == nullptr) {
      error = "type() argument 3 must be dict";
      return false;
    }
    std::vector<std::pair<std::string, Value>> attrs;
    attrs.reserve(namespace_dict->entries.size());
    for (const auto& entry : namespace_dict->entries) {
      auto* key = value_as_string(entry.first);
      if (key == nullptr) {
        error = "type() namespace keys must be strings";
        return false;
      }
      attrs.push_back({string_object_to_string(*key), entry.second});
    }
    Value base = Value::invalid();
    if (bases->items.empty()) {
      if (const auto* object_type = runtime.find_builtin(XlangVMNames::object_type)) {
        value_assign_fast(base, *object_type);
      }
    } else {
      for (const auto& item : bases->items) {
        if (value_as_class(item) == nullptr) {
          error = "type() bases must be classes";
          return false;
        }
      }
      value_assign_fast(base, bases->items[0]);
    }
    Value metaclass;
    metaclass.tag = ValueTag::Object;
    metaclass.as.obj = const_cast<Object*>(&klass.header);
    retain(metaclass);
    out = Value::class_object(string_object_to_string(*name), std::move(attrs), base, {}, std::move(metaclass));
    if (bases->items.size() > 1) {
      for (size_t i = 1; i < bases->items.size(); ++i) {
        if (!class_set_base(out, bases->items[i], error)) {
          return false;
        }
      }
    }
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Object) {
    if (args.size() != 0) {
      error = "object() expected no arguments";
      return false;
    }
    if (const auto* object_type = runtime.find_builtin(XlangVMNames::object_type)) {
      out = Value::instance(*object_type);
      return true;
    }
    error = "object type is not registered";
    return false;
  }

  if (constructor == XlangVMBuiltinConstructor::Str) {
    if (args.size() > 1) {
      error = "str() expected at most 1 argument";
      return false;
    }
    out = args.size() == 0 ? Value::string("") : Value::string(value_to_string(args.get(0)));
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Bool) {
    if (args.size() > 1) {
      error = "bool() expected at most 1 argument";
      return false;
    }
    out = Value::boolean(args.size() == 0 ? false : value_truthy(args.get(0)));
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Int) {
    if (args.size() > 2) {
      error = "int() expected at most 2 arguments";
      return false;
    }
    if (args.size() == 0) {
      out = Value::int64(0);
      return true;
    }
    const Value& value = args.get(0);
    if (value.tag == ValueTag::Int64) {
      value_assign_fast(out, value);
      return true;
    }
    if (value.tag == ValueTag::Bool) {
      out = Value::int64(value.as.b ? 1 : 0);
      return true;
    }
    if (value.tag == ValueTag::Double) {
      out = Value::int64(static_cast<int64_t>(value.as.f64));
      return true;
    }
    int base = 10;
    if (args.size() == 2) {
      if (args.get(1).tag != ValueTag::Int64) {
        error = "int() base must be an integer";
        return false;
      }
      base = static_cast<int>(args.get(1).as.i64);
      if (value_as_string(value) == nullptr && value_as_bytes(value) == nullptr && value_as_bytearray(value) == nullptr) {
        error = "int() can't convert non-string with explicit base";
        return false;
      }
    }
    if (auto* text = value_as_string(value)) {
      const std::string owned_text = string_object_to_string(*text);
      int64_t parsed = 0;
      if (xlang_vm_parse_int_text(owned_text, base, parsed)) {
        out = Value::int64(parsed);
        return true;
      }
    }
    if (auto* bytes = value_as_bytes(value)) {
      const std::string owned_text = bytes_object_to_string(*bytes);
      int64_t parsed = 0;
      if (xlang_vm_parse_int_text(owned_text, base, parsed)) {
        out = Value::int64(parsed);
        return true;
      }
    }
    if (auto* bytes = value_as_bytearray(value)) {
      int64_t parsed = 0;
      if (xlang_vm_parse_int_text(bytes->value, base, parsed)) {
        out = Value::int64(parsed);
        return true;
      }
    }
    error = "int() argument must be a string, bytes-like object, number, or bool";
    return false;
  }

  if (constructor == XlangVMBuiltinConstructor::Float) {
    if (args.size() > 1) {
      error = "float() expected at most 1 argument";
      return false;
    }
    if (args.size() == 0) {
      out = Value::number(0.0);
      return true;
    }
    const Value& value = args.get(0);
    if (value.tag == ValueTag::Double) {
      value_assign_fast(out, value);
      return true;
    }
    if (value.tag == ValueTag::Int64) {
      out = Value::number(static_cast<double>(value.as.i64));
      return true;
    }
    if (value.tag == ValueTag::Bool) {
      out = Value::number(value.as.b ? 1.0 : 0.0);
      return true;
    }
    if (auto* text = value_as_string(value)) {
      const std::string owned_text = string_object_to_string(*text);
      char* end = nullptr;
      const char* start = owned_text.c_str();
      const double parsed = std::strtod(start, &end);
      if (end != start && *end == '\0') {
        out = Value::number(parsed);
        return true;
      }
    }
    error = "float() argument must be a string, number, or bool";
    return false;
  }

  if (constructor == XlangVMBuiltinConstructor::Range) {
    if (args.size() < 1 || args.size() > 3) {
      error = "range() expected 1 to 3 arguments";
      return false;
    }
    int64_t start = 0;
    int64_t stop = 0;
    int64_t step = 1;
    auto read_int = [&](size_t index, int64_t& target) -> bool {
      const Value& value = args.get(index);
      if (value.tag != ValueTag::Int64) {
        error = "range() arguments must be int";
        return false;
      }
      target = value.as.i64;
      return true;
    };
    if (args.size() == 1) {
      if (!read_int(0, stop)) return false;
    } else {
      if (!read_int(0, start) || !read_int(1, stop)) return false;
      if (args.size() == 3 && !read_int(2, step)) return false;
    }
    if (step == 0) {
      error = "range() step must not be zero";
      return false;
    }
    out = Value::range(start, stop, step);
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::List || constructor == XlangVMBuiltinConstructor::Tuple || constructor == XlangVMBuiltinConstructor::Set) {
    if (args.size() > 1) {
      error = klass.name + "() expected at most 1 argument";
      return false;
    }
    std::vector<Value> items;
    if (args.size() == 1) {
      Value iterator;
      if (!sequence_get_iter(args.get(0), iterator, error)) {
        return false;
      }
      execution_lock.unlock();
      for (;;) {
        bool done = false;
        Value item;
        if (!sequence_iter_next(iterator, done, item, error)) {
          execution_lock.lock();
          return false;
        }
        if (done) break;
        items.push_back(std::move(item));
      }
      execution_lock.lock();
    }
    if (constructor == XlangVMBuiltinConstructor::List) {
      out = Value::list(std::move(items));
    } else if (constructor == XlangVMBuiltinConstructor::Tuple) {
      out = Value::tuple(std::move(items));
    } else {
      out = Value::set(std::move(items));
    }
    return true;
  }

  auto make_bytes_from_arg = [&](const Value& arg, std::string& bytes, std::string& local_error) -> bool {
    if (arg.tag == ValueTag::Int64) {
      if (arg.as.i64 < 0) {
        local_error = "negative count";
        return false;
      }
      bytes.assign(static_cast<size_t>(arg.as.i64), '\0');
      return true;
    }
    if (auto* source = value_as_bytes(arg)) {
      bytes = bytes_object_to_string(*source);
      return true;
    }
    if (auto* source = value_as_bytearray(arg)) {
      bytes = source->value;
      return true;
    }
    if (auto* view = value_as_memoryview(arg)) {
      for (size_t i = 0; i < view->size; ++i) {
        Value item;
        if (!sequence_get_item(arg, Value::int64(static_cast<int64_t>(i)), item, local_error)) {
          return false;
        }
        bytes.push_back(static_cast<char>(item.as.i64));
      }
      return true;
    }
    if (auto* string = value_as_string(arg)) {
      bytes = string_object_to_string(*string);
      return true;
    }
    Value iterator;
    if (!sequence_get_iter(arg, iterator, local_error)) {
      return false;
    }
    for (;;) {
      bool done = false;
      Value item;
      if (!sequence_iter_next(iterator, done, item, local_error)) {
        return false;
      }
      if (done) break;
      if (item.tag != ValueTag::Int64 || item.as.i64 < 0 || item.as.i64 > 255) {
        local_error = "bytes-like object requires integers in range(0, 256)";
        return false;
      }
      bytes.push_back(static_cast<char>(static_cast<unsigned char>(item.as.i64)));
    }
    return true;
  };

  if (constructor == XlangVMBuiltinConstructor::Dict) {
    if (args.size() > 1) {
      error = "dict() expected at most 1 argument";
      return false;
    }
    out = Value::dict({});
    if (args.size() == 0) {
      return true;
    }
    const Value& source = args.get(0);
    if (auto* dict = value_as_dict(source)) {
      for (const auto& entry : dict->entries) {
        if (!mapping_set_item(out, entry.first, entry.second, error)) {
          return false;
        }
      }
      return true;
    }
    if (auto* instance = value_as_instance(source)) {
      if (auto* storage = value_as_dict(instance->mapping_storage)) {
        for (const auto& entry : storage->entries) {
          if (!mapping_set_item(out, entry.first, entry.second, error)) {
            return false;
          }
        }
        return true;
      }
    }
    Value iterator;
    if (!sequence_get_iter(source, iterator, error)) {
      return false;
    }
    for (;;) {
      bool done = false;
      Value item;
      if (!sequence_iter_next(iterator, done, item, error)) {
        return false;
      }
      if (done) {
        break;
      }
      const TupleObject* tuple = value_as_tuple(item);
      const ListObject* list = value_as_list(item);
      const Value* key = nullptr;
      const Value* value = nullptr;
      if (tuple != nullptr && tuple->items.size() == 2) {
        key = &tuple->items[0];
        value = &tuple->items[1];
      } else if (list != nullptr && list->items.size() == 2) {
        key = &list->items[0];
        value = &list->items[1];
      } else {
        error = "dictionary update sequence element has length other than 2";
        return false;
      }
      if (!mapping_set_item(out, *key, *value, error)) {
        return false;
      }
    }
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Bytes) {
    if (args.size() > 1) {
      error = "bytes() expected at most 1 argument";
      return false;
    }
    if (args.size() == 0) {
      out = Value::bytes("");
      return true;
    }
    std::string bytes;
    if (!make_bytes_from_arg(args.get(0), bytes, error)) {
      return false;
    }
    out = Value::bytes(std::move(bytes));
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::ByteArray) {
    if (args.size() > 1) {
      error = "bytearray() expected at most 1 argument";
      return false;
    }
    if (args.size() == 0) {
      out = Value::bytearray("");
      return true;
    }
    std::string bytes;
    if (!make_bytes_from_arg(args.get(0), bytes, error)) {
      return false;
    }
    out = Value::bytearray(std::move(bytes));
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::MemoryView) {
    if (args.size() != 1) {
      error = "memoryview() expected 1 argument";
      return false;
    }
    const Value& source = args.get(0);
    if (auto* bytes = value_as_bytes(source)) {
      out = Value::memoryview(source, 0, bytes->size, true);
      return true;
    }
    if (auto* bytearray = value_as_bytearray(source)) {
      out = Value::memoryview(source, 0, bytearray->value.size(), false);
      return true;
    }
    if (auto* view = value_as_memoryview(source)) {
      out = Value::memoryview(view->owner, view->offset, view->size, view->readonly);
      return true;
    }
    error = "memoryview() requires a bytes-like object";
    return false;
  }

  if (constructor == XlangVMBuiltinConstructor::Property) {
    if (args.size() > 4) {
      error = "property() expected at most 4 arguments";
      return false;
    }
    out = Value::property(
        args.size() > 0 ? args.get(0) : Value::none(),
        args.size() > 1 ? args.get(1) : Value::none(),
        args.size() > 2 ? args.get(2) : Value::none(),
        args.size() > 3 ? args.get(3) : Value::none());
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::ClassMethod ||
      constructor == XlangVMBuiltinConstructor::StaticMethod) {
    if (args.size() != 1) {
      error = klass.name + "() expected 1 argument";
      return false;
    }
    const Value& function = args.get(0);
    if (value_as_function(function) == nullptr &&
        value_as_native_function(function) == nullptr &&
        value_as_bound_method(function) == nullptr) {
      error = klass.name + "() argument must be callable";
      return false;
    }
    out = constructor == XlangVMBuiltinConstructor::ClassMethod
        ? Value::class_method(function)
        : Value::static_method(function);
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Super) {
    if (args.size() != 0 && args.size() != 2) {
      error = "super() expected 0 or 2 arguments";
      return false;
    }
    Value klass;
    Value self;
    if (args.size() == 2) {
      if (value_as_class(args.get(0)) == nullptr) {
        error = "super() first argument must be type";
        return false;
      }
      value_assign_fast(klass, args.get(0));
      value_assign_fast(self, args.get(1));
    } else {
      Value locals = runtime.current_locals_snapshot();
      if (!mapping_get_item(locals, Value::string("self"), self, error)) {
        error = "super(): no current instance";
        return false;
      }
      if (!xlang_vm_infer_super_defining_class(runtime, self, klass, error)) {
        return false;
      }
    }
    out = Value::super_object(std::move(klass), std::move(self));
    return true;
  }

  return false;
}



} // namespace xlang3
