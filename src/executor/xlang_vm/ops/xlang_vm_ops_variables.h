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

#include "../xlang_frame.h"
#include "../xlang_vm_arithmetic.h"
#include "../xlang_vm_op_switch.h"

#include "xlang3/module_object.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE void move(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs) {
  value_assign_fast(regs[in.dst], regs[in.a]);
}

XLANG3_HOT_INLINE void store_local(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    const std::vector<size_t>& register_last_use,
    size_t ip) {
  if (in.a < register_last_use.size() && register_last_use[in.a] == ip) {
    xlang_perf_count_store_local(true);
    value_move_assign_fast(locals[in.dst], regs[in.a]);
    return;
  }
  xlang_perf_count_store_local(false);
  value_assign_fast(locals[in.dst], regs[in.a]);
}

XLANG3_HOT_INLINE void move_local(
    const ir::Instr& in,
    XlangVMSmallValueBuffer& locals) {
  value_assign_fast(locals[in.dst], locals[in.a]);
}

XLANG3_HOT_INLINE void load_cell_object(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& cells) {
  value_assign_fast(regs[in.dst], cells[in.a]);
}

XLANG3_HOT_INLINE void load_free_object(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    const std::vector<Value>& fn_obj_closure) {
  value_assign_fast(regs[in.dst], fn_obj_closure[in.a]);
}

XLANG3_HOT_INLINE void delete_local(
    const ir::Instr& in,
    XlangVMSmallValueBuffer& locals) {
  value_set_invalid(locals[in.dst]);
}

XLANG3_HOT_INLINE void delete_global(
    const ir::Instr& in,
    const ir::Function& fn,
    std::unordered_map<std::string, Value>& globals,
    uint64_t& globals_version) {
  globals.erase(fn.names[in.dst]);
  ++globals_version;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow load_module_slot(
    const ir::Instr& in,
    const ir::Module& module,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    std::unordered_map<std::string, Value>& globals,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.a >= module.global_slots.size()) {
    result.errors.push_back("invalid module slot");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& name = module.global_slots[in.a];
  auto* globals_module_obj = value_as_module(globals_module);
  if (globals_module_obj != nullptr) {
    if (in.a < globals_module_obj->slots.size() && globals_module_obj->slots[in.a].tag != ValueTag::Invalid) {
      value_assign_fast(regs[in.dst], globals_module_obj->slots[in.a]);
      return XlangVMOpFlow::Next;
    }
    std::string slot_error;
    uint32_t dynamic_slot = 0;
    if (module_find_attr_slot(globals_module, name, dynamic_slot, slot_error) &&
        dynamic_slot < globals_module_obj->slots.size() &&
        globals_module_obj->slots[dynamic_slot].tag != ValueTag::Invalid) {
      value_assign_fast(regs[in.dst], globals_module_obj->slots[dynamic_slot]);
      return XlangVMOpFlow::Next;
    }
    if (const auto* builtin = runtime.find_builtin(name)) {
      value_assign_fast(regs[in.dst], *builtin);
      return XlangVMOpFlow::Next;
    }
    return raise_runtime_error("name '" + name + "' is not defined") ? XlangVMOpFlow::ContinueLoop
                                                                    : XlangVMOpFlow::ReturnResult;
  }
  if (auto it = globals.find(name); it != globals.end()) {
    value_assign_fast(regs[in.dst], it->second);
  } else if (const auto* builtin = runtime.find_builtin(name)) {
    value_assign_fast(regs[in.dst], *builtin);
  } else {
    return raise_runtime_error("name '" + name + "' is not defined") ? XlangVMOpFlow::ContinueLoop
                                                                    : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow store_module_slot(
    const ir::Instr& in,
    const ir::Module& module,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    std::unordered_map<std::string, Value>& globals,
    uint64_t& globals_version,
    RuntimeResult& result) {
  if (in.dst >= module.global_slots.size() || in.a >= regs.size()) {
    result.errors.push_back("invalid module slot store");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* globals_module_obj = value_as_module(globals_module);
  if (globals_module_obj != nullptr) {
    if (in.dst >= globals_module_obj->slots.size()) {
      result.errors.push_back("module slot is not bound");
      return XlangVMOpFlow::ReturnResult;
    }
    value_assign_fast(globals_module_obj->slots[in.dst], regs[in.a]);
    ++globals_module_obj->version;
  } else {
    value_assign_fast(globals[module.global_slots[in.dst]], regs[in.a]);
    ++globals_version;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow load_global(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    std::unordered_map<std::string, Value>& globals,
    uint64_t globals_version,
    XlangVMInstrCache& instr_cache,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  xlang_vm_cache_touch(instr_cache, XlangVMCacheDomain::Global);
  if (in.a >= fn.names.size()) {
    result.errors.push_back("invalid global name");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* globals_module_obj = value_as_module(globals_module);
  const uint64_t current_globals_version = globals_module_obj != nullptr ? globals_module_obj->version : globals_version;
  auto& global_cache = instr_cache.global;
  if (global_cache.kind != 0) {
    if (globals_module_obj != nullptr && global_cache.kind == 1) {
      const auto slot = global_cache.slot;
      if (global_cache.version == current_globals_version &&
          slot < globals_module_obj->slots.size() &&
          globals_module_obj->slots[slot].tag != ValueTag::Invalid) {
        value_assign_fast(regs[in.dst], globals_module_obj->slots[slot]);
        return XlangVMOpFlow::Next;
      }
    } else if (global_cache.kind == 2 && global_cache.version == current_globals_version) {
      value_assign_fast(regs[in.dst], global_cache.value);
      return XlangVMOpFlow::Next;
    }
  }
  const auto& name = fn.names[in.a];
  if (globals_module_obj != nullptr) {
    std::string error;
    uint32_t slot = 0;
    if (module_find_attr_slot(globals_module, name, slot, error) &&
        slot < globals_module_obj->slots.size() &&
        globals_module_obj->slots[slot].tag != ValueTag::Invalid) {
      value_assign_fast(regs[in.dst], globals_module_obj->slots[slot]);
      global_cache.slot = slot;
      global_cache.version = globals_module_obj->version;
      global_cache.kind = 1;
      return XlangVMOpFlow::Next;
    }
    Value current_globals = runtime.current_globals_module();
    auto* current_module = value_as_module(current_globals);
    if (current_module != nullptr &&
        current_module != globals_module_obj &&
        module_find_attr_slot(current_globals, name, slot, error) &&
        slot < current_module->slots.size() &&
        current_module->slots[slot].tag != ValueTag::Invalid) {
      value_assign_fast(regs[in.dst], current_module->slots[slot]);
      return XlangVMOpFlow::Next;
    }
    if (const auto* builtin = runtime.find_builtin(name)) {
      value_assign_fast(regs[in.dst], *builtin);
      value_assign_fast(global_cache.value, regs[in.dst]);
      global_cache.version = globals_module_obj->version;
      global_cache.kind = 2;
    } else {
      return raise_runtime_error("name '" + name + "' is not defined") ? XlangVMOpFlow::ContinueLoop
                                                                      : XlangVMOpFlow::ReturnResult;
    }
  } else if (auto it = globals.find(name); it != globals.end()) {
    value_assign_fast(regs[in.dst], it->second);
    value_assign_fast(global_cache.value, regs[in.dst]);
    global_cache.version = globals_version;
    global_cache.kind = 2;
  } else if (const auto* builtin = runtime.find_builtin(name)) {
    value_assign_fast(regs[in.dst], *builtin);
    value_assign_fast(global_cache.value, regs[in.dst]);
    global_cache.version = globals_version;
    global_cache.kind = 2;
  } else {
    return raise_runtime_error("name '" + name + "' is not defined") ? XlangVMOpFlow::ContinueLoop
                                                                    : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow store_global(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    std::unordered_map<std::string, Value>& globals,
    uint64_t& globals_version,
    XlangVMInstrCache& instr_cache,
    RuntimeResult& result) {
  xlang_vm_cache_touch(instr_cache, XlangVMCacheDomain::Global);
  if (in.dst >= fn.names.size() || in.a >= regs.size()) {
    result.errors.push_back("invalid global store");
    return XlangVMOpFlow::ReturnResult;
  }
  if (value_as_module(globals_module) != nullptr) {
    std::string error;
    if (!module_set_attr(globals_module, fn.names[in.dst], regs[in.a], error)) {
      result.errors.push_back(error);
      return XlangVMOpFlow::ReturnResult;
    }
  } else {
    value_assign_fast(globals[fn.names[in.dst]], regs[in.a]);
    ++globals_version;
  }
  if (auto* globals_module_obj = value_as_module(globals_module)) {
    std::string error;
    uint32_t slot = 0;
    if (module_find_attr_slot(globals_module, fn.names[in.dst], slot, error)) {
      instr_cache.global.slot = slot;
      instr_cache.global.kind = 1;
    }
    instr_cache.global.version = globals_module_obj->version;
  } else {
    value_assign_fast(instr_cache.global.value, regs[in.a]);
    instr_cache.global.version = globals_version;
    instr_cache.global.kind = 2;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow delete_module_slot(
    const ir::Instr& in,
    const ir::Module& module,
    Value& globals_module,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.dst >= module.global_slots.size()) {
    result.errors.push_back("invalid module slot delete");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* globals_module_obj = value_as_module(globals_module);
  if (globals_module_obj == nullptr || in.dst >= globals_module_obj->slots.size()) {
    return raise_runtime_error("module slot is not bound") ? XlangVMOpFlow::ContinueLoop
                                                          : XlangVMOpFlow::ReturnResult;
  }
  value_set_invalid(globals_module_obj->slots[in.dst]);
  ++globals_module_obj->version;
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow load_const(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result,
    RaiseRuntimeError&&) {
  if (in.a >= fn.constants.size()) {
    result.errors.push_back("invalid constant index");
    return XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.dst], fn.constants[in.a]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow load_local(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.a >= locals.size()) {
    result.errors.push_back("invalid local slot");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid) {
    return raise_runtime_error("local variable is not defined")
               ? XlangVMOpFlow::ContinueLoop
               : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.dst], locals[in.a]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow add_local_const(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= fn.constants.size()) {
    result.errors.push_back("invalid local const add");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& lhs = locals[in.a];
  const auto& rhs = fn.constants[in.b];
  if (!fast_add(lhs, rhs, locals[in.dst])) {
    std::string error;
    if (!value_add(lhs, rhs, locals[in.dst], error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow add_local_local(
    const ir::Instr& in,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= locals.size()) {
    result.errors.push_back("invalid local local add");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& lhs = locals[in.a];
  const auto& rhs = locals[in.b];
  if (!fast_add(lhs, rhs, locals[in.dst])) {
    std::string error;
    if (!value_add(lhs, rhs, locals[in.dst], error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow load_cell(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& cells,
    RuntimeResult& result,
    RaiseRuntimeError&&) {
  if (in.a >= cells.size()) {
    result.errors.push_back("invalid cell slot");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* cell = value_as_cell(cells[in.a]);
  if (cell == nullptr) {
    result.errors.push_back("invalid cell object");
    return XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(regs[in.dst], cell->value);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow store_cell(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    XlangVMSmallValueBuffer& cells,
    RuntimeResult& result,
    RaiseRuntimeError&&) {
  if (in.dst >= cells.size() || in.a >= regs.size()) {
    result.errors.push_back("invalid cell store");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* cell = value_as_cell(cells[in.dst]);
  if (cell == nullptr) {
    result.errors.push_back("invalid cell object");
    return XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(cell->value, regs[in.a]);
  value_assign_fast(locals[fn.cell_slots[in.dst]], regs[in.a]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow load_free(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    const std::vector<Value>& fn_obj_closure,
    RuntimeResult& result,
    RaiseRuntimeError&&) {
  if (in.a >= fn_obj_closure.size()) {
    const std::string message =
        "invalid free slot in " + std::string(fn.name.empty() ? "<function>" : fn.name) +
        ": requested " + std::to_string(in.a) +
        ", closure size " + std::to_string(fn_obj_closure.size());
    result.errors.push_back(message);
    return XlangVMOpFlow::ReturnResult;
  }
  auto* cell = value_as_cell(fn_obj_closure[in.a]);
  if (cell == nullptr) {
    result.errors.push_back("invalid free cell");
    return XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(regs[in.dst], cell->value);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow store_free(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    const std::vector<Value>& fn_obj_closure,
    RuntimeResult& result,
    RaiseRuntimeError&&) {
  if (in.dst >= fn_obj_closure.size() || in.a >= regs.size()) {
    result.errors.push_back("invalid free store");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* cell = value_as_cell(fn_obj_closure[in.dst]);
  if (cell == nullptr) {
    result.errors.push_back("invalid free cell");
    return XlangVMOpFlow::ReturnResult;
  }
  value_assign_fast(cell->value, regs[in.a]);
  return XlangVMOpFlow::Next;
}

} // namespace xlang3::xlang_vm::ops
