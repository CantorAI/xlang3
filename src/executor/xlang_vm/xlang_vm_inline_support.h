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
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <algorithm>
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
  Slice,
  Range,
  List,
  Tuple,
  Set,
  FrozenSet,
  Dict,
  Bytes,
  ByteArray,
  MemoryView,
  Property,
  ClassMethod,
  StaticMethod,
  Super,
  Module,
  Method,
};

struct XlangVMBuiltinConstructorSpec {
  const char* name;
  XlangVMBuiltinConstructor kind;
};

XLANG3_HOT_INLINE bool xlang_vm_collect_type_slots(const Value& value, std::vector<std::string>& slots) {
  if (auto* string = value_as_string(value)) {
    const auto name = string_object_to_string(*string);
    if (name != "__dict__" && name != "__weakref__" &&
        std::find(slots.begin(), slots.end(), name) == slots.end()) {
      slots.push_back(name);
    }
    return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    for (const auto& item : tuple->items) {
      if (!xlang_vm_collect_type_slots(item, slots)) {
        return false;
      }
    }
    return true;
  }
  if (auto* list = value_as_list(value)) {
    for (const auto& item : list->items) {
      if (!xlang_vm_collect_type_slots(item, slots)) {
        return false;
      }
    }
    return true;
  }
  if (auto* set = value_as_set(value)) {
    for (const auto& item : set->items) {
      if (!xlang_vm_collect_type_slots(item, slots)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

XLANG3_HOT_INLINE DictObject* xlang_vm_type_namespace_dict(const Value& value) {
  if (auto* dict = value_as_dict(value)) {
    return dict;
  }
  if (auto* instance = value_as_instance(value)) {
    return value_as_dict(instance->mapping_storage);
  }
  return nullptr;
}

XLANG3_HOT_INLINE bool xlang_vm_collect_type_namespace_attrs(
    Runtime& runtime,
    const Value& namespace_value,
    std::vector<std::pair<std::string, Value>>& attrs,
    std::string& error) {
  if (auto* namespace_dict = xlang_vm_type_namespace_dict(namespace_value)) {
    attrs.reserve(namespace_dict->entries.size());
    for (const auto& entry : namespace_dict->entries) {
      auto* key = value_as_string(entry.first);
      if (key == nullptr) {
        error = "type() namespace keys must be strings";
        return false;
      }
      attrs.push_back({string_object_to_string(*key), entry.second});
    }
    return true;
  }
  if (!mapping_is_mapping(namespace_value)) {
    error = "type() argument 3 must be a mapping";
    return false;
  }

  Value iterator;
  if (!mapping_get_iter(namespace_value, iterator, error)) {
    return false;
  }
  for (;;) {
    bool done = false;
    Value key_value;
    if (!mapping_iter_next(iterator, done, key_value, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    auto* key = value_as_string(key_value);
    if (key == nullptr) {
      error = "type() namespace keys must be strings";
      return false;
    }
    Value item;
    if (!mapping_get_item(namespace_value, key_value, item, error)) {
      return false;
    }
    attrs.push_back({string_object_to_string(*key), item});
  }
}

XLANG3_HOT_INLINE bool xlang_vm_inline_class_attrs_have(
    const std::vector<std::pair<std::string, Value>>& attrs,
    const std::string& name) {
  for (const auto& attr : attrs) {
    if (attr.first == name) {
      return true;
    }
  }
  return false;
}

XLANG3_HOT_INLINE void xlang_vm_collect_set_name_descriptors(
    const std::vector<std::pair<std::string, Value>>& attrs,
    std::vector<std::pair<std::string, Value>>& descriptors) {
  for (const auto& attr : attrs) {
    Value set_name;
    std::string ignored;
    if (object_get_attr(attr.second, "__set_name__", set_name, ignored)) {
      descriptors.push_back({attr.first, std::move(set_name)});
    }
  }
}

XLANG3_HOT_INLINE bool xlang_vm_call_set_name_descriptors(
    Runtime& runtime,
    const Value& cls,
    const std::vector<std::pair<std::string, Value>>& descriptors,
    std::string& error) {
  for (const auto& descriptor : descriptors) {
    Value name_arg = Value::string(descriptor.first);
    Value call_args[] = {cls, name_arg};
    Value ignored;
    if (!runtime_call_callable(runtime, descriptor.second, call_args, 2, ignored, error)) {
      if (error.empty()) {
        error = "Error calling __set_name__";
      }
      return false;
    }
  }
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_call_init_subclass_for_new_class(
    Runtime& runtime,
    const Value& cls,
    TupleObject* bases,
    const std::vector<std::pair<std::string, Value>>& keywords,
    std::string& error) {
  if (bases == nullptr || bases->items.empty()) {
    return true;
  }
  Value hook;
  if (!object_lookup_class_attr(bases->items[0], "__init_subclass__", hook, error)) {
    error.clear();
    return true;
  }
  Value ignored;
  if (auto* method = value_as_class_method(hook)) {
    Value call_args[] = {cls};
    return runtime_call_callable_kw(runtime, method->function, call_args, 1, keywords, ignored, error);
  }
  if (auto* static_method = value_as_static_method(hook)) {
    return runtime_call_callable_kw(runtime, static_method->function, nullptr, 0, keywords, ignored, error);
  }
  if (value_as_function(hook) != nullptr || value_as_native_function(hook) != nullptr) {
    Value call_args[] = {cls};
    return runtime_call_callable_kw(runtime, hook, call_args, 1, keywords, ignored, error);
  }
  return runtime_call_callable_kw(runtime, hook, nullptr, 0, keywords, ignored, error);
}

XLANG3_HOT_INLINE std::string xlang_vm_inline_current_module_name(Runtime& runtime) {
  Value name_value;
  std::string ignored;
  if (module_get_attr(runtime.current_globals_module(), "__name__", name_value, ignored)) {
    if (auto* name = value_as_string(name_value)) {
      return string_object_to_string(*name);
    }
  }
  return "__main__";
}

XLANG3_HOT_INLINE bool xlang_vm_value_has_abstract_marker(const Value& value) {
  Value marker;
  std::string ignored;
  return object_get_attr(value, "__isabstractmethod__", marker, ignored) && value_truthy(marker);
}

XLANG3_HOT_INLINE void xlang_vm_add_abstract_name(std::vector<Value>& names, const std::string& name) {
  for (const auto& item : names) {
    auto* string = value_as_string(item);
    if (string != nullptr && string_object_to_string(*string) == name) {
      return;
    }
  }
  names.push_back(Value::string(name));
}

XLANG3_HOT_INLINE void xlang_vm_collect_abstract_names_from_iterable(const Value& value, std::vector<std::string>& names) {
  auto add_name = [&names](const Value& item) {
    auto* string = value_as_string(item);
    if (string == nullptr) {
      return;
    }
    const auto name = string_object_to_string(*string);
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      names.push_back(name);
    }
  };
  if (auto* set = value_as_set(value)) {
    for (const auto& item : set->items) {
      add_name(item);
    }
  } else if (auto* tuple = value_as_tuple(value)) {
    for (const auto& item : tuple->items) {
      add_name(item);
    }
  } else if (auto* list = value_as_list(value)) {
    for (const auto& item : list->items) {
      add_name(item);
    }
  }
}

XLANG3_HOT_INLINE Value xlang_vm_abc_abstract_methods_for_type_constructor_attrs(
    TupleObject* bases,
    const std::vector<std::pair<std::string, Value>>& attrs) {
  std::vector<Value> abstracts;
  std::vector<std::string> inherited_names;
  for (const auto& base : bases->items) {
    Value base_abstracts;
    std::string ignored;
    if (object_get_attr(base, "__abstractmethods__", base_abstracts, ignored)) {
      xlang_vm_collect_abstract_names_from_iterable(base_abstracts, inherited_names);
    }
  }
  for (const auto& name : inherited_names) {
    Value override_value;
    bool has_override = false;
    for (const auto& attr : attrs) {
      if (attr.first == name) {
        value_assign_fast(override_value, attr.second);
        has_override = true;
        break;
      }
    }
    if (!has_override || xlang_vm_value_has_abstract_marker(override_value)) {
      xlang_vm_add_abstract_name(abstracts, name);
    }
  }
  for (const auto& attr : attrs) {
    if (xlang_vm_value_has_abstract_marker(attr.second)) {
      xlang_vm_add_abstract_name(abstracts, attr.first);
    }
  }
  return Value::frozenset(std::move(abstracts));
}

XLANG3_HOT_INLINE Value xlang_vm_abc_abstract_methods_for_type_constructor(TupleObject* bases, DictObject* namespace_dict) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.reserve(namespace_dict->entries.size());
  for (const auto& entry : namespace_dict->entries) {
    auto* key = value_as_string(entry.first);
    if (key != nullptr) {
      attrs.push_back({string_object_to_string(*key), entry.second});
    }
  }
  return xlang_vm_abc_abstract_methods_for_type_constructor_attrs(bases, attrs);
}

XLANG3_HOT_INLINE XlangVMBuiltinConstructor xlang_vm_find_builtin_constructor(const std::string& name) {
  static constexpr XlangVMBuiltinConstructorSpec specs[] = {
      {XlangVMNames::builtin_type, XlangVMBuiltinConstructor::Type},
      {XlangVMNames::object_type, XlangVMBuiltinConstructor::Object},
      {XlangVMNames::builtin_str, XlangVMBuiltinConstructor::Str},
      {XlangVMNames::builtin_bool, XlangVMBuiltinConstructor::Bool},
      {XlangVMNames::builtin_int, XlangVMBuiltinConstructor::Int},
      {XlangVMNames::builtin_float, XlangVMBuiltinConstructor::Float},
      {XlangVMNames::builtin_slice, XlangVMBuiltinConstructor::Slice},
      {XlangVMNames::builtin_range, XlangVMBuiltinConstructor::Range},
      {XlangVMNames::builtin_list, XlangVMBuiltinConstructor::List},
      {XlangVMNames::builtin_tuple, XlangVMBuiltinConstructor::Tuple},
      {XlangVMNames::builtin_set, XlangVMBuiltinConstructor::Set},
      {XlangVMNames::builtin_frozenset, XlangVMBuiltinConstructor::FrozenSet},
      {XlangVMNames::builtin_dict, XlangVMBuiltinConstructor::Dict},
      {XlangVMNames::builtin_bytes, XlangVMBuiltinConstructor::Bytes},
      {XlangVMNames::builtin_bytearray, XlangVMBuiltinConstructor::ByteArray},
      {XlangVMNames::builtin_memoryview, XlangVMBuiltinConstructor::MemoryView},
      {XlangVMNames::builtin_property, XlangVMBuiltinConstructor::Property},
      {XlangVMNames::builtin_classmethod, XlangVMBuiltinConstructor::ClassMethod},
      {XlangVMNames::builtin_staticmethod, XlangVMBuiltinConstructor::StaticMethod},
      {XlangVMNames::builtin_super, XlangVMBuiltinConstructor::Super},
      {XlangVMNames::builtin_module, XlangVMBuiltinConstructor::Module},
      {XlangVMNames::builtin_method, XlangVMBuiltinConstructor::Method},
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

XLANG3_HOT_INLINE bool xlang_vm_class_is_builtin_module_class(const ClassObject& klass) {
  if (klass.name == XlangVMNames::builtin_module) {
    return true;
  }
  auto it = klass.attrs.find("__module__");
  if (it == klass.attrs.end()) {
    return true;
  }
  auto* module_name = value_as_string(it->second);
  return module_name != nullptr && string_object_to_string(*module_name) == "builtins";
}

XLANG3_HOT_INLINE bool xlang_vm_infer_super_defining_class(
    Runtime& runtime,
    const Value& self,
    Value& out,
    std::string& error) {
  auto* instance = value_as_instance(self);
  Value subject_class;
  if (instance != nullptr) {
    value_assign_fast(subject_class, instance->klass);
  } else if (value_as_class(self) != nullptr) {
    value_assign_fast(subject_class, self);
  } else {
    error = "super(): current self is not an instance or class";
    return false;
  }
  Value mro_value;
  if (!object_get_attr(subject_class, "__mro__", mro_value, error)) {
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
  if (value_as_class(self) != nullptr) {
    Value metaclass_value;
    std::string meta_error;
    if (object_get_attr(self, "__class__", metaclass_value, meta_error)) {
      Value meta_mro_value;
      auto* meta_mro = value_as_tuple(meta_mro_value);
      if (object_get_attr(metaclass_value, "__mro__", meta_mro_value, meta_error) &&
          (meta_mro = value_as_tuple(meta_mro_value)) != nullptr) {
        for (const auto& class_value : meta_mro->items) {
          auto* candidate = value_as_class(class_value);
          if (candidate == nullptr) {
            continue;
          }
          for (const auto& attr : candidate->attrs) {
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
              value_assign_fast(out, class_value);
              return true;
            }
          }
        }
      }
    }
  }
  value_assign_fast(out, subject_class);
  return true;
}

XLANG3_HOT_INLINE bool xlang_vm_current_super_first_argument(
    Runtime& runtime,
    const Value& locals,
    Value& out) {
  std::string ignored;
  const auto* owner = runtime.current_frame_module_owner();
  if (owner != nullptr && *owner) {
    const uint32_t function_id = runtime.current_frame_function_id();
    if (function_id < (*owner)->functions.size() && !(*owner)->functions[function_id].params.empty()) {
      const auto& first_name = (*owner)->functions[function_id].params[0];
      if (!first_name.empty() && mapping_get_item(locals, Value::string(first_name), out, ignored)) {
        return true;
      }
    }
  }
  return mapping_get_item(locals, Value::string("self"), out, ignored) ||
         mapping_get_item(locals, Value::string("cls"), out, ignored);
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
  bool exact_builtin_constructor = constructor != XlangVMBuiltinConstructor::Unknown;
  if (constructor != XlangVMBuiltinConstructor::Unknown && !xlang_vm_class_is_builtin_module_class(klass)) {
    constructor = XlangVMBuiltinConstructor::Unknown;
    exact_builtin_constructor = false;
  }
  if (constructor == XlangVMBuiltinConstructor::Unknown) {
    constructor = xlang_vm_find_inherited_builtin_constructor(klass);
    exact_builtin_constructor = false;
  }
  if (constructor == XlangVMBuiltinConstructor::Unknown) {
    return false;
  }

  std::vector<Value> constructor_positional;
  std::vector<std::pair<std::string, Value>> constructor_keywords;
  CallArgsView constructor_args = args;

  auto materialize_constructor_args = [&]() -> bool {
    constructor_positional.clear();
    constructor_keywords.clear();
    constructor_positional.reserve(args.size());
    for (size_t i = 0; i < args.size(); ++i) {
      constructor_positional.push_back(args.get(i));
    }
    auto expand_star_arg = [&](uint32_t star_reg) -> bool {
      Value iterator;
      if (!runtime_get_iter(runtime, args.registers[star_reg], iterator, error)) {
        error = "* argument must be iterable";
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
        constructor_positional.push_back(std::move(item));
      }
      return true;
    };
    if (args.star_args != nullptr && !args.star_args->empty()) {
      for (uint32_t star_reg : *args.star_args) {
        if (!expand_star_arg(star_reg)) {
          return false;
        }
      }
    } else if (args.star_arg != UINT32_MAX) {
      if (!expand_star_arg(args.star_arg)) {
        return false;
      }
    }
    auto add_keyword = [&](std::string name, const Value& value) -> bool {
      for (const auto& existing : constructor_keywords) {
        if (existing.first == name) {
          error = "got multiple values for keyword argument '" + name + "'";
          return false;
        }
      }
      constructor_keywords.push_back({std::move(name), value});
      return true;
    };
    if (args.keyword_args != nullptr) {
      for (const auto& keyword : *args.keyword_args) {
        if (!add_keyword(keyword.name, args.registers[keyword.value_reg])) {
          return false;
        }
      }
    }
    auto expand_kw_star_arg = [&](uint32_t kw_star_reg) -> bool {
      auto* kwargs = value_as_dict(args.registers[kw_star_reg]);
      if (kwargs == nullptr) {
        error = "** argument must be dict";
        return false;
      }
      for (const auto& entry : kwargs->entries) {
        auto* key = value_as_string(entry.first);
        if (key == nullptr) {
          error = "** argument keys must be strings";
          return false;
        }
        if (!add_keyword(string_object_to_string(*key), entry.second)) {
          return false;
        }
      }
      return true;
    };
    if (args.kw_star_args != nullptr && !args.kw_star_args->empty()) {
      for (uint32_t kw_star_reg : *args.kw_star_args) {
        if (!expand_kw_star_arg(kw_star_reg)) {
          return false;
        }
      }
    } else if (args.kw_star_arg != UINT32_MAX) {
      if (!expand_kw_star_arg(args.kw_star_arg)) {
        return false;
      }
    }
    constructor_args.leading = constructor_positional.data();
    constructor_args.leading_count = static_cast<uint32_t>(constructor_positional.size());
    constructor_args.registers = nullptr;
    constructor_args.register_args = nullptr;
    constructor_args.keyword_args = nullptr;
    constructor_args.star_arg = UINT32_MAX;
    constructor_args.kw_star_arg = UINT32_MAX;
    constructor_args.star_args = nullptr;
    constructor_args.kw_star_args = nullptr;
    return true;
  };

  if ((args.has_keywords() || args.has_expansion()) && !materialize_constructor_args()) {
    return false;
  }

  auto reject_constructor_keywords = [&]() -> bool {
    if (!constructor_keywords.empty()) {
      error = klass.name + "() got an unexpected keyword argument '" + constructor_keywords.front().first + "'";
      return false;
    }
    return true;
  };

  auto find_constructor_keyword = [&](const char* name) -> const Value* {
    for (const auto& keyword : constructor_keywords) {
      if (keyword.first == name) {
        return &keyword.second;
      }
    }
    return nullptr;
  };

  auto reject_constructor_keywords_except = [&](std::initializer_list<const char*> allowed) -> bool {
    for (const auto& keyword : constructor_keywords) {
      bool ok = false;
      for (const char* name : allowed) {
        if (keyword.first == name) {
          ok = true;
          break;
        }
      }
      if (!ok) {
        error = klass.name + "() got an unexpected keyword argument '" + keyword.first + "'";
        return false;
      }
    }
    return true;
  };

  auto require_constructor_string_keyword = [&](const Value* value, const char* name) -> bool {
    if (value == nullptr || value->tag == ValueTag::None) {
      return true;
    }
    if (value_as_string(*value) == nullptr) {
      error = klass.name + "() " + std::string(name) + " must be str or None";
      return false;
    }
    return true;
  };

  if (constructor == XlangVMBuiltinConstructor::Type) {
    if (constructor_args.size() == 1) {
      if (!reject_constructor_keywords()) return false;
      return runtime_type_of_value(runtime, constructor_args.get(0), out);
    }
    if (constructor_args.size() != 3) {
      error = "type() expected 1 or 3 arguments";
      return false;
    }
    auto* name = value_as_string(constructor_args.get(0));
    auto* bases = value_as_tuple(constructor_args.get(1));
    if (name == nullptr) {
      error = "type() argument 1 must be str";
      return false;
    }
    if (bases == nullptr) {
      error = "type() argument 2 must be tuple";
      return false;
    }
    std::vector<std::pair<std::string, Value>> attrs;
    if (!xlang_vm_collect_type_namespace_attrs(runtime, constructor_args.get(2), attrs, error)) {
      return false;
    }
    std::vector<std::string> explicit_slots;
    bool has_explicit_slots = false;
    for (const auto& attr : attrs) {
      if (attr.first == "__slots__") {
        has_explicit_slots = true;
        if (!xlang_vm_collect_type_slots(attr.second, explicit_slots)) {
          error = "type() __slots__ must be a string or iterable of strings";
          return false;
        }
      }
    }
    if (has_explicit_slots) {
      for (const auto& slot : explicit_slots) {
        for (const auto& attr : attrs) {
          if (attr.first == slot && attr.first != "__doc__") {
            error = "'" + slot + "' in __slots__ conflicts with class variable";
            return false;
          }
        }
      }
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
    std::string class_name = string_object_to_string(*name);
    if (!xlang_vm_inline_class_attrs_have(attrs, "__module__")) {
      attrs.push_back({"__module__", Value::string(xlang_vm_inline_current_module_name(runtime))});
    }
    if (!xlang_vm_inline_class_attrs_have(attrs, "__qualname__")) {
      attrs.push_back({"__qualname__", Value::string(class_name)});
    }
    std::vector<std::pair<std::string, Value>> set_name_descriptors;
    xlang_vm_collect_set_name_descriptors(attrs, set_name_descriptors);
    Value abstract_methods = Value::invalid();
    const bool needs_abstract_methods =
        klass.name == "ABCMeta" || class_has_builtin_base_name(const_cast<ClassObject*>(&klass), "ABCMeta");
    if (needs_abstract_methods) {
      abstract_methods = xlang_vm_abc_abstract_methods_for_type_constructor_attrs(bases, attrs);
    }
    out = Value::class_object(class_name, std::move(attrs), base, {}, std::move(metaclass));
    if (!xlang_vm_call_set_name_descriptors(runtime, out, set_name_descriptors, error)) {
      return false;
    }
    if (bases->items.size() > 1) {
      for (size_t i = 1; i < bases->items.size(); ++i) {
        if (!class_set_base(out, bases->items[i], error)) {
          return false;
        }
      }
    }
    if (needs_abstract_methods) {
      if (!object_set_attr(out, "__abstractmethods__", abstract_methods, error)) {
        return false;
      }
    }
    if (!xlang_vm_call_init_subclass_for_new_class(runtime, out, bases, constructor_keywords, error)) {
      return false;
    }
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Object) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() != 0) {
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
    if (!reject_constructor_keywords_except({"encoding", "errors"})) return false;
    const Value* encoding = find_constructor_keyword("encoding");
    const Value* errors_value = find_constructor_keyword("errors");
    if (!require_constructor_string_keyword(encoding, "encoding") ||
        !require_constructor_string_keyword(errors_value, "errors")) {
      return false;
    }
    if (constructor_args.size() > 3) {
      error = "str() expected at most 3 arguments";
      return false;
    }
    if (constructor_args.size() >= 2) {
      encoding = &constructor_args.get(1);
      if (!require_constructor_string_keyword(encoding, "encoding")) return false;
    }
    if (constructor_args.size() >= 3) {
      errors_value = &constructor_args.get(2);
      if (!require_constructor_string_keyword(errors_value, "errors")) return false;
    }
    if (constructor_args.size() == 0) {
      out = Value::string("");
      return true;
    }
    const Value& source = constructor_args.get(0);
    if (encoding != nullptr && encoding->tag != ValueTag::None) {
      if (auto* bytes = value_as_bytes(source)) {
        out = Value::string(bytes_object_to_string(*bytes));
        return true;
      }
      if (auto* bytearray = value_as_bytearray(source)) {
        out = Value::string(bytearray->value);
        return true;
      }
      if (auto* view = value_as_memoryview(source)) {
        std::string text;
        for (size_t i = 0; i < view->size; ++i) {
          Value item;
          if (!sequence_get_item(source, Value::int64(static_cast<int64_t>(i)), item, error)) {
            return false;
          }
          text.push_back(static_cast<char>(item.as.i64));
        }
        out = Value::string(std::move(text));
        return true;
      }
      error = "decoding str is not supported";
      return false;
    }
    return builtin_str_from_value(runtime, source, out, error);
  }

  if (constructor == XlangVMBuiltinConstructor::Bool) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() > 1) {
      error = "bool() expected at most 1 argument";
      return false;
    }
    out = Value::boolean(constructor_args.size() == 0 ? false : value_truthy(constructor_args.get(0)));
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Int) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() > 2) {
      error = "int() expected at most 2 arguments";
      return false;
    }
    auto finish_int_value = [&](const Value& parsed) -> bool {
      if (exact_builtin_constructor) {
        value_assign_fast(out, parsed);
        return true;
      }
      Value klass_value;
      klass_value.tag = ValueTag::Object;
      klass_value.as.obj = const_cast<Object*>(&klass.header);
      retain(klass_value);
      out = Value::instance(std::move(klass_value));
      std::string attr_error;
      return object_set_attr(out, "__xlang3_int_value__", parsed, attr_error);
    };
    auto finish_int = [&](int64_t parsed) -> bool {
      return finish_int_value(Value::int64(parsed));
    };
    if (constructor_args.size() == 0) {
      if (!finish_int(0)) {
        error = "int subclass construction failed";
        return false;
      }
      return true;
    }
    const Value& value = constructor_args.get(0);
    if (value.tag == ValueTag::Int64) {
      if (!finish_int(value.as.i64)) {
        error = "int subclass construction failed";
        return false;
      }
      return true;
    }
    if (value_as_bigint(value) != nullptr) {
      if (!finish_int_value(value)) {
        error = "int subclass construction failed";
        return false;
      }
      return true;
    }
    if (value.tag == ValueTag::Bool) {
      if (!finish_int(value.as.b ? 1 : 0)) {
        error = "int subclass construction failed";
        return false;
      }
      return true;
    }
    if (value.tag == ValueTag::Double) {
      if (!finish_int(static_cast<int64_t>(value.as.f64))) {
        error = "int subclass construction failed";
        return false;
      }
      return true;
    }
    Value stored;
    std::string stored_error;
    if (constructor_args.size() == 1 &&
        object_get_attr(value, "__xlang3_int_value__", stored, stored_error) &&
        (stored.tag == ValueTag::Int64 || value_as_bigint(stored) != nullptr)) {
      if (!finish_int_value(stored)) {
        error = "int subclass construction failed";
        return false;
      }
      return true;
    }
    if (constructor_args.size() == 1 &&
        object_get_attr(value, "_value_", stored, stored_error) &&
        (stored.tag == ValueTag::Int64 || value_as_bigint(stored) != nullptr)) {
      if (!finish_int_value(stored)) {
        error = "int subclass construction failed";
        return false;
      }
      return true;
    }
    if (constructor_args.size() == 1) {
      Value convert_method;
      Value converted;
      std::string call_error;
      if ((object_get_attr(value, "__int__", convert_method, call_error) ||
           object_get_attr(value, "__index__", convert_method, call_error))) {
        if (!runtime_call_callable(runtime, convert_method, nullptr, 0, converted, call_error)) {
          error = call_error;
          return false;
        }
        if (converted.tag == ValueTag::Int64 || value_as_bigint(converted) != nullptr) {
          if (!finish_int_value(converted)) {
            error = "int subclass construction failed";
            return false;
          }
          return true;
        }
        if (converted.tag == ValueTag::Bool) {
          if (!finish_int(converted.as.b ? 1 : 0)) {
            error = "int subclass construction failed";
            return false;
          }
          return true;
        }
        Value converted_stored;
        std::string converted_error;
        if ((object_get_attr(converted, "__xlang3_int_value__", converted_stored, converted_error) ||
             object_get_attr(converted, "_value_", converted_stored, converted_error)) &&
            (converted_stored.tag == ValueTag::Int64 || value_as_bigint(converted_stored) != nullptr)) {
          if (!finish_int_value(converted_stored)) {
            error = "int subclass construction failed";
            return false;
          }
          return true;
        }
        error = "__int__ returned non-int";
        return false;
      }
    }
    int base = 10;
    if (constructor_args.size() == 2) {
      if (constructor_args.get(1).tag != ValueTag::Int64) {
        error = "int() base must be an integer";
        return false;
      }
      base = static_cast<int>(constructor_args.get(1).as.i64);
      if (value_as_string(value) == nullptr && value_as_bytes(value) == nullptr && value_as_bytearray(value) == nullptr) {
        error = "int() can't convert non-string with explicit base";
        return false;
      }
    }
    if (auto* text = value_as_string(value)) {
      std::string parse_error;
      Value parsed = value_bigint_from_decimal(string_object_view(*text), base, parse_error);
      if (parsed.tag != ValueTag::Invalid) {
        if (!finish_int_value(parsed)) {
          error = "int subclass construction failed";
          return false;
        }
        return true;
      }
    }
    if (auto* bytes = value_as_bytes(value)) {
      std::string parse_error;
      const auto view = bytes_object_view(*bytes);
      Value parsed = value_bigint_from_decimal(view, base, parse_error);
      if (parsed.tag != ValueTag::Invalid) {
        if (!finish_int_value(parsed)) {
          error = "int subclass construction failed";
          return false;
        }
        return true;
      }
    }
    if (auto* bytes = value_as_bytearray(value)) {
      std::string parse_error;
      Value parsed = value_bigint_from_decimal(bytes->value, base, parse_error);
      if (parsed.tag != ValueTag::Invalid) {
        if (!finish_int_value(parsed)) {
          error = "int subclass construction failed";
          return false;
        }
        return true;
      }
    }
    error = "int() argument must be a string, bytes-like object, number, or bool";
    return false;
  }

  if (constructor == XlangVMBuiltinConstructor::Float) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() > 1) {
      error = "float() expected at most 1 argument";
      return false;
    }
    auto finish_float = [&](double parsed) -> bool {
      if (exact_builtin_constructor) {
        out = Value::number(parsed);
        return true;
      }
      Value klass_value;
      klass_value.tag = ValueTag::Object;
      klass_value.as.obj = const_cast<Object*>(&klass.header);
      retain(klass_value);
      out = Value::instance(std::move(klass_value));
      std::string attr_error;
      return object_set_attr(out, "__xlang3_float_value__", Value::number(parsed), attr_error);
    };
    if (constructor_args.size() == 0) {
      if (!finish_float(0.0)) {
        error = "float subclass construction failed";
        return false;
      }
      return true;
    }
    const Value& value = constructor_args.get(0);
    if (value.tag == ValueTag::Double) {
      if (!finish_float(value.as.f64)) {
        error = "float subclass construction failed";
        return false;
      }
      return true;
    }
    if (value.tag == ValueTag::Int64) {
      if (!finish_float(static_cast<double>(value.as.i64))) {
        error = "float subclass construction failed";
        return false;
      }
      return true;
    }
    if (value.tag == ValueTag::Bool) {
      if (!finish_float(value.as.b ? 1.0 : 0.0)) {
        error = "float subclass construction failed";
        return false;
      }
      return true;
    }
    Value stored;
    std::string stored_error;
    if (constructor_args.size() == 1 &&
        object_get_attr(value, "__xlang3_float_value__", stored, stored_error) && stored.tag == ValueTag::Double) {
      if (!finish_float(stored.as.f64)) {
        error = "float subclass construction failed";
        return false;
      }
      return true;
    }
    if (auto* text = value_as_string(value)) {
      const std::string owned_text = string_object_to_string(*text);
      char* end = nullptr;
      const char* start = owned_text.c_str();
      const double parsed = std::strtod(start, &end);
      if (end != start && *end == '\0') {
        if (!finish_float(parsed)) {
          error = "float subclass construction failed";
          return false;
        }
        return true;
      }
    }
    if (auto* bytes = value_as_bytes(value)) {
      const std::string owned_text = bytes_object_to_string(*bytes);
      char* end = nullptr;
      const char* start = owned_text.c_str();
      const double parsed = std::strtod(start, &end);
      if (end != start && *end == '\0') {
        if (!finish_float(parsed)) {
          error = "float subclass construction failed";
          return false;
        }
        return true;
      }
    }
    if (auto* bytes = value_as_bytearray(value)) {
      char* end = nullptr;
      const char* start = bytes->value.c_str();
      const double parsed = std::strtod(start, &end);
      if (end != start && *end == '\0') {
        if (!finish_float(parsed)) {
          error = "float subclass construction failed";
          return false;
        }
        return true;
      }
    }
    error = "float() argument must be a string, bytes-like object, number, or bool";
    return false;
  }

  if (constructor == XlangVMBuiltinConstructor::Range) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() < 1 || constructor_args.size() > 3) {
      error = "range() expected 1 to 3 arguments";
      return false;
    }
    int64_t start = 0;
    int64_t stop = 0;
    int64_t step = 1;
    auto read_int = [&](size_t index, int64_t& target) -> bool {
      const Value& value = constructor_args.get(index);
      if (!value_int_like_to_i64(value, target)) {
        error = "range() arguments must be int";
        return false;
      }
      return true;
    };
    if (constructor_args.size() == 1 && read_int(0, stop)) {
      out = Value::range(0, stop, 1);
      return true;
    }
    if (constructor_args.size() >= 2 &&
        read_int(0, start) &&
        read_int(1, stop) &&
        (constructor_args.size() != 3 || read_int(2, step))) {
      if (step == 0) {
        error = "range() step must not be zero";
        return false;
      }
      out = Value::range(start, stop, step);
      return true;
    }
    error.clear();
    Value range_start = constructor_args.size() == 1 ? Value::int64(0) : constructor_args.get(0);
    Value range_stop = constructor_args.size() == 1 ? constructor_args.get(0) : constructor_args.get(1);
    Value range_step = constructor_args.size() == 3 ? constructor_args.get(2) : Value::int64(1);
    Value zero_compare;
    if (!value_int_like_compare("==", range_step, Value::int64(0), zero_compare) ||
        zero_compare.tag != ValueTag::Bool) {
      error = "range() arguments must be int";
      return false;
    }
    if (zero_compare.as.b) {
      error = "range() step must not be zero";
      return false;
    }
    out = Value::range_values(range_start, range_stop, range_step);
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Slice) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() < 1 || constructor_args.size() > 3) {
      error = "slice() expected 1 to 3 arguments";
      return false;
    }
    Value start;
    Value stop;
    Value step;
    if (constructor_args.size() == 1) {
      value_set_none(start);
      stop = constructor_args.get(0);
      value_set_none(step);
    } else {
      start = constructor_args.get(0);
      stop = constructor_args.get(1);
      if (constructor_args.size() == 3) {
        step = constructor_args.get(2);
      } else {
        value_set_none(step);
      }
    }
    out = Value::slice(std::move(start), std::move(stop), std::move(step));
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::List || constructor == XlangVMBuiltinConstructor::Tuple ||
      constructor == XlangVMBuiltinConstructor::Set || constructor == XlangVMBuiltinConstructor::FrozenSet) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() > 1) {
      error = klass.name + "() expected at most 1 argument";
      return false;
    }
    std::vector<Value> items;
    if (constructor_args.size() == 1) {
      if (!runtime_collect_iterable(runtime, constructor_args.get(0), items, error)) {
        return false;
      }
    }
    if (constructor == XlangVMBuiltinConstructor::List) {
      out = Value::list(std::move(items));
    } else if (constructor == XlangVMBuiltinConstructor::Tuple) {
      Value tuple_storage = Value::tuple(std::move(items));
      if (exact_builtin_constructor) {
        value_assign_fast(out, tuple_storage);
      } else {
        Value klass_value;
        klass_value.tag = ValueTag::Object;
        klass_value.as.obj = const_cast<Object*>(&klass.header);
        retain(klass_value);
        out = Value::instance(std::move(klass_value));
        std::string attr_error;
        if (!object_set_attr(out, "_tuple", tuple_storage, attr_error)) {
          error = "tuple subclass construction failed";
          return false;
        }
      }
    } else if (constructor == XlangVMBuiltinConstructor::FrozenSet) {
      out = Value::frozenset(std::move(items));
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
      local_error = "string argument without an encoding";
      return false;
    }
    Value iterator;
    if (!runtime_get_iter(runtime, arg, iterator, local_error)) {
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
    if (constructor_args.size() > 1) {
      error = "dict() expected at most 1 argument";
      return false;
    }
    out = Value::dict({});
    auto add_constructor_keywords_to_dict = [&]() -> bool {
      for (const auto& keyword : constructor_keywords) {
        if (!mapping_set_item(out, Value::string(keyword.first), keyword.second, error)) {
          return false;
        }
      }
      return true;
    };
    if (constructor_args.size() == 0) {
      return add_constructor_keywords_to_dict();
    }
    const Value& source = constructor_args.get(0);
    if (auto* dict = value_as_dict(source)) {
      for (const auto& entry : dict->entries) {
        if (!mapping_set_item(out, entry.first, entry.second, error)) {
          return false;
        }
      }
      return add_constructor_keywords_to_dict();
    }
    if (auto* instance = value_as_instance(source)) {
      if (auto* storage = value_as_dict(instance->mapping_storage)) {
        for (const auto& entry : storage->entries) {
          if (!mapping_set_item(out, entry.first, entry.second, error)) {
            return false;
          }
        }
        return add_constructor_keywords_to_dict();
      }
    }
    if (mapping_is_mapping(source)) {
      Value iterator;
      if (!mapping_get_iter(source, iterator, error)) {
        return false;
      }
      for (;;) {
        bool done = false;
        Value key;
        if (!mapping_iter_next(iterator, done, key, error)) {
          return false;
        }
        if (done) {
          break;
        }
        Value value;
        if (!mapping_get_item(source, key, value, error) ||
            !mapping_set_item(out, key, value, error)) {
          return false;
        }
      }
      return add_constructor_keywords_to_dict();
    }
    Value iterator;
    if (!runtime_get_iter(runtime, source, iterator, error)) {
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
    return add_constructor_keywords_to_dict();
  }

  if (constructor == XlangVMBuiltinConstructor::Bytes) {
    if (!reject_constructor_keywords_except({"encoding", "errors"})) return false;
    const Value* encoding = find_constructor_keyword("encoding");
    const Value* errors_value = find_constructor_keyword("errors");
    if (!require_constructor_string_keyword(encoding, "encoding") ||
        !require_constructor_string_keyword(errors_value, "errors")) {
      return false;
    }
    if (constructor_args.size() > 3) {
      error = "bytes() expected at most 3 arguments";
      return false;
    }
    if (constructor_args.size() == 0) {
      if (exact_builtin_constructor) {
        out = Value::bytes("");
        return true;
      }
      Value klass_value;
      klass_value.tag = ValueTag::Object;
      klass_value.as.obj = const_cast<Object*>(&klass.header);
      retain(klass_value);
      out = Value::instance(std::move(klass_value));
      std::string attr_error;
      if (!object_set_attr(out, "__xlang3_bytes_value__", Value::bytes(""), attr_error)) {
        error = "bytes subclass construction failed";
        return false;
      }
      return true;
    }
    if (constructor_args.size() >= 2) {
      encoding = &constructor_args.get(1);
      if (!require_constructor_string_keyword(encoding, "encoding")) return false;
    }
    if (constructor_args.size() >= 3) {
      errors_value = &constructor_args.get(2);
      if (!require_constructor_string_keyword(errors_value, "errors")) return false;
    }
    std::string bytes;
    if (auto* string = value_as_string(constructor_args.get(0))) {
      if (encoding == nullptr || encoding->tag == ValueTag::None) {
        error = "string argument without an encoding";
        return false;
      }
      bytes = string_object_to_string(*string);
    } else if (!make_bytes_from_arg(constructor_args.get(0), bytes, error)) {
      return false;
    }
    if (exact_builtin_constructor) {
      out = Value::bytes(std::move(bytes));
      return true;
    }
    Value klass_value;
    klass_value.tag = ValueTag::Object;
    klass_value.as.obj = const_cast<Object*>(&klass.header);
    retain(klass_value);
    out = Value::instance(std::move(klass_value));
    std::string attr_error;
    if (!object_set_attr(out, "__xlang3_bytes_value__", Value::bytes(std::move(bytes)), attr_error)) {
      error = "bytes subclass construction failed";
      return false;
    }
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::ByteArray) {
    if (!reject_constructor_keywords_except({"encoding", "errors"})) return false;
    const Value* encoding = find_constructor_keyword("encoding");
    const Value* errors_value = find_constructor_keyword("errors");
    if (!require_constructor_string_keyword(encoding, "encoding") ||
        !require_constructor_string_keyword(errors_value, "errors")) {
      return false;
    }
    if (constructor_args.size() > 3) {
      error = "bytearray() expected at most 3 arguments";
      return false;
    }
    if (constructor_args.size() == 0) {
      out = Value::bytearray("");
      return true;
    }
    if (constructor_args.size() >= 2) {
      encoding = &constructor_args.get(1);
      if (!require_constructor_string_keyword(encoding, "encoding")) return false;
    }
    if (constructor_args.size() >= 3) {
      errors_value = &constructor_args.get(2);
      if (!require_constructor_string_keyword(errors_value, "errors")) return false;
    }
    std::string bytes;
    if (auto* string = value_as_string(constructor_args.get(0))) {
      if (encoding == nullptr || encoding->tag == ValueTag::None) {
        error = "string argument without an encoding";
        return false;
      }
      bytes = string_object_to_string(*string);
    } else if (!make_bytes_from_arg(constructor_args.get(0), bytes, error)) {
      return false;
    }
    out = Value::bytearray(std::move(bytes));
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::MemoryView) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() != 1) {
      error = "memoryview() expected 1 argument";
      return false;
    }
    const Value& source = constructor_args.get(0);
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
    if (!reject_constructor_keywords_except({"fget", "fset", "fdel", "doc"})) return false;
    if (constructor_args.size() > 4) {
      error = "property() expected at most 4 arguments";
      return false;
    }
    const char* property_names[] = {"fget", "fset", "fdel", "doc"};
    for (size_t i = 0; i < constructor_args.size() && i < std::size(property_names); ++i) {
      if (find_constructor_keyword(property_names[i]) != nullptr) {
        error = std::string("property() got multiple values for argument '") + property_names[i] + "'";
        return false;
      }
    }
    auto property_arg = [&](size_t index, const char* name) -> Value {
      if (const Value* keyword = find_constructor_keyword(name)) {
        return *keyword;
      }
      return constructor_args.size() > index ? constructor_args.get(index) : Value::none();
    };
    out = Value::property(
        property_arg(0, "fget"),
        property_arg(1, "fset"),
        property_arg(2, "fdel"),
        property_arg(3, "doc"));
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::ClassMethod ||
      constructor == XlangVMBuiltinConstructor::StaticMethod) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() != 1) {
      error = klass.name + "() expected 1 argument";
      return false;
    }
    const Value& function = constructor_args.get(0);
    if (value_as_function(function) == nullptr &&
        value_as_native_function(function) == nullptr &&
        value_as_bound_method(function) == nullptr &&
        value_as_class(function) == nullptr) {
      error = klass.name + "() argument must be callable";
      return false;
    }
    out = constructor == XlangVMBuiltinConstructor::ClassMethod
        ? Value::class_method(function)
        : Value::static_method(function);
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Super) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() != 0 && constructor_args.size() != 2) {
      error = "super() expected 0 or 2 arguments";
      return false;
    }
    Value klass;
    Value self;
    if (constructor_args.size() == 2) {
      if (value_as_class(constructor_args.get(0)) == nullptr) {
        error = "super() first argument must be type";
        return false;
      }
      value_assign_fast(klass, constructor_args.get(0));
      value_assign_fast(self, constructor_args.get(1));
    } else {
      Value locals = runtime.current_locals_snapshot();
      if (!xlang_vm_current_super_first_argument(runtime, locals, self)) {
        error = "super(): no current instance or class";
        return false;
      }
      if (!xlang_vm_infer_super_defining_class(runtime, self, klass, error)) {
        return false;
      }
    }
    out = Value::super_object(std::move(klass), std::move(self));
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Module) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() < 1 || constructor_args.size() > 2) {
      error = "module() expected name and optional doc";
      return false;
    }
    auto* name = value_as_string(constructor_args.get(0));
    if (name == nullptr) {
      error = "module name must be a string";
      return false;
    }
    out = Value::module(string_object_to_string(*name));
    std::string ignored;
    module_set_attr(out, "__doc__", constructor_args.size() == 2 ? constructor_args.get(1) : Value::none(), ignored);
    return true;
  }

  if (constructor == XlangVMBuiltinConstructor::Method) {
    if (!reject_constructor_keywords()) return false;
    if (constructor_args.size() != 2) {
      error = "method() expected function and instance";
      return false;
    }
    out = Value::bound_method(constructor_args.get(1), constructor_args.get(0));
    return true;
  }

  return false;
}



} // namespace xlang3
