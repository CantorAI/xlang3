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
#include "../xlang_vm_inline_support.h"
#include "../xlang_vm_op_switch.h"
#include "xlang_vm_ops_iteration.h"

#include "xlang3/builtins.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

#include <string>
#include <utility>
#include <vector>

namespace xlang3::xlang_vm::ops {

template <typename EmitMonitoringEvent, typename RaiseRuntimeError,
          typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow jump(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    bool collapse_unobservable_chain,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  uint32_t target = in.dst;
  if (collapse_unobservable_chain) {
    size_t remaining = fn.code.size();
    while (target < fn.code.size() && remaining-- != 0) {
      const auto& target_instruction = fn.code[target];
      if (target_instruction.op != ir::Op::Jump ||
          target_instruction.dst == target) {
        break;
      }
      target = target_instruction.dst;
    }
  }
  if (collapse_unobservable_chain && target < fn.code.size()) {
    const auto& target_instruction = fn.code[target];
    XlangVMOpFlow flow = XlangVMOpFlow::ContinueLoop;
    if (target_instruction.op == ir::Op::IterNext) {
      ip = target;
      flow = iter_next(
          target_instruction, runtime, regs, ip,
          std::forward<EmitMonitoringEvent>(emit_monitoring_event),
          std::forward<RaiseRuntimeError>(raise_runtime_error),
          std::forward<RaiseExceptionValue>(raise_exception_value));
    } else if (target_instruction.op == ir::Op::IterNextLocal) {
      ip = target;
      flow = iter_next_local(
          target_instruction, runtime, regs, locals, ip,
          std::forward<EmitMonitoringEvent>(emit_monitoring_event),
          std::forward<RaiseRuntimeError>(raise_runtime_error),
          std::forward<RaiseExceptionValue>(raise_exception_value));
    } else {
      flow = XlangVMOpFlow::ContinueLoop;
    }
    if (target_instruction.op == ir::Op::IterNext ||
        target_instruction.op == ir::Op::IterNextLocal) {
      if (flow == XlangVMOpFlow::Next) ip = target + 1;
      return flow == XlangVMOpFlow::Next ? XlangVMOpFlow::ContinueLoop : flow;
    }
  }
  Value destination = Value::int64(static_cast<int64_t>(target));
  if (!emit_monitoring_event(kSysMonitoringEventJump, &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  ip = target;
  return XlangVMOpFlow::ContinueLoop;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow jump_if_false(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending))
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    return raise_runtime_error(error) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.dst;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow not_jump_if_false(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  bool operand = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], operand, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const bool condition = !operand;
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.dst;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow move_jump_if_false(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  value_assign_fast(regs[in.dst], regs[in.a]);
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.b;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.b;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow move_jump_if_true(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  value_assign_fast(regs[in.dst], regs[in.a]);
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? in.b : static_cast<uint32_t>(ip + 1);
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) return XlangVMOpFlow::Next;
  if (!emit_monitoring_event(kSysMonitoringEventJump, &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  ip = in.b;
  return XlangVMOpFlow::ContinueLoop;
}

template <typename EmitMonitoringEvent, typename RaiseRuntimeError,
          typename RaiseExceptionValue, typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow jump_if_false_load_local(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    RuntimeResult& result,
    EmitMonitoringEvent&& emit_monitoring_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, regs[in.a], condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.b;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.b;
    return XlangVMOpFlow::ContinueLoop;
  }
  if (in.c >= locals.size()) {
    result.errors.push_back("invalid local slot in conditional load");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.c].tag == ValueTag::Invalid) {
    const std::string name = in.c < fn.locals.size() ? fn.locals[in.c] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  value_borrow_assign_fast(regs[in.dst], locals[in.c]);
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError, typename RaiseExceptionValue, typename EmitMonitoringEvent>
XLANG3_HOT_INLINE XlangVMOpFlow jump_if_local_const_false(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    RuntimeResult& result,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    EmitMonitoringEvent&& emit_monitoring_event) {
  if (in.a >= locals.size() || in.b >= fn.constants.size()) {
    result.errors.push_back("invalid local const jump");
    return XlangVMOpFlow::ReturnResult;
  }
  Value compare_result;
  const auto op = static_cast<ir::CompareOp>(in.c);
  const auto compare_flow = compare_values(
      op, locals[in.a], fn.constants[in.b], compare_result, runtime,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
  if (compare_flow != XlangVMOpFlow::Next) return compare_flow;
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, compare_result, condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.dst;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
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
      if (current_exception.tag != ValueTag::Invalid &&
          !value_is(exception, current_exception)) {
        object_set_attr(exception, "__context__", current_exception, ignored);
      }
      object_set_attr(exception, "__suppress_context__", Value::boolean(true), ignored);
    } else if (current_exception.tag != ValueTag::Invalid &&
               !value_is(exception, current_exception)) {
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

template <typename RaiseExceptionValue, typename RaiseRuntimeError, typename EmitMonitoringEvent>
XLANG3_HOT_INLINE XlangVMOpFlow reraise(
    Value& current_exception,
    RaiseExceptionValue&& raise_exception_value,
    RaiseRuntimeError&& raise_runtime_error,
    EmitMonitoringEvent&& emit_monitoring_event) {
  if (current_exception.tag == ValueTag::Invalid) {
    return raise_runtime_error("No active exception to reraise") ? XlangVMOpFlow::ContinueLoop
                                                                : XlangVMOpFlow::ReturnResult;
  }
  if (!emit_monitoring_event(kSysMonitoringEventReraise, &current_exception)) {
    return XlangVMOpFlow::ReturnResult;
  }
  return raise_exception_value(current_exception) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
}

XLANG3_HOT_INLINE void clear_exception(
    Runtime& runtime,
    Value& current_exception,
    std::vector<Value>& previous_exceptions,
    std::vector<size_t>& active_exception_handler_depths,
    std::vector<size_t>& active_exception_handler_frames) {
  if (!previous_exceptions.empty()) {
    value_assign_fast(current_exception, previous_exceptions.back());
    previous_exceptions.pop_back();
    active_exception_handler_depths.pop_back();
    active_exception_handler_frames.pop_back();
  } else {
    value_set_invalid(current_exception);
  }
  if (current_exception.tag == ValueTag::Invalid) {
    runtime.clear_active_exception();
  } else {
    runtime.set_active_exception(current_exception);
  }
  Value ignored_pending;
  runtime.take_pending_exception(ignored_pending);
}

XLANG3_HOT_INLINE void load_exception(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    const Value& current_exception) {
  value_assign_fast(regs[in.dst], current_exception);
}

XLANG3_HOT_INLINE void set_exception(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    Value& current_exception) {
  value_assign_fast(current_exception, regs[in.a]);
  runtime.set_active_exception(current_exception);
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

template <typename EmitMonitoringEvent, typename EmitTraceEvent, typename EmitProfileEvent, typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow yield_op(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    VMFrame& frame,
    std::vector<VMFrame>& frames,
    size_t frame_count,
    GeneratorObject* generator,
    RuntimeResult& result,
    EmitMonitoringEvent&& emit_monitoring_event,
    EmitTraceEvent&& emit_trace_event,
    EmitProfileEvent&& emit_profile_event,
    RaiseRuntimeError&& raise_runtime_error) {
  if (generator == nullptr) {
    return raise_runtime_error("yield used outside generator") ? XlangVMOpFlow::ContinueLoop
                                                              : XlangVMOpFlow::ReturnResult;
  }

  Value yielded_value;
  value_assign_fast(yielded_value, regs[in.a]);
  if (!emit_monitoring_event(frame, kSysMonitoringEventPyYield, &yielded_value)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!emit_trace_event(frame, "return", yielded_value) ||
      !emit_profile_event(frame, "return", yielded_value)) {
    return XlangVMOpFlow::ReturnResult;
  }
  ++ip;

  auto* state = new GeneratorVMState();
  state->frames = std::move(frames);
  state->frame_count = frame_count;
  state->send_target = in.dst;
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
XLANG3_HOT_INLINE XlangVMOpFlow return_value(
    const Value& source,
    GeneratorObject* generator,
    RuntimeResult& result,
    FinishFrame&& finish_frame) {
  Value return_value;
  value_assign_fast(return_value, source);
  if (!finish_frame(return_value)) {
    if (generator != nullptr) {
      generator->done = true;
      value_assign_fast(generator->return_value, return_value);
      value_assign_fast(result.value, return_value);
    }
    return XlangVMOpFlow::ReturnResult;
  }
  return XlangVMOpFlow::SwitchFrame;
}

template <typename RaiseUnboundLocalError, typename RaiseRuntimeError,
          typename RaiseExceptionValue, typename EmitMonitoringEvent>
XLANG3_HOT_INLINE XlangVMOpFlow jump_if_local_local_false(
    const ir::Instr& in,
    const ir::Function& fn,
    Runtime& runtime,
    XlangVMSmallValueBuffer& locals,
    size_t& ip,
    RuntimeResult& result,
    RaiseUnboundLocalError&& raise_unbound_local_error,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value,
    EmitMonitoringEvent&& emit_monitoring_event) {
  if (in.a >= locals.size() || in.b >= locals.size()) {
    result.errors.push_back("invalid local local jump");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid || locals[in.b].tag == ValueTag::Invalid) {
    const uint32_t slot = locals[in.a].tag == ValueTag::Invalid ? in.a : in.b;
    const std::string name = slot < fn.locals.size() ? fn.locals[slot] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name +
               "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  Value compare_result;
  const auto op = static_cast<ir::CompareOp>(in.c);
  const auto compare_flow = compare_values(
      op, locals[in.a], locals[in.b], compare_result, runtime,
      std::forward<RaiseRuntimeError>(raise_runtime_error),
      std::forward<RaiseExceptionValue>(raise_exception_value));
  if (compare_flow != XlangVMOpFlow::Next) return compare_flow;
  bool condition = false;
  std::string error;
  if (!runtime_truthy(runtime, compare_result, condition, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(error)
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  const uint32_t destination_offset = condition ? static_cast<uint32_t>(ip + 1) : in.dst;
  Value destination = Value::int64(static_cast<int64_t>(destination_offset));
  if (!emit_monitoring_event(
          condition ? kSysMonitoringEventBranchLeft : kSysMonitoringEventBranchRight,
          &destination)) {
    return XlangVMOpFlow::ReturnResult;
  }
  if (!condition) {
    ip = in.dst;
    return XlangVMOpFlow::ContinueLoop;
  }
  return XlangVMOpFlow::Next;
}

template <typename FinishFrame>
XLANG3_HOT_INLINE XlangVMOpFlow return_op(
    const ir::Instr& in,
    XlangVMSmallRegisterBuffer& regs,
    GeneratorObject* generator,
    RuntimeResult& result,
    FinishFrame&& finish_frame) {
  return return_value(regs[in.a], generator, result, std::forward<FinishFrame>(finish_frame));
}

template <typename FinishFrame>
XLANG3_HOT_INLINE XlangVMOpFlow return_const(
    const ir::Instr& in,
    const ir::Function& fn,
    GeneratorObject* generator,
    RuntimeResult& result,
    FinishFrame&& finish_frame) {
  if (in.a >= fn.constants.size()) {
    result.errors.push_back("invalid return constant");
    return XlangVMOpFlow::ReturnResult;
  }
  return return_value(fn.constants[in.a], generator, result, std::forward<FinishFrame>(finish_frame));
}

template <typename FinishFrame, typename RaiseUnboundLocalError>
XLANG3_HOT_INLINE XlangVMOpFlow return_local(
    const ir::Instr& in,
    const ir::Function& fn,
    XlangVMSmallValueBuffer& locals,
    GeneratorObject* generator,
    RuntimeResult& result,
    FinishFrame&& finish_frame,
    RaiseUnboundLocalError&& raise_unbound_local_error) {
  if (in.a >= locals.size()) {
    result.errors.push_back("invalid return local");
    return XlangVMOpFlow::ReturnResult;
  }
  if (locals[in.a].tag == ValueTag::Invalid) {
    const std::string name = in.a < fn.locals.size() ? fn.locals[in.a] : "?";
    return raise_unbound_local_error(
               "cannot access local variable '" + name + "' where it is not associated with a value")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  return return_value(locals[in.a], generator, result, std::forward<FinishFrame>(finish_frame));
}

} // namespace xlang3::xlang_vm::ops
