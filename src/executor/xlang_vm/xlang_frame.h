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
#include "xlang3/interpreter.h"
#include "xlang3/ir.h"
#include "xlang3/runtime.h"

#include <cstdint>
#include <memory>
#include <vector>

/*
XlangVM design note
Author: Shawn Xiong

Purpose:
Frame and per-instruction cache state for XlangVM execution.

Ownership rule:
Call-site and attribute-site caches live on the frame because they are tied to a
function's IR instruction stream. Runtime objects do not own interpreter
specialization state.

The frame owns fast execution storage: locals, registers, cells, exception
handlers, and per-site caches. It references ir::Module and ir::Function but
does not own compiled code. XlangVMThread owns the frame stack/vector.
*/

namespace xlang3 {

enum class CallSiteKind : uint8_t {
  Empty,
  UserFunction,
  NativeFunction,
  UserConstructor,
  NativeConstructor,
  InlineSlotConstructor,
  InlineSelfBinaryMethod,
  InlineArgBinaryFunction,
  InlineConstMethod,
  InlineSmallSelfMethod,
  InlineSelfSlotMethod,
  InlineSelfSlotConstSumMethod,
  InlineCachedStringMethod,
  InlineCachedLen,
};

enum class AttrSiteKind : uint8_t {
  Empty,
  InstanceAttr,
  InstanceSlot,
  Descriptor,
};

struct CallSiteCache {
  Object* callee_object = nullptr;
  CallSiteKind kind = CallSiteKind::Empty;
  FunctionObject* function = nullptr;
  NativeFunctionObject* native = nullptr;
  NativeFastCallCallback fast_callback = nullptr;
  void* native_user_data = nullptr;
  Object* arg0_object = nullptr;
  Object* arg1_object = nullptr;
  uint64_t class_version = 0;
  uint32_t lhs_slot = 0;
  uint32_t rhs_slot = 0;
  ir::Op inline_op = ir::Op::Add;
  uint32_t inline_function_id = 0;
  uint32_t next_arg = 0;
  ir::Op next_op = ir::Op::Add;
  Value inline_const;
  bool has_next = false;
  bool fast_releases_vm_lock = false;
  std::vector<std::pair<uint32_t, uint32_t>> slot_constructor_args;
  std::vector<Value> cached_values;
};

struct AttrSiteCache {
  uint32_t index = 0;
  AttrSiteKind kind = AttrSiteKind::Empty;
  Object* owner = nullptr;
  uint64_t version = 0;
  Value value;
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

enum class FrameReturnMode : uint8_t {
  StoreReturnValue,
  StoreConstructedInstance,
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

struct XlangVMFrame {
  const ir::Module* module = nullptr;
  const ir::Function* fn = nullptr;
  const std::vector<Value>* closure = nullptr;
  Value globals_module;
  std::shared_ptr<const ir::Module> module_owner;
  uint32_t return_dst = 0;
  bool has_caller = false;
  FrameReturnMode return_mode = FrameReturnMode::StoreReturnValue;
  Value continuation_value;
  size_t ip = 0;

  XlangVMSmallValueBuffer locals;
  XlangVMSmallValueBuffer cells;
  XlangVMSmallRegisterBuffer regs;
  XlangVMTempArena temps;

  std::vector<ExceptionHandler> exception_handlers;
  std::vector<Value> global_value_cache;
  std::vector<uint32_t> global_slot_cache;
  std::vector<uint64_t> global_cache_versions;
  std::vector<uint8_t> global_cache_kind;
  std::vector<CallSiteCache> call_site_cache;
  std::vector<AttrSiteCache> attr_site_cache;
  std::vector<Value> native_call_args;

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
      Value frame_continuation_value = Value::invalid())
      : module(&frame_module),
        fn(&frame_module.functions[function_id]),
        closure(&frame_closure),
        globals_module(std::move(frame_globals_module)),
        module_owner(std::move(frame_module_owner)),
        return_dst(frame_return_dst),
        has_caller(frame_has_caller),
        return_mode(frame_return_mode),
        continuation_value(std::move(frame_continuation_value)),
        locals(fn->locals.size(), Value::none()),
        cells(fn->cell_slots.size(), Value::invalid()),
        regs(fn->register_count, Value::invalid()),
        global_value_cache(fn->names.size(), Value::invalid()),
        global_slot_cache(fn->names.size(), 0),
        global_cache_versions(fn->names.size(), 0),
        global_cache_kind(fn->names.size(), 0),
        attr_site_cache(fn->code.size()) {
    for (size_t i = 0; i < args.size(); ++i) {
      value_assign_fast(locals[i], args.get(i));
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
      Value frame_continuation_value = Value::invalid()) {
    const ir::Function* old_fn = fn;
    module = &frame_module;
    fn = &frame_module.functions[function_id];
    closure = &frame_closure;
    globals_module = std::move(frame_globals_module);
    module_owner = std::move(frame_module_owner);
    return_dst = frame_return_dst;
    has_caller = frame_has_caller;
    return_mode = frame_return_mode;
    continuation_value = std::move(frame_continuation_value);
    ip = 0;

    locals.reset(fn->locals.size(), Value::none());
    cells.reset(fn->cell_slots.size(), Value::invalid());
    regs.reset(fn->register_count, Value::invalid());
    temps.clear();
    exception_handlers.clear();
    native_call_args.clear();

    if (old_fn != fn) {
      global_value_cache.assign(fn->names.size(), Value::invalid());
      global_slot_cache.assign(fn->names.size(), 0);
      global_cache_versions.assign(fn->names.size(), 0);
      global_cache_kind.assign(fn->names.size(), 0);
      call_site_cache.clear();
      attr_site_cache.assign(fn->code.size(), {});
      reserve_call_args();
    }

    for (size_t i = 0; i < args.size(); ++i) {
      value_assign_fast(locals[i], args.get(i));
    }
  }

private:
  void reserve_call_args() {
    uint32_t max_call_arg_count = 0;
    for (const auto& arg_regs : fn->call_args) {
      if (arg_regs.size() > max_call_arg_count) {
        max_call_arg_count = static_cast<uint32_t>(arg_regs.size());
      }
    }
    if (max_call_arg_count != 0) {
      native_call_args.reserve(static_cast<size_t>(max_call_arg_count) + 1);
      call_site_cache.resize(fn->code.size());
    }
  }
};

using VMFrame = XlangVMFrame;

} // namespace xlang3
