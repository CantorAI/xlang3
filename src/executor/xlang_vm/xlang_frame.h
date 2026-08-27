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
#include "xlang3/runtime.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

/*
XlangVM design note
Author: Shawn Xiong

Purpose:
Frame and per-instruction cache state for XlangVM execution.

Ownership rule:
Instruction caches live on the frame because they are tied to a function's IR
instruction stream. Runtime objects do not own interpreter specialization state.

The frame owns fast execution storage: locals, registers, cells, exception
handlers, and per-site caches. It references ir::Module and ir::Function but
does not own compiled code. XlangVMThread owns the frame stack/vector.
*/

namespace xlang3 {

enum class CallSiteKind : uint8_t {
  Empty,
  UserFunction,
  NativeFunction,
  BoundNativeFunction,
  UserConstructor,
  NativeConstructor,
  InlineSlotConstructor,
  InlineSelfBinaryMethod,
  InlineArgBinaryFunction,
  InlineConstMethod,
  InlineSmallSelfMethod,
  InlineSelfSlotMethod,
  InlineSelfSlotConstSumMethod,
  InlineFastListMethod,
  InlineCachedStringMethod,
  InlineCachedLen,
  BuiltinMethodSpec,
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
  const BuiltinMethodSpec* builtin_method = nullptr;
  NativeFastCallCallback fast_callback = nullptr;
  void* native_user_data = nullptr;
  Object* arg0_object = nullptr;
  Object* arg1_object = nullptr;
  uint64_t class_version = 0;
  uint32_t lhs_slot = 0;
  uint32_t rhs_slot = 0;
  ir::Op inline_op = ir::Op::Add;
  uint32_t inline_function_id = 0;
  uint32_t fast_method_id = 0;
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
  uint32_t function_id = 0;
  uint32_t return_dst = 0;
  bool has_caller = false;
  FrameReturnMode return_mode = FrameReturnMode::StoreReturnValue;
  Value continuation_value;
  Value trace_function;
  size_t ip = 0;
  uint32_t last_trace_line = 0;
  uint32_t last_monitoring_line = 0;
  uint32_t last_debug_line = 0;
  bool trace_call_emitted = false;

  XlangVMSmallValueBuffer locals;
  XlangVMSmallValueBuffer cells;
  XlangVMSmallRegisterBuffer regs;
  XlangVMTempArena temps;

  std::vector<ExceptionHandler> exception_handlers;
  std::vector<XlangVMInstrCache> instr_cache;
  std::vector<size_t> register_last_use;
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
        function_id(function_id),
        return_dst(frame_return_dst),
        has_caller(frame_has_caller),
        return_mode(frame_return_mode),
        continuation_value(std::move(frame_continuation_value)),
        locals(fn->locals.size(), Value::none()),
        cells(fn->cell_slots.size(), Value::invalid()),
        regs(fn->register_count, Value::invalid()),
        instr_cache(fn->code.size()) {
    compute_register_last_use();
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
    this->function_id = function_id;
    return_dst = frame_return_dst;
    has_caller = frame_has_caller;
    return_mode = frame_return_mode;
    continuation_value = std::move(frame_continuation_value);
    value_set_invalid(trace_function);
    ip = 0;
    last_trace_line = 0;
    last_monitoring_line = 0;
    last_debug_line = 0;
    trace_call_emitted = false;

    locals.reset(fn->locals.size(), Value::none());
    cells.reset(fn->cell_slots.size(), Value::invalid());
    regs.reset(fn->register_count, Value::invalid());
    temps.clear();
    exception_handlers.clear();
    native_call_args.clear();

    if (old_fn != fn) {
      instr_cache.assign(fn->code.size(), {});
      compute_register_last_use();
      reserve_call_args();
    }

    for (size_t i = 0; i < args.size(); ++i) {
      value_assign_fast(locals[i], args.get(i));
    }
  }

private:
  void note_register_use(uint32_t reg, size_t instr_index) {
    if (reg < register_last_use.size()) {
      register_last_use[reg] = instr_index;
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
      case ir::Op::StoreCell:
      case ir::Op::StoreFree:
      case ir::Op::StoreModuleSlot:
      case ir::Op::StoreGlobal:
      case ir::Op::Len:
      case ir::Op::GetIter:
      case ir::Op::IterNext:
      case ir::Op::Not:
      case ir::Op::Neg:
      case ir::Op::Invert:
      case ir::Op::JumpIfFalse:
      case ir::Op::Raise:
      case ir::Op::SetExceptionCause:
      case ir::Op::MatchException:
      case ir::Op::Yield:
      case ir::Op::Return:
      case ir::Op::Await:
      case ir::Op::Pop:
        one(instr.a);
        break;
      case ir::Op::LoadAttr:
      case ir::Op::LoadInstanceSlot:
      case ir::Op::TupleFromList:
        one(instr.a);
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
      case ir::Op::Sub:
      case ir::Op::Mul:
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
      case ir::Op::Is:
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
      case ir::Op::Call:
        one(instr.a);
        call_args(instr.b);
        break;
      case ir::Op::CallMethod:
        one(instr.a);
        call_args(instr.c);
        break;
      case ir::Op::CallModuleMethod:
        call_args(instr.c);
        break;
      case ir::Op::CallEx:
        one(instr.a);
        if (instr.c < this->fn->call_specs.size()) {
          const auto& spec = this->fn->call_specs[instr.c];
          list(spec.positional);
          for (const auto& keyword : spec.keywords) {
            one(keyword.value_reg);
          }
          one(spec.star_arg);
          one(spec.kw_star_arg);
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
    register_last_use.assign(fn->register_count, std::numeric_limits<size_t>::max());
    for (size_t i = 0; i < fn->code.size(); ++i) {
      for_each_register_read(fn->code[i], [&](uint32_t reg) {
        note_register_use(reg, i);
      });
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
