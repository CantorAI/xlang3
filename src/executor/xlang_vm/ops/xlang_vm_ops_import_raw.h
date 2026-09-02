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

#include "../xlang_vm_op_switch.h"

#include "../../../ipc/ipc_runtime.h"

#include "xlang3/module_object.h"
#include "xlang3/runtime.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE std::string resolve_relative_import_name(const std::string& name, Value& globals_module) {
  if (name.empty() || name.front() != '.') {
    return name;
  }
  std::string package;
  std::string ignored;
  Value package_value;
  if (module_get_attr(globals_module, "__package__", package_value, ignored)) {
    if (auto* string = value_as_string(package_value)) {
      package = string_object_to_string(*string);
    }
  }
  size_t dots = 0;
  while (dots < name.size() && name[dots] == '.') {
    ++dots;
  }
  for (size_t i = 1; i < dots && !package.empty(); ++i) {
    const auto cut = package.rfind('.');
    package = cut == std::string::npos ? std::string() : package.substr(0, cut);
  }
  const std::string tail = name.substr(dots);
  if (tail.empty()) {
    return package;
  }
  return package.empty() ? tail : package + "." + tail;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow import_module(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.a >= fn.names.size()) {
    result.errors.push_back("invalid module name");
    return XlangVMOpFlow::ReturnResult;
  }
  std::string error;
  if (!runtime.import_module(fn.names[in.a], regs[in.dst], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow import_module_thru(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.a >= fn.names.size()) {
    result.errors.push_back("invalid thru import module name");
    return XlangVMOpFlow::ReturnResult;
  }
  std::string error;
  if (!ipc_import_thru(runtime, fn.names[in.a], regs[in.b], regs[in.dst], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow import_from(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.a >= fn.names.size() || in.b >= fn.names.size()) {
    result.errors.push_back("invalid from import");
    return XlangVMOpFlow::ReturnResult;
  }
  const std::string module_name = resolve_relative_import_name(fn.names[in.a], globals_module);
  std::string error;
  if (!runtime.import_from(module_name, fn.names[in.b], regs[in.dst], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow import_star(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    Value& globals_module,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.dst >= fn.names.size()) {
    result.errors.push_back("invalid star import module name");
    return XlangVMOpFlow::ReturnResult;
  }
  std::string error;
  const std::string module_name = resolve_relative_import_name(fn.names[in.dst], globals_module);
  if (!runtime.import_star(module_name, globals_module, error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow raw_block(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallValueBuffer& locals,
    const std::vector<Value>& fn_obj_closure,
    Value& globals_module,
    std::unordered_map<std::string, Value>& globals,
    uint64_t& globals_version,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.dst >= fn.raw_blocks.size()) {
    result.errors.push_back("invalid raw block index");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& raw = fn.raw_blocks[in.dst];
  RawBlockContext context;
  context.get_var = [&](const std::string& name, Value& out, std::string& error) -> bool {
    for (size_t i = 0; i < fn.locals.size(); ++i) {
      if (fn.locals[i] == name && i < locals.size()) {
        value_assign_fast(out, locals[i]);
        return true;
      }
    }
    for (size_t i = 0; i < fn.free_vars.size(); ++i) {
      if (fn.free_vars[i] == name && i < fn_obj_closure.size()) {
        auto* cell = value_as_cell(fn_obj_closure[i]);
        if (cell != nullptr) {
          value_assign_fast(out, cell->value);
          return true;
        }
      }
    }
    if (value_as_module(globals_module) != nullptr) {
      if (module_get_attr(globals_module, name, out, error)) {
        return true;
      }
    } else if (auto it = globals.find(name); it != globals.end()) {
      value_assign_fast(out, it->second);
      return true;
    }
    if (const auto* builtin = runtime.find_builtin(name)) {
      value_assign_fast(out, *builtin);
      return true;
    }
    error = "name '" + name + "' is not defined";
    return false;
  };
  context.set_var = [&](const std::string& name, const Value& value, std::string& error) -> bool {
    for (size_t i = 0; i < fn.locals.size(); ++i) {
      if (fn.locals[i] == name && i < locals.size()) {
        value_assign_fast(locals[i], value);
        return true;
      }
    }
    if (value_as_module(globals_module) != nullptr) {
      return module_set_attr(globals_module, name, value, error);
    }
    value_assign_fast(globals[name], value);
    ++globals_version;
    return true;
  };
  std::string error;
  if (!runtime.execute_raw_block(context, raw.language, raw.provider, raw.body, error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
