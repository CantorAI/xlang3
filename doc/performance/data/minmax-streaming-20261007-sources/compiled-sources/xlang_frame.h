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

#include "xlang_value_buffer.h"
#include "xlang_vm_instr_cache.h"
#include "xlang3/builtin_methods.h"
#include "xlang3/interpreter.h"
#include "xlang3/ir.h"
#include "xlang3/module_object.h"
#include "xlang3/runtime.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

/*
XlangVM design note
Author: Shawn Xiong

Purpose:
Frame and per-instruction cache state for XlangVM execution.

Ownership rule:
Instruction caches live on interpreter frame storage because they are tied to a
function's IR instruction stream. Inactive stack slots retain bounded prepared
state for functions previously activated at that depth.

The frame owns fast execution storage: locals, registers, cells, exception
handlers, and per-site caches. It references ir::Module and ir::Function but
does not own compiled code. XlangVMThread owns the frame stack/vector.
*/

namespace xlang3 {

enum class CallSiteKind : uint8_t {
  Empty,
  UserFunction,
  ExactPositionalFunction,
  GetItemUserFunction,
  GetItemNativeFunction,
  SetItemUserFunction,
  SetItemNativeFunction,
  BoundPythonMethod,
  NativeFunction,
  BoundNativeFunction,
  UserConstructor,
  NativeConstructor,
  InlineSlotConstructor,
  InlineMathPointConstructor,
  InlineSelfBinaryMethod,
  InlineSelfAttrBinaryMethod,
  InlineClassMethodAttrIntCompare,
  InlineSelfAttrBooleanExprMethod,
  InlineArgBinaryFunction,
  InlineConditionalArgFunction,
  InlineTrivialFunction,
  InlineBuiltinTypeValue,
  InlineConstMethod,
  InlineSmallSelfMethod,
  InlineSelfSlotMethod,
  InlineSelfSlotConstSumMethod,
  InlineSelfSlotMaximizeMethod,
  InlineSelfSlotNormalizeMethod,
  InlineFastListMethod,
  InlineCachedStringMethod,
  InlineCachedLen,
  BuiltinMethodSpec,
};

enum class AttrSiteKind : uint8_t {
  Empty,
  InstanceDict,
  InstanceAttr,
  InstanceSlot,
  ClassValue,
  Descriptor,
  PropertyInstanceAttr,
};

struct CallSiteCache {
  Object* callee_object = nullptr;
  Value retained_callee;
  CallSiteKind kind = CallSiteKind::Empty;
  FunctionObject* function = nullptr;
  uint64_t function_code_version = 0;
  NativeFunctionObject* native = nullptr;
  const BuiltinMethodSpec* builtin_method = nullptr;
  NativeFastCallCallback fast_callback = nullptr;
  void* native_user_data = nullptr;
  Object* arg0_object = nullptr;
  Object* arg1_object = nullptr;
  uint64_t class_version = 0;
  uint32_t lhs_slot = 0;
  uint32_t rhs_slot = 0;
  std::array<uint32_t, 3> inline_slots{};
  ModuleObject* inline_globals_module = nullptr;
  // These versions are mutually exclusive by call-site specialization. The
  // classmethod comparator uses the secondary slot for its metaclass guard.
  union {
    uint64_t inline_globals_version = 0;
    uint64_t secondary_class_version;
  };
  ir::Op inline_op = ir::Op::Add;
  uint32_t inline_function_id = UINT32_MAX;
  uint32_t fast_method_id = 0;
  uint32_t next_arg = 0;
  ir::Op next_op = ir::Op::Add;
  Value inline_const;
  bool has_next = false;
  bool next_is_constant = false;
  bool fast_releases_vm_lock = false;
  std::vector<std::pair<uint32_t, uint32_t>> slot_constructor_args;
  std::vector<Value> cached_values;
};

struct AttrSiteCache {
  uint32_t index = 0;
  AttrSiteKind kind = AttrSiteKind::Empty;
  Object* owner = nullptr;
  uint64_t version = 0;
  // For ClassValue, owner/version guard the metaclass, secondary_version
  // guards the receiver class, and class_value points into that class's attrs.
  uint64_t secondary_version = 0;
  const Value* class_value = nullptr;
  const std::string* property_attr_name = nullptr;
  Value value;
  // Class versions do not change when an accessor's __code__ is replaced.
  // Check the function generation before using names/slots from its old IR.
  FunctionObject* accessor_function = nullptr;
  uint64_t accessor_code_version = 0;
  uint32_t getter_slot = 0;
  uint32_t setter_slot = 0;
  uint32_t deleter_slot = 0;
  ir::Op getter_op = ir::Op::Add;
  ir::Op setter_op = ir::Op::Add;
  Value getter_const;
  Value setter_const;
  Value deleter_const;
  bool getter_inline = false;
  bool getter_has_const = false;
  bool setter_inline = false;
  bool setter_has_const = false;
  bool deleter_inline = false;
};

struct GlobalSiteCache {
  Value value;
  uint32_t slot = 0;
  uint64_t version = 0;
  uint8_t kind = 0;
};

struct XlangVMInstrCache : XlangVMInstrCacheCore {
  GlobalSiteCache global;
  CallSiteCache call;
  AttrSiteCache attr;
};

struct XlangVMMonitoringSiteState {
  uint64_t generation = 0;
  int64_t disabled_events = 0;
};

// Most IR instructions never touch an adaptive cache. Keep one small IP->slot
// index per instruction, but construct the owning cache payload only at the
// sites listed by FunctionExecutionMetadata. This avoids zero-initializing the
// large CallSiteCache/AttrSiteCache objects for uncached instructions on each
// Python frame activation. Monitoring DISABLE state is smaller and must remain
// per-IP even for uncached instructions, so it lives in a separate dense array
// instead of inflating every specialization cache payload.
class XlangVMInstrCacheStorage {
 public:
  using iterator = std::vector<XlangVMInstrCache>::iterator;
  using const_iterator = std::vector<XlangVMInstrCache>::const_iterator;

  void reset(size_t instruction_count,
             const std::vector<uint32_t>& cached_instruction_ips) {
    cache_slot_by_ip_.assign(instruction_count, kNoCacheSlot);
    monitoring_by_ip_.assign(instruction_count, {});
    cache_sites_.clear();
    cache_sites_.resize(cached_instruction_ips.size());
    for (size_t slot = 0; slot < cached_instruction_ips.size(); ++slot) {
      const uint32_t ip = cached_instruction_ips[slot];
      if (ip < cache_slot_by_ip_.size())
        cache_slot_by_ip_[ip] = static_cast<uint32_t>(slot);
    }
  }

  size_t size() const { return cache_slot_by_ip_.size(); }
  size_t cached_site_count() const { return cache_sites_.size(); }
  bool empty() const { return cache_slot_by_ip_.empty(); }
  XlangVMMonitoringSiteState& monitoring_at(size_t ip) { return monitoring_by_ip_[ip]; }
  void clear_monitoring() {
    std::fill(monitoring_by_ip_.begin(), monitoring_by_ip_.end(),
              XlangVMMonitoringSiteState{});
  }
  iterator begin() { return cache_sites_.begin(); }
  iterator end() { return cache_sites_.end(); }
  const_iterator begin() const { return cache_sites_.begin(); }
  const_iterator end() const { return cache_sites_.end(); }

  XlangVMInstrCache& operator[](size_t ip) {
    assert(ip < cache_slot_by_ip_.size());
    const uint32_t slot = cache_slot_by_ip_[ip];
    assert(slot != kNoCacheSlot);
    return cache_sites_[slot];
  }
  const XlangVMInstrCache& operator[](size_t ip) const {
    assert(ip < cache_slot_by_ip_.size());
    const uint32_t slot = cache_slot_by_ip_[ip];
    assert(slot != kNoCacheSlot);
    return cache_sites_[slot];
  }

 private:
  static constexpr uint32_t kNoCacheSlot = UINT32_MAX;
  std::vector<uint32_t> cache_slot_by_ip_;
  std::vector<XlangVMMonitoringSiteState> monitoring_by_ip_;
  std::vector<XlangVMInstrCache> cache_sites_;
};

enum class FrameReturnMode : uint8_t {
  StoreReturnValue,
  DiscardReturnValue,
  StoreConstructedInstance,
  StoreBoolean,
  StoreNegatedBoolean,
};

enum class ExceptionHandlerKind : uint8_t {
  Except,
  With,
};

struct ExceptionHandler {
  uint32_t ip = 0;
  ExceptionHandlerKind kind = ExceptionHandlerKind::Except;
  uint32_t manager_reg = 0;
};

struct XlangVMUnwind {};
using VMUnwind = XlangVMUnwind;

struct XlangVMPreparedFunctionState {
  std::shared_ptr<const ir::Module> module_owner;
  XlangVMInstrCacheStorage instr_cache;
  uint64_t monitoring_configuration_generation = 0;
  int64_t monitoring_events = 0;
};

struct XlangVMFrame {
  const ir::Module* module = nullptr;
  const ir::Function* fn = nullptr;
  std::unique_ptr<std::vector<Value>> closure_owner;
  const std::vector<Value>* closure = nullptr;
  Value globals_module;
  // Own the namespace selected at entry. Replacing globals['__builtins__']
  // later must not change an active frame or a function's captured namespace.
  // Only dict-backed execution needs this additional ownership; module frames
  // keep their existing fast path and incur no additional object refcounts.
  Value captured_builtins;
  std::shared_ptr<const ir::Module> module_owner;
  uint32_t function_id = 0;
  uint64_t activation_id = 0;
  GeneratorObject* coroutine_owner = nullptr;
  GeneratorObject* inline_coroutine_parent = nullptr;
  bool inline_coroutine_entry = false;
  uint32_t return_dst = 0;
  bool has_caller = false;
  FrameReturnMode return_mode = FrameReturnMode::StoreReturnValue;
  Value continuation_value;
  Value monitoring_code;
  Value trace_function;
  Value trace_frame_object;
  // Function definitions in one module repeatedly capture the same builtin
  // binding; the dedicated module version invalidates this across rebinding.
  ModuleObject* function_builtins_cache_module = nullptr;
  uint64_t function_builtins_cache_version = 0;
  Value function_builtins_cache;
  size_t ip = 0;
  uint32_t last_trace_line = 0;
  uint32_t last_monitoring_line = 0;
  uint32_t last_debug_line = 0;
  uint64_t monitoring_configuration_generation = 0;
  int64_t monitoring_events = 0;
  bool monitoring_cache_touched = false;
  bool trace_call_emitted = false;
  bool trace_lines = true;
  bool trace_opcodes = false;

  XlangVMSmallValueBuffer locals;
  XlangVMSmallValueBuffer cells;
  XlangVMSmallRegisterBuffer regs;
  XlangVMTempArena temps;

  std::vector<ExceptionHandler> exception_handlers;
  XlangVMInstrCacheStorage instr_cache;
  std::shared_ptr<const ir::FunctionExecutionMetadata> execution_metadata;
  std::vector<uint32_t> memoryview_registers;
  std::vector<bool> memoryview_register_flags;
  std::vector<Value> native_call_args;
  // Reuse per-caller argument-binding storage across calls at this frame depth.
  // The scratch is cleared immediately after the callee copies its bound locals.
  std::vector<Value> call_binding_scratch;
  std::unordered_map<const ir::Function*, XlangVMPreparedFunctionState> prepared_functions;

  void set_closure(const std::vector<Value>& frame_closure) {
    if (frame_closure.empty()) {
      closure_owner.reset();
      static const std::vector<Value> empty_closure;
      closure = &empty_closure;
    } else {
      closure_owner = std::make_unique<std::vector<Value>>(frame_closure);
      closure = closure_owner.get();
    }
  }

  void capture_execution_builtins(CallArgsView args) {
    value_set_invalid(captured_builtins);
    if (!mapping_is_dict(globals_module)) return;
    if (args.captured_builtins != nullptr && args.captured_builtins->tag != ValueTag::Invalid) {
      value_assign_fast(captured_builtins, *args.captured_builtins);
    } else {
      std::string ignored;
      mapping_get_string_item(globals_module, "__builtins__", captured_builtins, ignored);
    }
  }

  XlangVMFrame(
      const ir::Module& frame_module,
      uint32_t function_id,
      CallArgsView args,
      const std::vector<Value>& frame_closure,
      Value frame_globals_module,
      std::shared_ptr<const ir::Module> frame_module_owner,
      uint32_t frame_return_dst,
      bool frame_has_caller,
      FrameReturnMode frame_return_mode = FrameReturnMode::StoreReturnValue,
      Value frame_continuation_value = Value::invalid(),
      bool frame_args_are_moved = false)
      : module(&frame_module),
        fn(&frame_module.functions[function_id]),
        globals_module(std::move(frame_globals_module)),
        module_owner(std::move(frame_module_owner)),
        function_id(function_id),
        return_dst(frame_return_dst),
        has_caller(frame_has_caller),
        return_mode(frame_return_mode),
        continuation_value(std::move(frame_continuation_value)),
        locals(fn->locals.size(), Value::invalid()),
        cells(fn->cell_slots.size(), Value::invalid()),
        regs(fn->register_count, Value::invalid()) {
    set_closure(frame_closure);
    capture_execution_builtins(args);
    compute_register_last_use();
    instr_cache.reset(fn->code.size(), execution_metadata->cache_cleanup_instructions);
    for (size_t i = 0; i < args.size(); ++i) {
      if (frame_args_are_moved) {
        value_move_assign_fast(locals[i], const_cast<Value&>(args.get(i)));
      } else {
        value_assign_fast(locals[i], args.get(i));
      }
    }
    reserve_call_args();
  }

  void reset(
      const ir::Module& frame_module,
      uint32_t function_id,
      CallArgsView args,
      const std::vector<Value>& frame_closure,
      Value frame_globals_module,
      std::shared_ptr<const ir::Module> frame_module_owner,
      uint32_t frame_return_dst,
      bool frame_has_caller,
      FrameReturnMode frame_return_mode = FrameReturnMode::StoreReturnValue,
      Value frame_continuation_value = Value::invalid(),
      bool frame_args_are_moved = false) {
    const ir::Module* old_module = module;
    const uint32_t old_function_id = this->function_id;
    const ir::Function* old_fn = fn;
    const ir::Function* next_fn = &frame_module.functions[function_id];
    if (old_fn != nullptr && old_fn != next_fn) {
      auto prepared = prepared_functions.find(old_fn);
      if (prepared == prepared_functions.end()) {
        if (prepared_functions.size() >= 32) {
          prepared_functions.erase(prepared_functions.begin());
        }
        prepared = prepared_functions.emplace(old_fn, XlangVMPreparedFunctionState{}).first;
      }
      prepared->second.module_owner = module_owner;
      prepared->second.instr_cache = std::move(instr_cache);
      prepared->second.monitoring_configuration_generation = monitoring_configuration_generation;
      prepared->second.monitoring_events = monitoring_events;
    }
    module = &frame_module;
    fn = next_fn;
    set_closure(frame_closure);
    globals_module = std::move(frame_globals_module);
    capture_execution_builtins(args);
    module_owner = std::move(frame_module_owner);
    this->function_id = function_id;
    coroutine_owner = nullptr;
    inline_coroutine_parent = nullptr;
    inline_coroutine_entry = false;
    return_dst = frame_return_dst;
    has_caller = frame_has_caller;
    return_mode = frame_return_mode;
    continuation_value = std::move(frame_continuation_value);
    if (old_module != &frame_module || old_function_id != function_id) {
      value_set_invalid(monitoring_code);
    }
    value_set_invalid(trace_function);
    value_set_invalid(trace_frame_object);
    function_builtins_cache_module = nullptr;
    function_builtins_cache_version = 0;
    value_set_invalid(function_builtins_cache);
    ip = 0;
    last_trace_line = 0;
    last_monitoring_line = 0;
    last_debug_line = 0;
    if (old_fn != fn) {
      monitoring_configuration_generation = 0;
      monitoring_events = 0;
    }
    trace_call_emitted = false;
    trace_lines = true;
    trace_opcodes = false;

    locals.reset_after_clear(fn->locals.size(), Value::invalid());
    cells.reset_after_clear(fn->cell_slots.size(), Value::invalid());
    regs.reset_after_clear(fn->register_count, Value::invalid());
    temps.clear();
    exception_handlers.clear();
    native_call_args.clear();
    call_binding_scratch.clear();
    memoryview_registers.clear();
    memoryview_register_flags.clear();

    if (old_fn != fn) {
      auto prepared = prepared_functions.find(fn);
      if (prepared != prepared_functions.end()) {
        instr_cache = std::move(prepared->second.instr_cache);
        monitoring_configuration_generation = prepared->second.monitoring_configuration_generation;
        monitoring_events = prepared->second.monitoring_events;
      }
      compute_register_last_use();
      if (prepared == prepared_functions.end())
        instr_cache.reset(fn->code.size(), execution_metadata->cache_cleanup_instructions);
      reserve_call_args();
    }

    for (size_t i = 0; i < args.size(); ++i) {
      if (frame_args_are_moved) {
        value_move_assign_fast(locals[i], const_cast<Value&>(args.get(i)));
      } else {
        value_assign_fast(locals[i], args.get(i));
      }
    }
  }

  void clear_for_pop() {
    value_set_invalid(captured_builtins);
    value_set_invalid(globals_module);
    value_set_invalid(continuation_value);
    coroutine_owner = nullptr;
    inline_coroutine_parent = nullptr;
    inline_coroutine_entry = false;
    // Inline caches must not extend the lifetime of Python objects after the
    // frame returns. CPython's adaptive caches are non-owning; XLang3 cache
    // entries currently contain owning Values, so discard owning payloads while
    // retaining the allocated cache vector for the next activation. Function
    // metadata lists only IR sites that can own cache state, avoiding a scan of
    // every instruction on each Python return in call-heavy workloads.
    const auto* cache_sites = execution_metadata == nullptr || monitoring_cache_touched
        ? nullptr : &execution_metadata->cache_cleanup_instructions;
    if (cache_sites != nullptr) {
      for (uint32_t index : *cache_sites) {
        if (index < instr_cache.size()) clear_cache_if_owned(instr_cache[index]);
      }
    } else {
      for (auto& cache : instr_cache) clear_cache_if_owned(cache);
    }
    if (monitoring_cache_touched) instr_cache.clear_monitoring();
    monitoring_cache_touched = false;
    value_set_invalid(trace_function);
    value_set_invalid(trace_frame_object);
    closure = nullptr;
    locals.clear_values();
    cells.clear_values();
    regs.clear_object_values();
    temps.clear();
    exception_handlers.clear();
    native_call_args.clear();
    memoryview_registers.clear();
    memoryview_register_flags.clear();
  }

private:
  static void clear_cache_if_owned(XlangVMInstrCache& cache) {
    // Fused global-then-attribute/call sites can write two payloads: the last
    // adaptive domain is not a complete ownership mask. Clear their global
    // payload first, matching the old whole-record release order, then clear
    // the active attribute/call payload. Clearing unrelated payloads in all
    // 424 bytes on every Python return adds needless work. Keep cache writers
    // aligned with this ownership rule and instruction_may_own_inline_cache.
    // See doc/performance/vm-cache-domain-cleanup-20260930.md for the official
    // benchmark and complete Release gate evidence behind this design.
    const XlangVMCacheDomain domain = cache.domain;
    if (domain != XlangVMCacheDomain::GetItem && domain != XlangVMCacheDomain::Len) {
      xlang_vm_cache_clear(cache);
    }
    switch (domain) {
      case XlangVMCacheDomain::Global:
        cache.global = GlobalSiteCache{};
        break;
      case XlangVMCacheDomain::Attr: {
        cache.global = GlobalSiteCache{};
        // CPython keeps guarded attribute caches with the code object. Keep
        // XLang3's equally non-owning index caches warm across frame reuse;
        // globally unique class versions prevent a freed class at a reused
        // address from satisfying an old guard. Descriptor caches own Values
        // and still reset below so a cache cannot extend Python object life.
        const AttrSiteKind kind = cache.attr.kind;
        const bool keep = kind == AttrSiteKind::InstanceAttr ||
            kind == AttrSiteKind::InstanceDict ||
            kind == AttrSiteKind::InstanceSlot ||
            kind == AttrSiteKind::ClassValue;
        // These guarded records already survive return. If their owning
        // fields are empty, retain them in place instead of zeroing the whole
        // payload and reconstructing its scalar guards at every activation.
        // Global owners above and monitoring masks below still clear on pop.
        if (keep && cache.attr.value.tag == ValueTag::Invalid &&
            cache.attr.getter_const.tag == ValueTag::Invalid &&
            cache.attr.setter_const.tag == ValueTag::Invalid &&
            cache.attr.deleter_const.tag == ValueTag::Invalid) {
          break;
        }
        const uint32_t index = cache.attr.index;
        Object* owner = cache.attr.owner;
        const uint64_t version = cache.attr.version;
        const uint64_t secondary_version = cache.attr.secondary_version;
        const Value* class_value = cache.attr.class_value;
        cache.attr = AttrSiteCache{};
        if (keep) {
          cache.attr.index = index;
          cache.attr.kind = kind;
          cache.attr.owner = owner;
          cache.attr.version = version;
          if (kind == AttrSiteKind::ClassValue) {
            cache.attr.secondary_version = secondary_version;
            cache.attr.class_value = class_value;
          }
        }
        break;
      }
      case XlangVMCacheDomain::Call:
        cache.global = GlobalSiteCache{};
        cache.call = CallSiteCache{};
        break;
      case XlangVMCacheDomain::CallMethod: {
        cache.global = GlobalSiteCache{};
        // The common method cache points into the receiver class's attrs.
        // That class owns the function, and its process-wide version tag
        // invalidates the raw pointer on mutation or class destruction. Copy
        // only scalar guards: retained_callee, inline_const, and vectors stay
        // empty, preserving CPython-like code-site warming without rooting
        // otherwise-dead Python objects.
        const CallSiteCache& old = cache.call;
        const bool keep = old.kind == CallSiteKind::UserFunction ||
            old.kind == CallSiteKind::ExactPositionalFunction ||
            old.kind == CallSiteKind::NativeFunction ||
            old.kind == CallSiteKind::InlineSelfSlotMaximizeMethod ||
            old.kind == CallSiteKind::InlineSelfAttrBooleanExprMethod ||
            old.kind == CallSiteKind::InlineSelfAttrBinaryMethod ||
            old.kind == CallSiteKind::InlineSelfBinaryMethod ||
            old.kind == CallSiteKind::InlineSelfSlotMethod ||
            old.kind == CallSiteKind::InlineSmallSelfMethod ||
            old.kind == CallSiteKind::BuiltinMethodSpec;
        // A class-version guard permits these raw method targets to remain
        // warm. Avoid resetting and rebuilding an already ownerless payload;
        // any retained callable, constant, or vector uses the release path.
        if (keep && old.retained_callee.tag == ValueTag::Invalid &&
            old.inline_const.tag == ValueTag::Invalid &&
            old.slot_constructor_args.empty() && old.cached_values.empty()) {
          break;
        }
        const CallSiteKind kind = old.kind;
        Object* callee_object = old.callee_object;
        FunctionObject* function = old.function;
        NativeFunctionObject* native = old.native;
        const BuiltinMethodSpec* builtin_method = old.builtin_method;
        const uint64_t class_version = old.class_version;
        const uint32_t lhs_slot = old.lhs_slot;
        const uint32_t rhs_slot = old.rhs_slot;
        const std::array<uint32_t, 3> inline_slots = old.inline_slots;
        const uint32_t fast_method_id = old.fast_method_id;
        const ir::Op inline_op = old.inline_op;
        cache.call = CallSiteCache{};
        if (keep) {
          auto& retained = cache.call;
          retained.kind = kind;
          retained.callee_object = callee_object;
          retained.function = function;
          retained.native = native;
          retained.builtin_method = builtin_method;
          retained.class_version = class_version;
          retained.lhs_slot = lhs_slot;
          retained.rhs_slot = rhs_slot;
          retained.inline_slots = inline_slots;
          retained.fast_method_id = fast_method_id;
          retained.inline_op = inline_op;
        }
        break;
      }
      case XlangVMCacheDomain::GetItem:
        // GetItem's Python-method guard has raw class/function pointers; those
        // must not survive return even though its scalar core can be reused.
        cache.call = CallSiteCache{};
        break;
      case XlangVMCacheDomain::Len:
      case XlangVMCacheDomain::Empty:
        break;
      default:
        cache = XlangVMInstrCache{};
        return;
    }
    // Len/GetItem's core owns no objects and validates the current operand
    // kind on every hit, so short activations can accumulate specialization.
  }

  static bool instruction_may_own_inline_cache(ir::Op op) {
    // Keep this set aligned with every xlang_vm_cache_touch() callsite and
    // include fused IR ops that delegate to those cached handlers.
    switch (op) {
      case ir::Op::LoadModuleSlot:
      case ir::Op::LoadGlobal:
      case ir::Op::LoadLocalGlobal:
      case ir::Op::LoadGlobalLocal:
      case ir::Op::StoreGlobal:
      case ir::Op::LoadAttr:
      case ir::Op::LoadLocalAttr:
      case ir::Op::LoadModuleAttr:
      case ir::Op::StoreAttr:
      case ir::Op::DeleteAttr:
      case ir::Op::Len:
      case ir::Op::GetItem:
      // SetItem now resolves guarded Python/native setters. Sparse storage
      // must allocate its payload before the opcode handler indexes the cache.
      case ir::Op::SetItem:
      case ir::Op::LoadLocalGetItem:
      case ir::Op::GetItemConst:
      case ir::Op::Call:
      case ir::Op::CallLocal:
      case ir::Op::CallGlobal:
      case ir::Op::CallEx:
      case ir::Op::CallMethod:
      case ir::Op::CallMethodEx:
      case ir::Op::CallLocalMethod:
      case ir::Op::CallModuleMethod:
        return true;
      default:
        return false;
    }
  }

  template <typename Fn>
  void for_each_register_read(const ir::Instr& instr, Fn&& fn) const {
    auto one = [&](uint32_t reg) {
      if (reg != UINT32_MAX) {
        fn(reg);
      }
    };
    auto list = [&](const std::vector<uint32_t>& regs) {
      for (uint32_t reg : regs) {
        one(reg);
      }
    };
    auto call_args = [&](uint32_t index) {
      if (index < this->fn->call_args.size()) {
        list(this->fn->call_args[index]);
      }
    };

    switch (instr.op) {
      case ir::Op::Move:
      case ir::Op::StoreLocal:
      case ir::Op::StoreLocalLoadLocal:
      case ir::Op::StoreCell:
      case ir::Op::StoreFree:
      case ir::Op::StoreModuleSlot:
      case ir::Op::StoreGlobal:
      case ir::Op::Len:
      case ir::Op::CaptureExpressions:
      case ir::Op::GetIter:
      case ir::Op::IterNext:
      case ir::Op::IterNextLocal:
      case ir::Op::Not:
      case ir::Op::NotJumpIfFalse:
      case ir::Op::Neg:
      case ir::Op::Invert:
      case ir::Op::JumpIfFalse:
      case ir::Op::MoveJumpIfFalse:
      case ir::Op::MoveJumpIfTrue:
      case ir::Op::JumpIfFalseLoadLocal:
      case ir::Op::Raise:
      case ir::Op::SetExceptionCause:
      case ir::Op::SetException:
      case ir::Op::MatchException:
      case ir::Op::Yield:
      case ir::Op::Return:
      case ir::Op::Await:
      case ir::Op::Pop:
        one(instr.a);
        break;
      case ir::Op::YieldFrom:
        one(instr.a);
        one(instr.b);
        one(instr.c);
        break;
      case ir::Op::LoadAttr:
      case ir::Op::LoadInstanceSlot:
      case ir::Op::TupleFromList:
        one(instr.a);
        break;
      case ir::Op::StoreLocalPair:
        one(instr.a);
        one(instr.c);
        break;
      case ir::Op::LoadLocalAttr:
        one(instr.c);
        break;
      case ir::Op::LoadModuleAttr:
        one(instr.c);
        break;
      case ir::Op::LoadLocalGetItem:
        one(instr.b);
        one(instr.c);
        break;
      case ir::Op::GetItemConst:
        one(instr.a);
        break;
      case ir::Op::DictSetConst:
        one(instr.a);
        break;
      case ir::Op::StoreLocalInstanceSlot:
        one(instr.b);
        break;
      case ir::Op::StoreAttr:
      case ir::Op::StoreInstanceSlot:
      case ir::Op::SetItem:
        one(instr.dst);
        one(instr.a);
        one(instr.b);
        break;
      case ir::Op::DeleteAttr:
      case ir::Op::DeleteItem:
      case ir::Op::ListAppend:
      case ir::Op::ListExtend:
      case ir::Op::SetAdd:
      case ir::Op::SetUpdate:
      case ir::Op::GetItem:
      case ir::Op::Add:
      case ir::Op::InplaceAdd:
      case ir::Op::Sub:
      case ir::Op::Mul:
      case ir::Op::MatMul:
      case ir::Op::Div:
      case ir::Op::FloorDiv:
      case ir::Op::Mod:
      case ir::Op::ModConst:
      case ir::Op::Pow:
      case ir::Op::BitAnd:
      case ir::Op::BitOr:
      case ir::Op::BitXor:
      case ir::Op::Shl:
      case ir::Op::Shr:
      case ir::Op::BoolAnd:
      case ir::Op::BoolOr:
      case ir::Op::Compare:
      case ir::Op::CompareJumpIfFalse:
      case ir::Op::Is:
      case ir::Op::IsJumpIfFalse:
      case ir::Op::Contains:
        one(instr.a);
        one(instr.b);
        break;
      case ir::Op::DictSet:
        one(instr.dst);
        one(instr.a);
        one(instr.b);
        break;
      case ir::Op::MakeSlice:
        one(instr.a);
        one(instr.b);
        one(instr.c);
        break;
      case ir::Op::UnpackSequence:
        one(instr.a);
        break;
      case ir::Op::ImportModuleThru:
        one(instr.b);
        break;
      case ir::Op::Call:
        one(instr.a);
        call_args(instr.b);
        break;
      case ir::Op::CallLocal:
      case ir::Op::CallGlobal:
        call_args(instr.b);
        break;
      case ir::Op::CallMethod:
        one(instr.a);
        call_args(instr.c);
        break;
      case ir::Op::CallMethodEx:
        one(instr.a);
        if (instr.c < this->fn->call_specs.size()) {
          const auto& spec = this->fn->call_specs[instr.c];
          list(spec.positional);
          for (const auto& keyword : spec.keywords) one(keyword.value_reg);
        }
        break;
      case ir::Op::CallLocalMethod:
        call_args(instr.c);
        break;
      case ir::Op::CallModuleMethod:
        call_args(instr.c);
        break;
      case ir::Op::CallEx:
        one(instr.a);
        if (instr.b < this->fn->call_specs.size()) {
          const auto& spec = this->fn->call_specs[instr.b];
          list(spec.positional);
          for (const auto& keyword : spec.keywords) {
            one(keyword.value_reg);
          }
          one(spec.star_arg);
          one(spec.kw_star_arg);
          list(spec.star_args);
          list(spec.kw_star_args);
        }
        break;
      case ir::Op::MakeFunction:
        if (instr.b < this->fn->function_closures.size()) {
          list(this->fn->function_closures[instr.b]);
        }
        if (instr.c < this->fn->function_defaults.size()) {
          list(this->fn->function_defaults[instr.c]);
        }
        break;
      case ir::Op::SetFunctionAnnotations:
        one(instr.dst);
        if (instr.b < this->fn->function_annotations.size()) {
          for (const auto& annotation : this->fn->function_annotations[instr.b]) {
            one(annotation.second);
          }
        }
        break;
      case ir::Op::SetFunctionKwDefaults:
        one(instr.dst);
        if (instr.a < this->fn->function_kwdefaults.size()) {
          for (const auto& item : this->fn->function_kwdefaults[instr.a]) {
            one(item.second);
          }
        }
        break;
      case ir::Op::SetClassBase:
        one(instr.dst);
        one(instr.a);
        break;
      case ir::Op::MakeTuple:
        if (instr.a < this->fn->tuple_items.size()) {
          list(this->fn->tuple_items[instr.a]);
        }
        break;
      case ir::Op::MakeList:
        if (instr.a < this->fn->list_items.size()) {
          list(this->fn->list_items[instr.a]);
        }
        break;
      case ir::Op::MakeSet:
        if (instr.a < this->fn->set_items.size()) {
          list(this->fn->set_items[instr.a]);
        }
        break;
      case ir::Op::MakeDict:
        if (instr.a < this->fn->dict_items.size()) {
          for (const auto& item : this->fn->dict_items[instr.a]) {
            one(item.first);
            one(item.second);
          }
        }
        break;
      case ir::Op::MakeClass:
        if (instr.b < this->fn->class_attrs.size()) {
          for (const auto& attr : this->fn->class_attrs[instr.b]) {
            one(attr.second);
          }
        }
        break;
      default:
        break;
    }
  }

  void compute_register_last_use() {
    auto prepared = std::atomic_load_explicit(
        &fn->execution_metadata, std::memory_order_acquire);
    if (prepared != nullptr && prepared->owner == fn) {
      execution_metadata = std::move(prepared);
      return;
    }
    auto computed = std::make_shared<ir::FunctionExecutionMetadata>();
    computed->owner = fn;
    computed->cache_cleanup_instructions.reserve(fn->code.size() / 8);
    for (size_t ip = 0; ip < fn->code.size(); ++ip) {
      if (instruction_may_own_inline_cache(fn->code[ip].op)) {
        computed->cache_cleanup_instructions.push_back(static_cast<uint32_t>(ip));
      }
    }
    computed->register_last_use.assign(
        fn->register_count, std::numeric_limits<size_t>::max());
    computed->register_loop_carried.assign(fn->register_count, false);
    for (size_t i = 0; i < fn->code.size(); ++i) {
      for_each_register_read(fn->code[i], [&](uint32_t reg) {
        if (reg < computed->register_last_use.size()) {
          computed->register_last_use[reg] = i;
        }
      });
    }
    // Values read in a loop may be needed again after a backward edge. Keep
    // those registers for the frame lifetime; the linear last-use index alone
    // cannot express loop-carried liveness.
    for (size_t i = 0; i < fn->code.size(); ++i) {
      const auto& instr = fn->code[i];
      if ((instr.op != ir::Op::Jump && instr.op != ir::Op::JumpIfFalse) ||
          instr.dst >= i) {
        continue;
      }
      // Call results and container literals produced inside a loop are
      // overwritten on every iteration, so their registers are not roots
      // carried into the next iteration.
      std::vector<bool> temporary_defined_in_loop(fn->register_count, false);
      for (size_t loop_ip = instr.dst; loop_ip <= i; ++loop_ip) {
        const auto& loop_instr = fn->code[loop_ip];
        switch (loop_instr.op) {
          case ir::Op::Call:
          case ir::Op::CallEx:
          case ir::Op::CallMethod:
          case ir::Op::CallMethodEx:
          case ir::Op::CallLocal:
          case ir::Op::CallLocalMethod:
          case ir::Op::CallModuleMethod:
          case ir::Op::CallGlobal:
          case ir::Op::MakeDict:
          case ir::Op::MakeList:
          case ir::Op::MakeSet:
          case ir::Op::MakeTuple:
            if (loop_instr.dst < temporary_defined_in_loop.size())
              temporary_defined_in_loop[loop_instr.dst] = true;
            break;
          default:
            break;
        }
      }
      for (size_t loop_ip = instr.dst; loop_ip <= i; ++loop_ip) {
        for_each_register_read(fn->code[loop_ip], [&](uint32_t reg) {
          // LoadModuleAttr writes its receiver scratch register from the live
          // module slot before reading it internally. That read is not a root
          // carried from the previous iteration. Keep its linear last-use for
          // cleanup, and let any later external read establish real liveness.
          // Otherwise `del obj` in a loop retains obj in this fused temporary.
          const auto& loop_instr = fn->code[loop_ip];
          if (loop_instr.op == ir::Op::LoadModuleAttr && reg == loop_instr.c) return;
          if (reg < computed->register_last_use.size() &&
              !temporary_defined_in_loop[reg]) {
            computed->register_loop_carried[reg] = true;
            computed->register_last_use[reg] = std::numeric_limits<size_t>::max();
          }
        });
      }
    }
    // Function copies inherit the source's cache pointer. Replace metadata
    // whose owner is a different Function object instead of using stale
    // register indices from that source object.
    std::shared_ptr<const ir::FunctionExecutionMetadata> expected = prepared;
    std::shared_ptr<const ir::FunctionExecutionMetadata> immutable = std::move(computed);
    if (!std::atomic_compare_exchange_strong_explicit(
            &fn->execution_metadata,
            &expected,
            immutable,
            std::memory_order_release,
            std::memory_order_acquire)) {
      if (expected != nullptr && expected->owner == fn) {
        execution_metadata = std::move(expected);
      } else {
        compute_register_last_use();
      }
    } else {
      execution_metadata = std::move(immutable);
    }
  }

public:
  void release_memoryviews_last_used_at(size_t instruction_index) {
    if (memoryview_registers.empty()) return;
    const auto& register_last_use = execution_metadata->register_last_use;
    const auto& register_loop_carried = execution_metadata->register_loop_carried;
    for (size_t index = 0; index < memoryview_registers.size();) {
      const uint32_t reg = memoryview_registers[index];
      const bool release = reg >= regs.size() || value_as_memoryview(regs[reg]) == nullptr ||
          (reg < register_last_use.size() && register_last_use[reg] == instruction_index &&
           (reg >= register_loop_carried.size() || !register_loop_carried[reg]));
      if (!release) {
        ++index;
        continue;
      }
      if (reg < regs.size() && value_as_memoryview(regs[reg]) != nullptr) {
        value_set_invalid(regs[reg]);
      }
      if (reg < memoryview_register_flags.size()) memoryview_register_flags[reg] = false;
      memoryview_registers[index] = memoryview_registers.back();
      memoryview_registers.pop_back();
    }
  }

  // Guarded local arithmetic may skip its ordinary register-based fallback.
  // Drop memoryviews left in registers written only by skipped instructions,
  // so an earlier fallback iteration cannot extend a buffer lease.
  void release_memoryviews_for_skipped_local_add(
      const ir::Function& function, size_t first_instruction, size_t span) {
    if (memoryview_registers.empty()) return;
    const size_t end = std::min(function.code.size(), first_instruction + span);
    for (size_t index = first_instruction; index < end; ++index) {
      const auto& instr = function.code[index];
      switch (instr.op) {
        case ir::Op::LoadConst:
        case ir::Op::LoadLocal:
        case ir::Op::Add:
        case ir::Op::Sub:
        case ir::Op::Mul:
          release_memoryview_register(instr.dst);
          break;
        case ir::Op::LoadLocalPair:
        case ir::Op::LoadLocalConst:
          release_memoryview_register(instr.dst);
          release_memoryview_register(instr.b);
          break;
        case ir::Op::LoadConstPair:
          release_memoryview_register(instr.dst);
          release_memoryview_register(instr.b);
          break;
        default:
          break;
      }
    }
  }

  // Guarded local operations can skip a small, side-effect-free expression
  // fallback. Clear its temporary results so an earlier guard miss cannot
  // keep a materialized slice or other temporary object alive.
  void release_registers_for_skipped_expression_fallback(
      const ir::Function& function,
      size_t first_instruction,
      size_t span,
      uint32_t preserved_register = UINT32_MAX) {
    const size_t end = std::min(function.code.size(), first_instruction + span);
    auto clear = [&](uint32_t reg) {
      if (reg >= regs.size() || reg == preserved_register) return;
      release_memoryview_register(reg);
      value_set_invalid(regs[reg]);
    };
    for (size_t index = first_instruction; index < end; ++index) {
      const auto& instr = function.code[index];
      switch (instr.op) {
        case ir::Op::LoadConst:
        case ir::Op::LoadLocal:
        case ir::Op::Add:
        case ir::Op::Sub:
        case ir::Op::Neg:
        case ir::Op::MakeSlice:
        case ir::Op::GetItem:
        case ir::Op::Compare:
        case ir::Op::Call:
        case ir::Op::CallLocal:
        case ir::Op::CallGlobal:
        case ir::Op::CallMethod:
        case ir::Op::CallLocalMethod:
        case ir::Op::CallEx:
        case ir::Op::CallMethodEx:
        case ir::Op::CallModuleMethod:
          clear(instr.dst);
          break;
        case ir::Op::GetItemConst:
          clear(instr.dst);
          clear(instr.c);
          break;
        case ir::Op::GuardedLocalListGetItem:
          clear(instr.dst);
          break;
        case ir::Op::LoadLocalPair:
        case ir::Op::LoadLocalConst:
        case ir::Op::LoadConstPair:
          clear(instr.dst);
          clear(instr.b);
          break;
        case ir::Op::LoadLocalGetItem:
          clear(instr.dst);
          clear(instr.c);
          break;
        default:
          break;
      }
    }
  }

  void track_memoryview_result(uint32_t reg) {
    if (reg >= regs.size() || value_as_memoryview(regs[reg]) == nullptr) return;
    if (memoryview_register_flags.empty()) {
      memoryview_register_flags.assign(regs.size(), false);
    }
    if (!memoryview_register_flags[reg]) {
      memoryview_register_flags[reg] = true;
      memoryview_registers.push_back(reg);
    }
  }

private:
  void release_memoryview_register(uint32_t reg) {
    if (reg >= memoryview_register_flags.size() || !memoryview_register_flags[reg]) return;
    if (reg < regs.size() && value_as_memoryview(regs[reg]) != nullptr) {
      value_set_invalid(regs[reg]);
    }
    memoryview_register_flags[reg] = false;
    for (size_t index = 0; index < memoryview_registers.size(); ++index) {
      if (memoryview_registers[index] == reg) {
        memoryview_registers[index] = memoryview_registers.back();
        memoryview_registers.pop_back();
        break;
      }
    }
  }

  void reserve_call_args() {
    uint32_t max_call_arg_count = 0;
    for (const auto& arg_regs : fn->call_args) {
      if (arg_regs.size() > max_call_arg_count) {
        max_call_arg_count = static_cast<uint32_t>(arg_regs.size());
      }
    }
    if (max_call_arg_count != 0) {
      native_call_args.reserve(static_cast<size_t>(max_call_arg_count) + 1);
    }
  }
};

using VMFrame = XlangVMFrame;

} // namespace xlang3
