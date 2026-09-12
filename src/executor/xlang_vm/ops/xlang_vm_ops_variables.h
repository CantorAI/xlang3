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
#include "xlang3/expression.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE bool load_mapping_global(
    Runtime& runtime,
    const Value& globals_mapping,
    const std::string& name,
    Value& out,
    bool& found,
    std::string& error) {
  found = false;
  if (!mapping_is_mapping(globals_mapping)) {
    return true;
  }
  const Value key = Value::string(name);
  if (mapping_get_item(globals_mapping, key, out, error)) {
    found = true;
    return true;
  }
  if (value_as_instance(globals_mapping) == nullptr) {
    error.clear();
    return true;
  }
  Value missing;
  std::string missing_error;
  if (!object_get_attr(globals_mapping, "__missing__", missing, missing_error)) {
    error.clear();
    return true;
  }
  if (!runtime_call_callable(runtime, missing, &key, 1, out, error)) {
    return false;
  }
  found = true;
  return true;
}

XLANG3_HOT_INLINE void capture_expressions(const ir::Instr& in, XlangVMSmallRegisterBuffer& regs) {
  value_assign_fast(regs[in.dst], Value::boolean(expression_capture_enabled(regs[in.a])));
}

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
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    std::vector<Value>& native_call_args,
    const std::vector<size_t>& register_last_use,
    const std::vector<bool>& register_loop_carried,
    size_t ip) {
  const Value& local = locals[in.dst];
  Value deleted_value;
  if (local.tag == ValueTag::Object && local.as.obj != nullptr) {
    deleted_value = local;
    const auto* deleted_list = value_as_list_storage(local);
    auto is_deleted_list_item = [&](const Value& candidate) {
      if (deleted_list == nullptr || candidate.tag != ValueTag::Object ||
          candidate.as.obj == nullptr) {
        return false;
      }
      for (const auto& item : deleted_list->items) {
        if (item.tag == ValueTag::Object && item.as.obj == candidate.as.obj) {
          return true;
        }
      }
      return false;
    };
    for (size_t i = 0; i < regs.size(); ++i) {
      auto& reg = regs[i];
      if ((reg.tag == ValueTag::Object && reg.as.obj == local.as.obj) ||
          is_deleted_list_item(reg)) {
        value_set_invalid(reg);
      }
    }
    for (auto& arg : native_call_args) {
      if (arg.tag == ValueTag::Object && arg.as.obj == local.as.obj) {
        value_set_invalid(arg);
      }
    }
    native_call_args.clear();
  }
  for (size_t i = 0; i < regs.size() && i < register_last_use.size(); ++i) {
    const bool loop_carried = i < register_loop_carried.size() &&
        register_loop_carried[i];
    if (!loop_carried) {
      value_set_invalid(regs[i]);
    }
  }
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

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow load_module_slot(
    const ir::Instr& in,
    const ir::Module& module,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    std::unordered_map<std::string, Value>& globals,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.a >= module.global_slots.size()) {
    result.errors.push_back("invalid module slot");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& name = module.global_slots[in.a];
  auto* globals_module_obj = value_as_module(globals_module);
  if (globals_module_obj != nullptr) {
    const auto bound = globals_module_obj->name_to_slot.find(name);
    if (bound != globals_module_obj->name_to_slot.end() && bound->second == in.a &&
        in.a < globals_module_obj->slots.size() && globals_module_obj->slots[in.a].tag != ValueTag::Invalid) {
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
  bool mapping_found = false;
  std::string mapping_error;
  if (!load_mapping_global(runtime, globals_module, name, regs[in.dst], mapping_found, mapping_error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                       : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(mapping_error) ? XlangVMOpFlow::ContinueLoop
                                              : XlangVMOpFlow::ReturnResult;
  }
  if (mapping_found) {
    return XlangVMOpFlow::Next;
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
    std::string error;
    if (!module_set_attr(globals_module, module.global_slots[in.dst], regs[in.a], error)) {
      result.errors.push_back(error);
      return XlangVMOpFlow::ReturnResult;
    }
  } else {
    value_assign_fast(globals[module.global_slots[in.dst]], regs[in.a]);
    ++globals_version;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
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
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
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
    if (const auto* builtin = runtime.find_builtin(name)) {
      value_assign_fast(regs[in.dst], *builtin);
      value_assign_fast(global_cache.value, regs[in.dst]);
      global_cache.version = globals_module_obj->version;
      global_cache.kind = 2;
    } else {
      return raise_runtime_error("name '" + name + "' is not defined") ? XlangVMOpFlow::ContinueLoop
                                                                      : XlangVMOpFlow::ReturnResult;
    }
  } else {
    bool mapping_found = false;
    std::string mapping_error;
    if (!load_mapping_global(runtime, globals_module, name, regs[in.dst], mapping_found, mapping_error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop
                                                         : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(mapping_error) ? XlangVMOpFlow::ContinueLoop
                                                : XlangVMOpFlow::ReturnResult;
    }
    if (mapping_found) {
      value_assign_fast(global_cache.value, regs[in.dst]);
      global_cache.version = globals_version;
      global_cache.kind = 2;
      return XlangVMOpFlow::Next;
    }
    if (auto it = globals.find(name); it != globals.end()) {
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
  if (globals_module_obj == nullptr) {
    return raise_runtime_error("module slot is not bound") ? XlangVMOpFlow::ContinueLoop
                                                          : XlangVMOpFlow::ReturnResult;
  }
  uint32_t slot = 0;
  std::string error;
  if (!module_find_attr_slot(globals_module, module.global_slots[in.dst], slot, error) ||
      slot >= globals_module_obj->slots.size()) {
    return raise_runtime_error("module slot is not bound") ? XlangVMOpFlow::ContinueLoop
                                                          : XlangVMOpFlow::ReturnResult;
  }
  value_set_invalid(globals_module_obj->slots[slot]);
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

template <typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow load_local(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  if (in.a >= locals.size()) {
    result.errors.push_back("invalid local slot");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.locals.size() ? fn.locals[in.a] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
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

template <typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow load_cell(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& cells,
    RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  if (in.a >= cells.size()) {
    result.errors.push_back("invalid cell slot");
    return XlangVMOpFlow::ReturnResult;
  }
  auto* cell = value_as_cell(cells[in.a]);
  if (cell == nullptr) {
    result.errors.push_back("invalid cell object");
    return XlangVMOpFlow::ReturnResult;
  }
  if (cell->value.tag == ValueTag::Invalid) {
    const uint32_t local_slot = in.a < fn.cell_slots.size() ? fn.cell_slots[in.a] : UINT32_MAX;
    const std::string name = local_slot < fn.locals.size() ? fn.locals[local_slot] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
               ? XlangVMOpFlow::ContinueLoop
               : XlangVMOpFlow::ReturnResult;
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
    std::vector<Value>& native_call_args,
    const std::vector<size_t>& register_last_use,
    const std::vector<bool>& register_loop_carried,
    size_t ip,
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
  Value deleted_value;
  if (regs[in.a].tag == ValueTag::Invalid &&
      cell->value.tag == ValueTag::Object && cell->value.as.obj != nullptr) {
    deleted_value = cell->value;
    Object* deleted_object = cell->value.as.obj;
    for (size_t i = 0; i < regs.size(); ++i) {
      if (regs[i].tag == ValueTag::Object && regs[i].as.obj == deleted_object) {
        value_set_invalid(regs[i]);
      }
    }
    for (auto& arg : native_call_args) {
      if (arg.tag == ValueTag::Object && arg.as.obj == deleted_object) {
        value_set_invalid(arg);
      }
    }
    native_call_args.clear();
    for (size_t i = 0; i < regs.size() && i < register_last_use.size(); ++i) {
      const bool loop_carried = i < register_loop_carried.size() &&
          register_loop_carried[i];
      if (!loop_carried) {
        value_set_invalid(regs[i]);
      }
    }
  }
  value_assign_fast(cell->value, regs[in.a]);
  value_assign_fast(locals[fn.cell_slots[in.dst]], regs[in.a]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseNameError>
XLANG3_HOT_INLINE XlangVMOpFlow load_free(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    const std::vector<Value>& fn_obj_closure,
    RuntimeResult& result,
    RaiseNameError&& raise_name_error) {
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
  if (cell->value.tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.free_vars.size() ? fn.free_vars[in.a] : "?";
    return raise_name_error("free variable '" + name + "' is not defined")
               ? XlangVMOpFlow::ContinueLoop
               : XlangVMOpFlow::ReturnResult;
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
