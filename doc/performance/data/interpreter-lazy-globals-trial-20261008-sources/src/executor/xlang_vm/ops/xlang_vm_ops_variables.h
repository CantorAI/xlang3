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
#include "xlang3/eval_locals.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include <string>
#include <array>
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
  // Compiled expressions load the same names repeatedly. Exact-dict hits
  // can use the stored string index without allocating a new Python key for
  // every load. Misses retain the generic path and subclass missing hooks.
  if (value_as_dict(globals_mapping) != nullptr &&
      mapping_get_string_item(globals_mapping, name, out, error)) {
    found = true;
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

XLANG3_HOT_INLINE void store_local_pair(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    const std::vector<size_t>& register_last_use,
    size_t ip) {
  if (in.a == in.c) {
    // Both stores consume the same register. Keep it alive until the second
    // assignment, matching the two original StoreLocal instructions.
    xlang_perf_count_store_local(false);
    value_assign_fast(locals[in.dst], regs[in.a]);
  } else {
    store_local(in, regs, locals, register_last_use, ip);
  }
  const ir::Instr second{ir::Op::StoreLocal, in.b, in.c, 0, 0};
  store_local(second, regs, locals, register_last_use, ip);
}

template <typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow store_local_load_local(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    const std::vector<size_t>& register_last_use,
    size_t ip,
    RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  store_local(in, regs, locals, register_last_use, ip);
  if (in.c >= locals.size()) {
    result.errors.push_back("invalid local slot in store/load pair");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.c].tag == ValueTag::Invalid) {
    const std::string name = in.c < fn.locals.size() ? fn.locals[in.c] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.b], locals[in.c]);
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow move_local(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    size_t& ip,
    bool allow_guarded_fast_path) {
  if ((in.c & ir::kGuardedLocalMoveFlag) != 0) {
    const size_t span = in.c & ir::kGuardedLocalMoveSpanMask;
    if (span == 0 || ip >= fn.code.size() || span >= fn.code.size() - ip) {
      result.errors.push_back("invalid guarded local move fallback span");
      return XlangVMOpFlow::ReturnResult;
    }
    const auto immediate_numeric = [](const Value& value) {
      return value.tag == ValueTag::Int64 || value.tag == ValueTag::Double;
    };
    bool exact_numeric_chain = allow_guarded_fast_path;
    for (size_t offset = 0; exact_numeric_chain && offset <= span; ++offset) {
      const auto& move = fn.code[ip + offset];
      if (move.op != ir::Op::MoveLocal || (offset != 0 && move.c != 0) ||
          move.dst >= locals.size() ||
          move.a >= locals.size() || !immediate_numeric(locals[move.dst]) ||
          !immediate_numeric(locals[move.a])) {
        exact_numeric_chain = false;
      }
    }
    if (exact_numeric_chain) {
      // Exact immediate values cannot run finalizers or weakref callbacks while
      // locals are replaced. Preserve copy order, then skip the redundant VM
      // dispatches; observable/debugged execution keeps the original opcodes.
      for (size_t offset = 0; offset <= span; ++offset) {
        const auto& move = fn.code[ip + offset];
        value_assign_fast(locals[move.dst], locals[move.a]);
      }
      ip += span;
      return XlangVMOpFlow::Next;
    }
  }
  value_assign_fast(locals[in.dst], locals[in.a]);
  return XlangVMOpFlow::Next;
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
  const auto register_is_live = [&](size_t index) {
    if (index < register_loop_carried.size() && register_loop_carried[index]) {
      return true;
    }
    return index < register_last_use.size() &&
        register_last_use[index] != std::numeric_limits<size_t>::max() &&
        register_last_use[index] > ip;
  };
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
      if (!register_is_live(i) &&
          ((reg.tag == ValueTag::Object && reg.as.obj == local.as.obj) ||
           is_deleted_list_item(reg))) {
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
    if (!register_is_live(i)) {
      value_set_invalid(regs[i]);
    }
  }
  value_set_invalid(locals[in.dst]);
}

inline void release_deleted_global_temporaries(
    const Value& deleted_value,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    XlangVMInstrCacheStorage& caches,
    const std::vector<size_t>& register_last_use,
    const std::vector<bool>& register_loop_carried,
    size_t ip) {
  if (deleted_value.tag != ValueTag::Object || deleted_value.as.obj == nullptr) return;
  // Deletion is a cold ownership boundary. Release obsolete expression/call
  // temporaries here, rather than adding scans to every VM instruction. Real
  // future and loop-carried registers remain roots; Python aliases still own
  // the object through their namespace/container values.
  for (size_t index = 0; index < regs.size(); ++index) {
    const bool carried = index < register_loop_carried.size() && register_loop_carried[index];
    const bool future = index < register_last_use.size() &&
        register_last_use[index] != std::numeric_limits<size_t>::max() &&
        register_last_use[index] > ip;
    // An obsolete container temporary can retain the deleted value too (for
    // example max([item])). Apply the same dead-register cleanup as del-local,
    // including those indirect owners, rather than checking pointer equality.
    if (!carried && !future) {
      value_set_invalid(regs[index]);
    }
  }
  native_call_args.clear();
  for (auto& cache : caches) {
    if (cache.global.value.tag == ValueTag::Object &&
        cache.global.value.as.obj == deleted_value.as.obj) {
      cache.global.kind = 0;
      value_set_invalid(cache.global.value);
    }
  }
}

XLANG3_HOT_INLINE void delete_global(
    const ir::Instr& in,
    const ir::Function& fn,
    InterpreterFallbackGlobals& globals,
    uint64_t& globals_version,
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    XlangVMInstrCacheStorage& caches,
    const std::vector<size_t>& register_last_use,
    const std::vector<bool>& register_loop_carried,
    size_t ip) {
  Value deleted_value = globals.take(fn.names[in.dst]);
  ++globals_version;
  // Publish the absent binding and new version before finalizers reenter.
  release_deleted_global_temporaries(deleted_value, regs, native_call_args,
                                    caches, register_last_use, register_loop_carried, ip);
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow load_module_slot(
    const ir::Instr& in,
    const ir::Module& module,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    InterpreterFallbackGlobals& globals,
    XlangVMInstrCache& instr_cache,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.a >= module.global_slots.size()) {
    result.errors.push_back("invalid module slot");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& name = module.global_slots[in.a];
  auto* globals_module_obj = value_as_module(globals_module);
  if (globals_module_obj == nullptr && mapping_is_dict(globals_module)) {
    const auto& fn = module.functions[runtime.current_frame_function_id()];
    if (const Value* active_locals = current_eval_locals(runtime, &fn)) {
      Value retained_locals = *active_locals;
      std::string lookup_error;
      if (mapping_get_item_runtime(runtime, retained_locals, Value::string(name), regs[in.dst], lookup_error))
        return XlangVMOpFlow::Next;
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        const Value* key_error = runtime.find_builtin("KeyError");
        auto* expected = key_error == nullptr ? nullptr : value_as_class(*key_error);
        auto* actual = value_as_class(runtime.exception_type(pending));
        if (expected == nullptr || actual == nullptr || !class_is_subclass(actual, expected))
          return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      } else if (lookup_error != "key not found") {
        return raise_runtime_error(lookup_error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
    }
  }
  if (globals_module_obj != nullptr) {
    xlang_vm_cache_touch(instr_cache, XlangVMCacheDomain::Global);
    auto& global_cache = instr_cache.global;
    if (global_cache.kind == 1 && global_cache.version == globals_module_obj->version) {
      const uint32_t slot = global_cache.slot;
      if (slot < globals_module_obj->slots.size() &&
          globals_module_obj->slots[slot].tag != ValueTag::Invalid) {
        value_assign_fast(regs[in.dst], globals_module_obj->slots[slot]);
        return XlangVMOpFlow::Next;
      }
    } else if (global_cache.kind == 2 && global_cache.version == globals_module_obj->version) {
      value_assign_fast(regs[in.dst], global_cache.value);
      return XlangVMOpFlow::Next;
    }
    const auto bound = globals_module_obj->name_to_slot.find(name);
    if (bound != globals_module_obj->name_to_slot.end() && bound->second == in.a &&
        in.a < globals_module_obj->slots.size() && globals_module_obj->slots[in.a].tag != ValueTag::Invalid) {
      value_assign_fast(regs[in.dst], globals_module_obj->slots[in.a]);
      global_cache.kind = 1;
      global_cache.slot = in.a;
      global_cache.version = globals_module_obj->version;
      return XlangVMOpFlow::Next;
    }
    std::string slot_error;
    uint32_t dynamic_slot = 0;
    if (module_find_attr_slot(globals_module, name, dynamic_slot, slot_error) &&
        dynamic_slot < globals_module_obj->slots.size() &&
        globals_module_obj->slots[dynamic_slot].tag != ValueTag::Invalid) {
      value_assign_fast(regs[in.dst], globals_module_obj->slots[dynamic_slot]);
      global_cache.kind = 1;
      global_cache.slot = dynamic_slot;
      global_cache.version = globals_module_obj->version;
      return XlangVMOpFlow::Next;
    }
    Value builtin;
    if (runtime.resolve_builtin(name, builtin)) {
      value_move_assign_fast(regs[in.dst], builtin);
      global_cache.kind = 0;
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
  if (const Value* value = globals.find(name)) {
    value_assign_fast(regs[in.dst], *value);
  } else if (Value builtin; runtime.resolve_builtin(name, builtin)) {
    value_move_assign_fast(regs[in.dst], builtin);
  } else {
    return raise_runtime_error("name '" + name + "' is not defined") ? XlangVMOpFlow::ContinueLoop
                                                                    : XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE XlangVMOpFlow store_module_slot(
    const ir::Instr& in,
    const ir::Module& module,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    InterpreterFallbackGlobals& globals,
    uint64_t& globals_version,
    RuntimeResult& result) {
  if (in.dst >= module.global_slots.size() || in.a >= regs.size()) {
    result.errors.push_back("invalid module slot store");
    return XlangVMOpFlow::ReturnResult;
  }
  if (mapping_is_dict(globals_module)) {
    // Entry-code walrus bindings belong to eval locals, while nested function
    // global stores belong to the retained globals dict. Updating a separate
    // interpreter map would silently lose the write and subsequent reads.
    const auto& fn = module.functions[runtime.current_frame_function_id()];
    const Value* eval_locals = current_eval_locals(runtime, &fn);
    Value target = eval_locals == nullptr ? globals_module : *eval_locals;
    std::string error;
    const Value key = Value::string(module.global_slots[in.dst]);
    const bool stored = eval_locals != nullptr
        ? mapping_set_item_runtime(runtime, target, key, regs[in.a], error)
        : mapping_set_item(target, key, regs[in.a], error);
    if (!stored) { result.errors.push_back(error); return XlangVMOpFlow::ReturnResult; }
    return XlangVMOpFlow::Next;
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
    InterpreterFallbackGlobals& globals,
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
  Value eval_locals;
  if (const Value* active_locals = current_eval_locals(runtime, &fn)) {
    value_assign_fast(eval_locals, *active_locals);
    Value key = Value::string(fn.names[in.a]);
    std::string lookup_error;
    if (mapping_get_item_runtime(runtime, eval_locals, key, regs[in.dst], lookup_error)) {
      global_cache.kind = 0;
      return XlangVMOpFlow::Next;
    }
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      const Value* key_error_value = runtime.find_builtin("KeyError");
      auto* expected = key_error_value == nullptr ? nullptr : value_as_class(*key_error_value);
      auto* actual = value_as_class(runtime.exception_type(pending));
      if (expected == nullptr || actual == nullptr ||
          !class_is_subclass(actual, expected)) {
        return raise_exception_value(std::move(pending))
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
    } else if (lookup_error != "key not found") {
      return raise_runtime_error(lookup_error)
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    global_cache.kind = 0;
  }
  // A live dictionary has no module version counter. Never reuse a cached
  // value across callback mutation or a returned function's later activation.
  const bool live_dict_globals = mapping_is_dict(globals_module);
  if (live_dict_globals) global_cache.kind = 0;
  if (global_cache.kind != 0 && eval_locals.tag == ValueTag::Invalid) {
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
    Value builtin;
    if (runtime.resolve_builtin(name, builtin)) {
      value_move_assign_fast(regs[in.dst], builtin);
      global_cache.kind = 0;
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
    if (const Value* value = globals.find(name)) {
    value_assign_fast(regs[in.dst], *value);
    value_assign_fast(global_cache.value, regs[in.dst]);
    global_cache.version = globals_version;
    global_cache.kind = 2;
    } else if (Value builtin; runtime.resolve_builtin(name, builtin)) {
      value_move_assign_fast(regs[in.dst], builtin);
      global_cache.kind = 0;
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
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    Value& globals_module,
    InterpreterFallbackGlobals& globals,
    uint64_t& globals_version,
    XlangVMInstrCache& instr_cache,
    RuntimeResult& result) {
  xlang_vm_cache_touch(instr_cache, XlangVMCacheDomain::Global);
  if (in.dst >= fn.names.size() || in.a >= regs.size()) {
    result.errors.push_back("invalid global store");
    return XlangVMOpFlow::ReturnResult;
  }
  if (mapping_is_dict(globals_module)) {
    const Value* eval_locals = current_eval_locals(runtime, &fn);
    Value target = eval_locals == nullptr ? globals_module : *eval_locals;
    std::string error;
    const Value key = Value::string(fn.names[in.dst]);
    const bool stored = eval_locals != nullptr
        ? mapping_set_item_runtime(runtime, target, key, regs[in.a], error)
        : mapping_set_item(target, key, regs[in.a], error);
    instr_cache.global.kind = 0;
    if (!stored) { result.errors.push_back(error); return XlangVMOpFlow::ReturnResult; }
    return XlangVMOpFlow::Next;
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
    XlangVMSmallRegisterBuffer& regs,
    std::vector<Value>& native_call_args,
    XlangVMInstrCacheStorage& caches,
    const std::vector<size_t>& register_last_use,
    const std::vector<bool>& register_loop_carried,
    size_t ip,
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
  Value deleted_value = globals_module_obj->slots[slot];
  if (!module_delete_attr(globals_module, module.global_slots[in.dst], error)) {
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop
                                      : XlangVMOpFlow::ReturnResult;
  }
  release_deleted_global_temporaries(deleted_value, regs, native_call_args,
                                    caches, register_last_use, register_loop_carried, ip);
  return XlangVMOpFlow::Next;
}

template <typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow load_local(
    const ir::Instr&, const ir::Function&, XlangVMSmallRegisterBuffer&,
    XlangVMSmallValueBuffer&, RuntimeResult&, RaiseUnboundLocalError&&);

template <typename RaiseUnboundLocalError, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow load_local_global(
    const ir::Instr& in, const ir::Function& fn, Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs, XlangVMSmallValueBuffer& locals,
    Value& globals_module, InterpreterFallbackGlobals& globals,
    uint64_t globals_version, XlangVMInstrCache& instr_cache, RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error,
    RaiseRuntimeError&& raise_runtime_error, RaiseExceptionValue&& raise_exception_value) {
  const ir::Instr local{ir::Op::LoadLocal, in.dst, in.a, 0, 0};
  auto flow = load_local(local, fn, regs, locals, result, raise_unbound_local_error);
  if (flow != XlangVMOpFlow::Next) return flow;
  const ir::Instr global{ir::Op::LoadGlobal, in.b, in.c, 0, 0};
  return load_global(global, fn, runtime, regs, globals_module, globals, globals_version,
                     instr_cache, result, raise_runtime_error, raise_exception_value);
}

template <typename RaiseUnboundLocalError, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow load_global_local(
    const ir::Instr& in, const ir::Function& fn, Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs, XlangVMSmallValueBuffer& locals,
    Value& globals_module, InterpreterFallbackGlobals& globals,
    uint64_t globals_version, XlangVMInstrCache& instr_cache, RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error,
    RaiseRuntimeError&& raise_runtime_error, RaiseExceptionValue&& raise_exception_value) {
  const ir::Instr global{ir::Op::LoadGlobal, in.dst, in.a, 0, 0};
  auto flow = load_global(global, fn, runtime, regs, globals_module, globals, globals_version,
                          instr_cache, result, raise_runtime_error, raise_exception_value);
  if (flow != XlangVMOpFlow::Next) return flow;
  const ir::Instr local{ir::Op::LoadLocal, in.b, in.c, 0, 0};
  return load_local(local, fn, regs, locals, result, raise_unbound_local_error);
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
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    size_t& ip,
    bool allow_guarded_fast_path,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= fn.constants.size()) {
    result.errors.push_back("invalid local const add");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& lhs = locals[in.a];
  const auto& rhs = fn.constants[in.b];
  if ((in.c & ir::kGuardedLocalAddFlag) != 0) {
    const size_t fallback_span = in.c & ir::kGuardedLocalAddSpanMask;
    if (fallback_span == 0 || ip >= fn.code.size() ||
        fallback_span >= fn.code.size() - ip) {
      result.errors.push_back("invalid guarded local const add fallback span");
      return XlangVMOpFlow::ReturnResult;
    }
    // Exact immediate numeric tags exclude bool, big-int objects, subclasses,
    // and user objects; those all execute the following normal Add sequence.
    if (!allow_guarded_fast_path ||
        (lhs.tag != ValueTag::Int64 && lhs.tag != ValueTag::Double) ||
        (rhs.tag != ValueTag::Int64 && rhs.tag != ValueTag::Double)) {
      return XlangVMOpFlow::Next;
    }
    if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
      int64_t sum = 0;
      if (xlang_vm_checked_add_i64(lhs.as.i64, rhs.as.i64, sum)) {
        // Exact-int loop increments can commit the checked payload directly;
        // overflow still executes the original Add sequence for bigint promotion.
        value_set_int64(locals[in.dst], sum);
        frame.release_memoryviews_for_skipped_local_add(fn, ip + 1, fallback_span);
        ip += fallback_span;
        return XlangVMOpFlow::Next;
      }
      return XlangVMOpFlow::Next;
    }
    Value sum;
    if (!fast_add(lhs, rhs, sum)) return XlangVMOpFlow::Next;
    value_move_assign_fast(locals[in.dst], sum);
    frame.release_memoryviews_for_skipped_local_add(fn, ip + 1, fallback_span);
    ip += fallback_span;
    return XlangVMOpFlow::Next;
  }
  if (!fast_add(lhs, rhs, locals[in.dst])) {
    std::string error;
    if (!value_add(lhs, rhs, locals[in.dst], error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow load_local_pair(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  if (in.a >= locals.size() || in.c >= locals.size()) {
    result.errors.push_back("invalid paired local slot");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.locals.size() ? fn.locals[in.a] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.dst], locals[in.a]);
  if (locals[in.c].tag == ValueTag::Invalid) {
    const std::string name = in.c < fn.locals.size() ? fn.locals[in.c] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.b], locals[in.c]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow load_local_const(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  if (in.a >= locals.size() || in.c >= fn.constants.size()) {
    result.errors.push_back("invalid local/constant pair");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.locals.size() ? fn.locals[in.a] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.dst], locals[in.a]);
  value_borrow_assign_fast(regs[in.b], fn.constants[in.c]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow load_const_pair(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallRegisterBuffer& regs,
    RuntimeResult& result,
    RaiseRuntimeError&&) {
  if (in.a >= fn.constants.size() || in.c >= fn.constants.size()) {
    result.errors.push_back("invalid paired constant index");
    return XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.dst], fn.constants[in.a]);
  value_borrow_assign_fast(regs[in.b], fn.constants[in.c]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseUnboundLocalError, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow inplace_add_local_const(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= fn.constants.size()) {
    result.errors.push_back("invalid local const in-place add");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& lhs = locals[in.a];
  const auto& rhs = fn.constants[in.b];
  if (lhs.tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.locals.size() ? fn.locals[in.a] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  if (fast_add(lhs, rhs, locals[in.dst])) {
    return XlangVMOpFlow::Next;
  }
  const Value* callable = runtime.find_builtin("__xlang3_inplace_add__");
  if (callable == nullptr) {
    return raise_runtime_error("in-place addition is unavailable")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  Value args[2] = {lhs, rhs};
  Value output;
  std::string error;
  if (runtime_call_callable(runtime, *callable, args, 2, output, error)) {
    value_move_assign_fast(locals[in.dst], output);
    return XlangVMOpFlow::Next;
  }
  Value pending;
  if (runtime.take_pending_exception(pending)) {
    return raise_exception_value(std::move(pending))
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return raise_runtime_error(error)
      ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

template <typename RaiseUnboundLocalError, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow inplace_add_local_local(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= locals.size()) {
    result.errors.push_back("invalid local local in-place add");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto raise_unbound = [&](uint32_t slot) {
    const std::string name = slot < fn.locals.size() ? fn.locals[slot] : "?";
    return raise_unbound_local_error(
        "cannot access local variable '" + name +
        "' where it is not associated with a value");
  };
  if (locals[in.a].tag == ValueTag::Invalid) {
    return raise_unbound(in.a) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.b].tag == ValueTag::Invalid) {
    return raise_unbound(in.b) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const Value& lhs = locals[in.a];
  const Value& rhs = locals[in.b];
  Value output;
  if (fast_add(lhs, rhs, output)) {
    value_move_assign_fast(locals[in.dst], output);
    return XlangVMOpFlow::Next;
  }
  const Value* callable = runtime.find_builtin("__xlang3_inplace_add__");
  if (callable == nullptr) {
    return raise_runtime_error("in-place addition is unavailable")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  Value args[2] = {lhs, rhs};
  std::string error;
  if (runtime_call_callable(runtime, *callable, args, 2, output, error)) {
    value_move_assign_fast(locals[in.dst], output);
    return XlangVMOpFlow::Next;
  }
  Value pending;
  if (runtime.take_pending_exception(pending)) {
    return raise_exception_value(std::move(pending))
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return raise_runtime_error(error)
      ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow add_local_local(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    size_t& ip,
    bool allow_guarded_fast_path,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.dst >= locals.size() || in.a >= locals.size() || in.b >= locals.size()) {
    result.errors.push_back("invalid local local add");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& lhs = locals[in.a];
  const auto& rhs = locals[in.b];
  if ((in.c & ir::kGuardedLocalAddFlag) != 0) {
    const size_t fallback_span = in.c & ir::kGuardedLocalAddSpanMask;
    if (fallback_span == 0 || ip >= fn.code.size() ||
        fallback_span >= fn.code.size() - ip) {
      result.errors.push_back("invalid guarded local local add fallback span");
      return XlangVMOpFlow::ReturnResult;
    }
    // Keep the common loop update to one dispatch; unusual or observable cases
    // fall through to the exact generic bytecode emitted immediately afterward.
    if (!allow_guarded_fast_path ||
        (lhs.tag != ValueTag::Int64 && lhs.tag != ValueTag::Double) ||
        (rhs.tag != ValueTag::Int64 && rhs.tag != ValueTag::Double)) {
      return XlangVMOpFlow::Next;
    }
    if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
      int64_t sum = 0;
      if (xlang_vm_checked_add_i64(lhs.as.i64, rhs.as.i64, sum)) {
        // Exact-int local updates avoid a temporary Value; overflow resumes
        // the original Add bytecode so Python's arbitrary-precision result is preserved.
        value_set_int64(locals[in.dst], sum);
        frame.release_memoryviews_for_skipped_local_add(fn, ip + 1, fallback_span);
        ip += fallback_span;
        return XlangVMOpFlow::Next;
      }
      return XlangVMOpFlow::Next;
    }
    Value sum;
    if (!fast_add(lhs, rhs, sum)) return XlangVMOpFlow::Next;
    value_move_assign_fast(locals[in.dst], sum);
    frame.release_memoryviews_for_skipped_local_add(fn, ip + 1, fallback_span);
    ip += fallback_span;
    return XlangVMOpFlow::Next;
  }
  if (!fast_add(lhs, rhs, locals[in.dst])) {
    std::string error;
    if (!value_add(lhs, rhs, locals[in.dst], error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  return XlangVMOpFlow::Next;
}

enum class GuardedNumericPlanResult : uint8_t { NotApplicable, Applied, Invalid };

// Keep the mixed/double fallback out of the interpreter's hot exact-int path;
// functions can contain guarded plans even when their warmed values are ints.
XLANG3_NOINLINE inline GuardedNumericPlanResult evaluate_mixed_guarded_numeric_plan(
    const ir::GuardedLocalNumericExprSpec& spec,
    const ir::Function& fn,
    XlangVMSmallValueBuffer& locals,
    Value& out,
    RuntimeResult& result) {
  std::array<ValueTag, ir::kMaxGuardedLocalNumericExprNodes> tags;
  std::array<int64_t, ir::kMaxGuardedLocalNumericExprNodes> integers;
  std::array<double, ir::kMaxGuardedLocalNumericExprNodes> doubles;
  for (size_t i = 0; i < spec.nodes.size(); ++i) {
    const auto& node = spec.nodes[i];
    switch (node.kind) {
      case ir::GuardedLocalNumericExprNodeKind::Local:
        if (node.a >= locals.size() ||
            (locals[node.a].tag != ValueTag::Int64 && locals[node.a].tag != ValueTag::Double)) {
          return GuardedNumericPlanResult::NotApplicable;
        }
        tags[i] = locals[node.a].tag;
        if (tags[i] == ValueTag::Int64) integers[i] = locals[node.a].as.i64;
        else doubles[i] = locals[node.a].as.f64;
        break;
      case ir::GuardedLocalNumericExprNodeKind::Constant:
        if (node.a >= fn.constants.size()) {
          result.errors.push_back("invalid guarded numeric expression constant");
          return GuardedNumericPlanResult::Invalid;
        }
        if (fn.constants[node.a].tag != ValueTag::Int64 &&
            fn.constants[node.a].tag != ValueTag::Double) {
          return GuardedNumericPlanResult::NotApplicable;
        }
        tags[i] = fn.constants[node.a].tag;
        if (tags[i] == ValueTag::Int64) integers[i] = fn.constants[node.a].as.i64;
        else doubles[i] = fn.constants[node.a].as.f64;
        break;
      case ir::GuardedLocalNumericExprNodeKind::Add:
      case ir::GuardedLocalNumericExprNodeKind::Sub:
      case ir::GuardedLocalNumericExprNodeKind::Mul: {
        if (node.a >= i || node.b >= i) {
          result.errors.push_back("invalid guarded numeric expression node reference");
          return GuardedNumericPlanResult::Invalid;
        }
        if (tags[node.a] == ValueTag::Int64 && tags[node.b] == ValueTag::Int64) {
          bool succeeded = false;
          if (node.kind == ir::GuardedLocalNumericExprNodeKind::Add) {
            succeeded = xlang_vm_checked_add_i64(integers[node.a], integers[node.b], integers[i]);
          } else if (node.kind == ir::GuardedLocalNumericExprNodeKind::Sub) {
            succeeded = xlang_vm_checked_sub_i64(integers[node.a], integers[node.b], integers[i]);
          } else {
            succeeded = xlang_vm_checked_mul_i64(integers[node.a], integers[node.b], integers[i]);
          }
          if (!succeeded) return GuardedNumericPlanResult::NotApplicable;
          tags[i] = ValueTag::Int64;
        } else {
          const double lhs = tags[node.a] == ValueTag::Int64
              ? static_cast<double>(integers[node.a]) : doubles[node.a];
          const double rhs = tags[node.b] == ValueTag::Int64
              ? static_cast<double>(integers[node.b]) : doubles[node.b];
          if (node.kind == ir::GuardedLocalNumericExprNodeKind::Add) doubles[i] = lhs + rhs;
          else if (node.kind == ir::GuardedLocalNumericExprNodeKind::Sub) doubles[i] = lhs - rhs;
          else doubles[i] = lhs * rhs;
          tags[i] = ValueTag::Double;
        }
        break;
      }
      default:
        result.errors.push_back("invalid guarded numeric expression node kind");
        return GuardedNumericPlanResult::Invalid;
    }
  }
  const size_t final_index = spec.nodes.size() - 1;
  if (tags[final_index] == ValueTag::Int64) value_set_int64(out, integers[final_index]);
  else value_set_number(out, doubles[final_index]);
  return GuardedNumericPlanResult::Applied;
}

XLANG3_HOT_INLINE XlangVMOpFlow guarded_local_numeric_expr(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMFrame& frame,
    XlangVMSmallValueBuffer& locals,
    RuntimeResult& result,
    size_t& ip,
    bool allow_guarded_fast_path) {
  if (in.dst >= locals.size() || in.a >= fn.guarded_local_numeric_exprs.size()) {
    result.errors.push_back("invalid guarded local numeric expression");
    return XlangVMOpFlow::ReturnResult;
  }
  const auto& spec = fn.guarded_local_numeric_exprs[in.a];
  const size_t fallback_span = spec.fallback_span;
  if (spec.nodes.empty() || spec.nodes.size() > ir::kMaxGuardedLocalNumericExprNodes ||
      fallback_span == 0 || ip >= fn.code.size() ||
      fallback_span >= fn.code.size() - ip) {
    result.errors.push_back("invalid guarded local numeric expression fallback");
    return XlangVMOpFlow::ReturnResult;
  }
  if (!allow_guarded_fast_path) return XlangVMOpFlow::Next;

  // Keep the hot accumulator loop out of the generic node evaluator: its
  // static (accumulator + index * constant) - constant shape needs only four
  // registers and checked integer operations, then retains the same fallback.
  if (spec.nodes.size() == 7) {
    const auto& n0 = spec.nodes[0];
    const auto& n1 = spec.nodes[1];
    const auto& n2 = spec.nodes[2];
    const auto& n3 = spec.nodes[3];
    const auto& n4 = spec.nodes[4];
    const auto& n5 = spec.nodes[5];
    const auto& n6 = spec.nodes[6];
    if (n0.kind == ir::GuardedLocalNumericExprNodeKind::Local &&
        n0.a == in.dst &&
        n1.kind == ir::GuardedLocalNumericExprNodeKind::Local &&
        n2.kind == ir::GuardedLocalNumericExprNodeKind::Constant &&
        n3.kind == ir::GuardedLocalNumericExprNodeKind::Mul &&
        n3.a == 1 && n3.b == 2 &&
        n4.kind == ir::GuardedLocalNumericExprNodeKind::Add &&
        n4.a == 0 && n4.b == 3 &&
        n5.kind == ir::GuardedLocalNumericExprNodeKind::Constant &&
        n6.kind == ir::GuardedLocalNumericExprNodeKind::Sub &&
        n6.a == 4 && n6.b == 5 &&
        n1.a < locals.size() && n2.a < fn.constants.size() &&
        n5.a < fn.constants.size() &&
        locals[in.dst].tag == ValueTag::Int64 &&
        locals[n1.a].tag == ValueTag::Int64 &&
        fn.constants[n2.a].tag == ValueTag::Int64 &&
        fn.constants[n5.a].tag == ValueTag::Int64) {
      int64_t scaled_index = 0;
      int64_t adjusted_index = 0;
      int64_t sum = 0;
      if (xlang_vm_checked_mul_i64(
              locals[n1.a].as.i64, fn.constants[n2.a].as.i64, scaled_index) &&
          xlang_vm_checked_sub_i64(
              scaled_index, fn.constants[n5.a].as.i64, adjusted_index) &&
          xlang_vm_checked_add_i64(locals[in.dst].as.i64, adjusted_index, sum)) {
        value_set_int64(locals[in.dst], sum);
        frame.release_memoryviews_for_skipped_local_add(fn, ip + 1, fallback_span);
        ip += fallback_span;
        return XlangVMOpFlow::Next;
      }
    }
  }

  // The common scalar-loop shape contains only exact int64 locals/constants.
  // Evaluate that analyzed plan in unboxed integers and construct one Value at
  // the destination. Checked overflow exits to the original bytecode so Python
  // bigint promotion, bool/subclass operators, and other cases stay unchanged.
  // The lowering plan is topological: each node writes its own slot before a
  // later node can reference it, so zeroing this fixed scratch array per loop
  // iteration only adds work to the exact-int fast path.
  std::array<int64_t, ir::kMaxGuardedLocalNumericExprNodes> int_values;
  bool exact_int_plan = true;
  for (size_t i = 0; i < spec.nodes.size(); ++i) {
    const auto& node = spec.nodes[i];
    switch (node.kind) {
      case ir::GuardedLocalNumericExprNodeKind::Local:
        if (node.a >= locals.size() || locals[node.a].tag == ValueTag::Invalid ||
            locals[node.a].tag != ValueTag::Int64) {
          exact_int_plan = false;
          break;
        }
        int_values[i] = locals[node.a].as.i64;
        break;
      case ir::GuardedLocalNumericExprNodeKind::Constant:
        if (node.a >= fn.constants.size()) {
          result.errors.push_back("invalid guarded numeric expression constant");
          return XlangVMOpFlow::ReturnResult;
        }
        if (fn.constants[node.a].tag != ValueTag::Int64) {
          exact_int_plan = false;
          break;
        }
        int_values[i] = fn.constants[node.a].as.i64;
        break;
      case ir::GuardedLocalNumericExprNodeKind::Add:
      case ir::GuardedLocalNumericExprNodeKind::Sub:
      case ir::GuardedLocalNumericExprNodeKind::Mul: {
        if (node.a >= i || node.b >= i) {
          result.errors.push_back("invalid guarded numeric expression node reference");
          return XlangVMOpFlow::ReturnResult;
        }
        bool succeeded = false;
        if (node.kind == ir::GuardedLocalNumericExprNodeKind::Add) {
          succeeded = xlang_vm_checked_add_i64(int_values[node.a], int_values[node.b], int_values[i]);
        } else if (node.kind == ir::GuardedLocalNumericExprNodeKind::Sub) {
          succeeded = xlang_vm_checked_sub_i64(int_values[node.a], int_values[node.b], int_values[i]);
        } else {
          succeeded = xlang_vm_checked_mul_i64(int_values[node.a], int_values[node.b], int_values[i]);
        }
        if (!succeeded) exact_int_plan = false;
        break;
      }
      default:
        result.errors.push_back("invalid guarded numeric expression node kind");
        return XlangVMOpFlow::ReturnResult;
    }
    if (!exact_int_plan) break;
  }
  if (exact_int_plan) {
    Value result_value;
    value_set_int64(result_value, int_values[spec.nodes.size() - 1]);
    value_move_assign_fast(locals[in.dst], result_value);
    frame.release_memoryviews_for_skipped_local_add(fn, ip + 1, fallback_span);
    ip += fallback_span;
    return XlangVMOpFlow::Next;
  }

  // This cold helper keeps mixed-number checks and scratch arrays outside the
  // exact-int interpreter path while retaining bigint and malformed-IR fallback.
  Value numeric_result;
  const auto numeric_status = evaluate_mixed_guarded_numeric_plan(
      spec, fn, locals, numeric_result, result);
  if (numeric_status == GuardedNumericPlanResult::Invalid) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (numeric_status == GuardedNumericPlanResult::Applied) {
    value_move_assign_fast(locals[in.dst], numeric_result);
    frame.release_memoryviews_for_skipped_local_add(fn, ip + 1, fallback_span);
    ip += fallback_span;
    return XlangVMOpFlow::Next;
  }
  std::array<Value, ir::kMaxGuardedLocalNumericExprNodes> values{};
  for (size_t i = 0; i < spec.nodes.size(); ++i) {
    const auto& node = spec.nodes[i];
    switch (node.kind) {
      case ir::GuardedLocalNumericExprNodeKind::Local:
        if (node.a >= locals.size() || locals[node.a].tag == ValueTag::Invalid) {
          return XlangVMOpFlow::Next;
        }
        if (locals[node.a].tag != ValueTag::Int64 && locals[node.a].tag != ValueTag::Double) {
          return XlangVMOpFlow::Next;
        }
        value_borrow_assign_fast(values[i], locals[node.a]);
        break;
      case ir::GuardedLocalNumericExprNodeKind::Constant:
        if (node.a >= fn.constants.size()) {
          result.errors.push_back("invalid guarded numeric expression constant");
          return XlangVMOpFlow::ReturnResult;
        }
        if (fn.constants[node.a].tag != ValueTag::Int64 &&
            fn.constants[node.a].tag != ValueTag::Double) return XlangVMOpFlow::Next;
        value_borrow_assign_fast(values[i], fn.constants[node.a]);
        break;
      case ir::GuardedLocalNumericExprNodeKind::Add:
      case ir::GuardedLocalNumericExprNodeKind::Sub:
      case ir::GuardedLocalNumericExprNodeKind::Mul: {
        if (node.a >= i || node.b >= i) {
          result.errors.push_back("invalid guarded numeric expression node reference");
          return XlangVMOpFlow::ReturnResult;
        }
        bool succeeded = false;
        if (node.kind == ir::GuardedLocalNumericExprNodeKind::Add)
          succeeded = fast_add(values[node.a], values[node.b], values[i]);
        else if (node.kind == ir::GuardedLocalNumericExprNodeKind::Sub)
          succeeded = fast_sub(values[node.a], values[node.b], values[i]);
        else
          succeeded = fast_mul(values[node.a], values[node.b], values[i]);
        // Overflow and big-integer promotion take the unchanged generic path.
        if (!succeeded) return XlangVMOpFlow::Next;
        break;
      }
      default:
        result.errors.push_back("invalid guarded numeric expression node kind");
        return XlangVMOpFlow::ReturnResult;
    }
  }
  value_move_assign_fast(locals[in.dst], values[spec.nodes.size() - 1]);
  frame.release_memoryviews_for_skipped_local_add(fn, ip + 1, fallback_span);
  ip += fallback_span;
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
    const auto register_is_live = [&](size_t index) {
      if (index < register_loop_carried.size() && register_loop_carried[index]) {
        return true;
      }
      return index < register_last_use.size() &&
          register_last_use[index] != std::numeric_limits<size_t>::max() &&
          register_last_use[index] > ip;
    };
    deleted_value = cell->value;
    Object* deleted_object = cell->value.as.obj;
    for (size_t i = 0; i < regs.size(); ++i) {
      if (!register_is_live(i) && regs[i].tag == ValueTag::Object &&
          regs[i].as.obj == deleted_object) {
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
      if (!register_is_live(i)) {
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
