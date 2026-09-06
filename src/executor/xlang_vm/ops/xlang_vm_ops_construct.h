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

#include "../xlang_vm_names.h"
#include "../xlang_vm_inline_support.h"
#include "../xlang_vm_op_switch.h"

#include "xlang3/object_model.h"
#include "xlang3/module_object.h"
#include "xlang3/runtime.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE bool xlang_vm_class_attrs_have(
    const std::vector<std::pair<std::string, Value>>& attrs,
    const std::string& name) {
  for (const auto& attr : attrs) {
    if (attr.first == name) {
      return true;
    }
  }
  return false;
}

XLANG3_HOT_INLINE std::string xlang_vm_current_module_name(const Value& globals_module) {
  Value name_value;
  std::string ignored;
  if (module_get_attr(globals_module, "__name__", name_value, ignored)) {
    if (auto* name = value_as_string(name_value)) {
      return string_object_to_string(*name);
    }
  }
  return "__main__";
}

XLANG3_HOT_INLINE bool xlang_vm_class_slot_conflicts(
    const std::vector<std::pair<std::string, uint32_t>>& attrs,
    const std::vector<std::string>& slots,
    std::string& error) {
  bool has_explicit_slots = false;
  for (const auto& attr : attrs) {
    if (attr.first == "__slots__") {
      has_explicit_slots = true;
      break;
    }
  }
  if (!has_explicit_slots) {
    return false;
  }
  for (const auto& slot : slots) {
    if (slot == "__dict__" || slot == "__weakref__") {
      continue;
    }
    for (const auto& attr : attrs) {
      if (attr.first == slot && attr.first != "__doc__") {
        error = "'" + slot + "' in __slots__ conflicts with class variable";
        return true;
      }
    }
  }
  return false;
}

XLANG3_HOT_INLINE XlangVMOpFlow make_class(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    const Value& globals_module,
    RuntimeResult& result) {
  if (in.a >= fn.names.size() || in.b >= fn.class_attrs.size() || in.c >= fn.class_instance_slots.size()) {
    result.errors.push_back("invalid class data");
    return XlangVMOpFlow::ReturnResult;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.reserve(fn.class_attrs[in.b].size());
  std::vector<std::string> attr_order;
  attr_order.reserve(fn.class_attrs[in.b].size());
  std::string slot_error;
  if (xlang_vm_class_slot_conflicts(fn.class_attrs[in.b], fn.class_instance_slots[in.c], slot_error)) {
    result.errors.push_back(slot_error);
    return XlangVMOpFlow::ReturnResult;
  }
  for (const auto& attr : fn.class_attrs[in.b]) {
    if (attr.second >= regs.size()) {
      result.errors.push_back("invalid class attr register");
      return XlangVMOpFlow::ReturnResult;
    }
    attr_order.push_back(attr.first);
    attrs.push_back(std::make_pair(attr.first, regs[attr.second]));
  }
  const std::string class_name = fn.names[in.a];
  if (!xlang_vm_class_attrs_have(attrs, "__module__")) {
    attrs.push_back({"__module__", Value::string(xlang_vm_current_module_name(globals_module))});
  }
  if (!xlang_vm_class_attrs_have(attrs, "__qualname__")) {
    attrs.push_back({"__qualname__", Value::string(class_name)});
  }
  std::vector<std::pair<std::string, Value>> set_name_descriptors;
  xlang_vm_collect_set_name_descriptors(attrs, set_name_descriptors);
  Value base = Value::invalid();
  if (const auto* object_base = runtime.find_builtin(XlangVMNames::object_type)) {
    value_assign_fast(base, *object_base);
  }
  Value metaclass = Value::invalid();
  if (const auto* type_type = runtime.find_builtin(XlangVMNames::builtin_type)) {
    value_assign_fast(metaclass, *type_type);
  }
  regs[in.dst] = Value::class_object(
      class_name,
      std::move(attrs),
      std::move(base),
      fn.class_instance_slots[in.c],
      std::move(metaclass));
  std::string set_name_error;
  if (!xlang_vm_call_set_name_descriptors(runtime, regs[in.dst], set_name_descriptors, set_name_error)) {
    result.errors.push_back(set_name_error);
    return XlangVMOpFlow::ReturnResult;
  }
  if (auto* klass = value_as_class(regs[in.dst])) {
    klass->definition_attr_order = std::move(attr_order);
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow make_function(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    const std::shared_ptr<const ir::Module>& module_owner,
    RuntimeResult& result) {
  if (in.b >= fn.function_closures.size()) {
    result.errors.push_back("invalid function closure list");
    return XlangVMOpFlow::ReturnResult;
  }
  std::vector<Value> closure;
  closure.reserve(fn.function_closures[in.b].size());
  for (const auto reg : fn.function_closures[in.b]) {
    if (reg >= regs.size()) {
      result.errors.push_back("invalid closure register");
      return XlangVMOpFlow::ReturnResult;
    }
    closure.push_back(regs[reg]);
  }
  std::vector<Value> defaults;
  if (in.c != UINT32_MAX) {
    if (in.c >= fn.function_defaults.size()) {
      result.errors.push_back("invalid function defaults list");
      return XlangVMOpFlow::ReturnResult;
    }
    defaults.reserve(fn.function_defaults[in.c].size());
    for (const auto reg : fn.function_defaults[in.c]) {
      if (reg >= regs.size()) {
        result.errors.push_back("invalid default register");
        return XlangVMOpFlow::ReturnResult;
      }
      defaults.push_back(regs[reg]);
    }
  }
  regs[in.dst] = Value::function(in.a, std::move(closure), globals_module, module_owner, std::move(defaults));
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow set_function_annotations(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result) {
  if (in.a >= fn.function_annotations.size()) {
    result.errors.push_back("invalid function annotations list");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* function = value_as_function(regs[in.dst]);
  if (function == nullptr) {
    result.errors.push_back("invalid function annotations target");
    return XlangVMOpFlow::ReturnResult;
  }
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(fn.function_annotations[in.a].size());
  for (const auto& annotation : fn.function_annotations[in.a]) {
    if (annotation.second >= regs.size()) {
      result.errors.push_back("invalid annotation register");
      return XlangVMOpFlow::ReturnResult;
    }
    entries.push_back(std::make_pair(Value::string(annotation.first), regs[annotation.second]));
  }
  value_assign_fast(function->annotations, Value::dict(std::move(entries)));
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow set_function_kwdefaults(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result) {
  if (in.a >= fn.function_kwdefaults.size()) {
    result.errors.push_back("invalid function keyword defaults list");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* function = value_as_function(regs[in.dst]);
  if (function == nullptr) {
    result.errors.push_back("invalid function keyword defaults target");
    return XlangVMOpFlow::ReturnResult;
  }
  function->kwdefaults.clear();
  function->kwdefaults.reserve(fn.function_kwdefaults[in.a].size());
  for (const auto& item : fn.function_kwdefaults[in.a]) {
    if (item.second >= regs.size()) {
      result.errors.push_back("invalid keyword default register");
      return XlangVMOpFlow::ReturnResult;
    }
    function->kwdefaults.push_back({item.first, regs[item.second]});
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow set_class_base(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error) {
  std::string error;
  if (!class_set_base(regs[in.dst], regs[in.a], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  Value hook;
  if (object_lookup_class_attr(regs[in.a], "__init_subclass__", hook, error)) {
    Value ignored;
    bool ok = true;
    if (auto* method = value_as_class_method(hook)) {
      Value call_args[] = {regs[in.dst]};
      ok = runtime_call_callable(runtime, method->function, call_args, 1, ignored, error);
    } else if (auto* static_method = value_as_static_method(hook)) {
      ok = runtime_call_callable(runtime, static_method->function, nullptr, 0, ignored, error);
    } else if (value_as_function(hook) != nullptr || value_as_native_function(hook) != nullptr) {
      Value call_args[] = {regs[in.dst]};
      ok = runtime_call_callable(runtime, hook, call_args, 1, ignored, error);
    } else {
      ok = runtime_call_callable(runtime, hook, nullptr, 0, ignored, error);
    }
    if (!ok) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  } else {
    error.clear();
  }
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
