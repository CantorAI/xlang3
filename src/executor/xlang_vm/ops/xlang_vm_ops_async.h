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
#include "../xlang_vm_op_switch.h"

#include "xlang3/generator.h"
#include "xlang3/functional_iterators.h"

#ifndef XLANG3_EMBEDDED
#include "task_objects.h"
#endif

#include <string>
#include <vector>

namespace xlang3::xlang_vm::ops {

template <typename EmitMonitoringEvent, typename EmitTraceEvent, typename EmitProfileEvent,
          typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow await_op(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    VMFrame& frame,
    std::vector<VMFrame>& frames,
    size_t frame_count,
    GeneratorObject* active_generator,
    RuntimeResult& result,
    EmitMonitoringEvent&& emit_monitoring_event,
    EmitTraceEvent&& emit_trace_event,
    EmitProfileEvent&& emit_profile_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
#ifndef XLANG3_EMBEDDED
  auto* awaited_generator = value_as_generator(regs[in.a]);
  if (awaited_generator == nullptr && value_as_async_generator_awaitable(regs[in.a]) == nullptr) {
    Value await_method;
    std::string method_error;
    if (attribute_get(regs[in.a], "__await__", await_method, method_error)) {
      Value await_iterator;
      if (!runtime_call_callable(runtime, await_method, nullptr, 0, await_iterator, method_error)) {
        Value pending;
        if (runtime.take_pending_exception(pending)) {
          return raise_exception_value(std::move(pending))
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        return raise_runtime_error(method_error.empty() ? "await failed" : method_error)
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      awaited_generator = value_as_generator(await_iterator);
      if (awaited_generator == nullptr) {
        return raise_runtime_error("__await__() returned non-iterator")
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      awaited_generator->is_await_iterator = true;
      value_assign_fast(regs[in.a], await_iterator);
    }
  }
  bool iterable_coroutine = false;
  if (awaited_generator != nullptr && !awaited_generator->is_coroutine) {
    Value code_value;
    std::string ignored;
    if (object_get_attr(awaited_generator->function, "__code__", code_value, ignored)) {
      if (auto* code = value_as_code(code_value)) {
        iterable_coroutine = code->flags_override >= 0 && (code->flags_override & 0x100) != 0;
      }
    }
  }
  if (awaited_generator != nullptr &&
      (awaited_generator->is_coroutine || awaited_generator->is_await_iterator || iterable_coroutine)) {
    Value send_value = !awaited_generator->started || regs[in.dst].tag == ValueTag::Invalid
        ? Value::none() : regs[in.dst];
    value_set_invalid(regs[in.dst]);
    bool done = false;
    Value yielded_or_returned;
    std::string await_error;
    Value awaited_value = regs[in.a];
    if (!generator_send(awaited_value, std::move(send_value), done, yielded_or_returned, await_error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(await_error.empty() ? "await failed" : await_error)
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    if (done) {
      if (active_generator != nullptr) value_set_invalid(active_generator->awaiting);
      Value stop = runtime.make_exception("StopIteration", "");
      std::string ignored;
      object_set_attr(stop, "value", yielded_or_returned, ignored);
      object_set_attr(stop, "args", yielded_or_returned.tag == ValueTag::None
          ? Value::tuple({}) : Value::tuple({yielded_or_returned}), ignored);
      Value event_arg = Value::tuple({runtime.exception_type(stop), stop, Value::none()});
      if (!emit_trace_event(frame, "exception", event_arg)) {
        return XlangVMOpFlow::ReturnResult;
      }
      value_assign_fast(regs[in.dst], yielded_or_returned);
      return XlangVMOpFlow::Next;
    }
    if (active_generator == nullptr) {
      return raise_runtime_error("coroutine yielded outside an active coroutine")
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    if (!emit_monitoring_event(frame, kSysMonitoringEventPyYield, &yielded_or_returned)) {
      return XlangVMOpFlow::ReturnResult;
    }
    if (!emit_trace_event(frame, "return", yielded_or_returned) ||
        !emit_profile_event(frame, "return", yielded_or_returned)) {
      return XlangVMOpFlow::ReturnResult;
    }
    auto* state = new GeneratorVMState();
    state->frames = std::move(frames);
    state->frame_count = frame_count;
    state->send_target = in.dst;
    if (active_generator->vm_state_cleanup != nullptr && active_generator->vm_state != nullptr) {
      active_generator->vm_state_cleanup(active_generator->vm_state);
    }
    active_generator->vm_state = state;
    active_generator->vm_state_cleanup = destroy_generator_vm_state;
    active_generator->done = false;
    value_assign_fast(active_generator->awaiting, regs[in.a]);
    value_assign_fast(result.value, yielded_or_returned);
    return XlangVMOpFlow::ReturnResult;
  }
  std::string await_error;
  if (!xlang_task_await_value(runtime, regs[in.a], regs[in.dst], await_error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    return raise_runtime_error(await_error.empty() ? "await failed" : await_error)
               ? XlangVMOpFlow::ContinueLoop
               : XlangVMOpFlow::ReturnResult;
  }
#else
  value_assign_fast(regs[in.dst], regs[in.a]);
#endif
  return XlangVMOpFlow::Next;
}

template <typename RaiseRuntimeError>
XLANG3_HOT_INLINE XlangVMOpFlow yield_from(
    RaiseRuntimeError&& raise_runtime_error) {
  return raise_runtime_error("internal yield from was not lowered") ? XlangVMOpFlow::ContinueLoop
                                                                   : XlangVMOpFlow::ReturnResult;
}

XLANG3_HOT_INLINE void pop() {}

} // namespace xlang3::xlang_vm::ops
