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
#include "../xlang_vm_op_switch.h"

#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE XlangVMOpFlow make_class(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result) {
  if (in.a >= fn.names.size() || in.b >= fn.class_attrs.size() || in.c >= fn.class_instance_slots.size()) {
    result.errors.push_back("invalid class data");
    return XlangVMOpFlow::ReturnResult;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.reserve(fn.class_attrs[in.b].size());
  for (const auto& attr : fn.class_attrs[in.b]) {
    if (attr.second >= regs.size()) {
      result.errors.push_back("invalid class attr register");
      return XlangVMOpFlow::ReturnResult;
    }
    attrs.push_back(std::make_pair(attr.first, regs[attr.second]));
  }
  Value base = Value::invalid();
  if (const auto* object_base = runtime.find_builtin(XlangVMNames::object_type)) {
    value_assign_fast(base, *object_base);
  }
  regs[in.dst] = Value::class_object(fn.names[in.a], std::move(attrs), std::move(base), fn.class_instance_slots[in.c]);
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

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow set_class_base(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    RaiseRuntimeError&& raise_runtime_error) {
  std::string error;
  if (!class_set_base(regs[in.dst], regs[in.a], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
