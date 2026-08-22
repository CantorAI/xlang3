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

#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

#include <string>
#include <utility>
#include <vector>

namespace xlang3::xlang_vm::ops {

XLANG3_HOT_INLINE XlangVMOpFlow jump(const ir::Instr& in, size_t& ip) {
  ip = in.dst;
  return XlangVMOpFlow::ContinueLoop;
}

XLANG3_HOT_INLINE XlangVMOpFlow jump_if_false(
    const ir::Instr& in,
    const ir::Module& module,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip) {
  if (!xlang_vm_truthy(module, regs[in.a])) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow jump_if_local_const_false(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (in.a >= locals.size() || in.b >= fn.constants.size()) {
    result.errors.push_back("invalid local const jump");
    return XlangVMOpFlow::ReturnResult;
  }
  Value compare_result;
  const auto op = static_cast<ir::CompareOp>(in.c);
  if (!fast_compare(op, locals[in.a], fn.constants[in.b], compare_result)) {
    std::string error;
    if (!value_compare(compare_name(op), locals[in.a], fn.constants[in.b], compare_result, error)) {
      return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  }
  if (!value_truthy(compare_result)) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

XLANG3_HOT_INLINE void setup_except(
    const ir::Instr& in,
    std::vector<ExceptionHandler>& exception_handlers) {
  exception_handlers.push_back({in.dst, ExceptionHandlerKind::Except, 0});
}

XLANG3_HOT_INLINE void setup_with(
    const ir::Instr& in,
    std::vector<ExceptionHandler>& exception_handlers) {
  exception_handlers.push_back({in.dst, ExceptionHandlerKind::With, in.a});
}

XLANG3_HOT_INLINE XlangVMOpFlow pop_except(
    std::vector<ExceptionHandler>& exception_handlers,
    RuntimeResult& result) {
  if (exception_handlers.empty()) {
    result.errors.push_back("invalid exception handler pop");
    return XlangVMOpFlow::ReturnResult;
  }
  exception_handlers.pop_back();
  return XlangVMOpFlow::Next;
}

template <typename RaiseExceptionValue, typename NormalizeException>
XLANG3_HOT_INLINE XlangVMOpFlow raise_op(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    Value& pending_cause,
    bool& pending_explicit_cause,
    Value& current_exception,
    RuntimeResult& result,
    RaiseExceptionValue&& raise_exception_value,
    NormalizeException&& normalize_exception) {
  if (in.a >= regs.size()) {
    result.errors.push_back("invalid raise value");
    return XlangVMOpFlow::ReturnResult;
  }
  Value exception = normalize_exception(regs[in.a]);
  if (value_as_instance(exception) != nullptr) {
    std::string ignored;
    if (pending_explicit_cause) {
      object_set_attr(exception, "__cause__", pending_cause, ignored);
      if (current_exception.tag != ValueTag::Invalid) {
        object_set_attr(exception, "__context__", current_exception, ignored);
      }
      object_set_attr(exception, "__suppress_context__", Value::boolean(true), ignored);
    } else if (current_exception.tag != ValueTag::Invalid) {
      object_set_attr(exception, "__context__", current_exception, ignored);
      object_set_attr(exception, "__suppress_context__", Value::boolean(false), ignored);
    }
  }
  value_set_invalid(pending_cause);
  pending_explicit_cause = false;
  return raise_exception_value(std::move(exception)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

template <typename NormalizeException>
XLANG3_HOT_INLINE XlangVMOpFlow set_exception_cause(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    Value& pending_cause,
    bool& pending_explicit_cause,
    RuntimeResult& result,
    NormalizeException&& normalize_exception) {
  if (in.a >= regs.size()) {
    result.errors.push_back("invalid exception cause");
    return XlangVMOpFlow::ReturnResult;
  }
  if (regs[in.a].tag == ValueTag::None) {
    value_set_none(pending_cause);
  } else {
    pending_cause = normalize_exception(regs[in.a]);
  }
  pending_explicit_cause = true;
  return XlangVMOpFlow::Next;
}

template <typename RaiseExceptionValue, typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow reraise(
    Value& current_exception,
    RaiseExceptionValue&& raise_exception_value,
    RaiseRuntimeError&& raise_runtime_error) {
  if (current_exception.tag == ValueTag::Invalid) {
    return raise_runtime_error("No active exception to reraise") ? XlangVMOpFlow::ContinueLoop
                                                                : XlangVMOpFlow::ReturnResult;
  }
  return raise_exception_value(current_exception) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

XLANG3_HOT_INLINE void clear_exception(Runtime& runtime, Value& current_exception) {
  value_set_invalid(current_exception);
  runtime.clear_active_exception();
}

XLANG3_HOT_INLINE void load_exception(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    const Value& current_exception) {
  value_assign_fast(regs[in.dst], current_exception);
}

XLANG3_HOT_INLINE void load_exception_type(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    const Value& current_exception) {
  regs[in.dst] = runtime.exception_type(current_exception);
}

template <typename ExceptionMatches>
XLANG3_HOT_INLINE void match_exception(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    ExceptionMatches&& exception_matches) {
  value_set_bool(regs[in.dst], exception_matches(regs[in.a]));
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow yield_op(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    std::vector<VMFrame>& frames,
    size_t frame_count,
    GeneratorObject* generator,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error) {
  if (generator == nullptr) {
    return raise_runtime_error("yield used outside generator") ? XlangVMOpFlow::ContinueLoop
                                                              : XlangVMOpFlow::ReturnResult;
  }

  Value yielded_value;
  value_assign_fast(yielded_value, regs[in.a]);
  ++ip;

  auto* state = new GeneratorVMState();
  state->frames = std::move(frames);
  state->frame_count = frame_count;
  if (generator->vm_state_cleanup != nullptr && generator->vm_state != nullptr) {
    generator->vm_state_cleanup(generator->vm_state);
  }
  generator->vm_state = state;
  generator->vm_state_cleanup = destroy_generator_vm_state;
  generator->done = false;
  value_assign_fast(result.value, yielded_value);
  return XlangVMOpFlow::ReturnResult;
}

template <typename FinishFrame>
XLANG3_HOT_INLINE XlangVMOpFlow return_op(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    GeneratorObject* generator,
    RuntimeResult& result,
    FinishFrame&& finish_frame) {
  Value return_value;
  value_assign_fast(return_value, regs[in.a]);
  if (!finish_frame(return_value)) {
    if (generator != nullptr) {
      generator->done = true;
      value_set_none(result.value);
    }
    return XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::SwitchFrame;
}

} // namespace xlang3::xlang_vm::ops
