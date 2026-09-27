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
  auto* awaited_async_generator = value_as_async_generator_awaitable(regs[in.a]);
  if (awaited_generator == nullptr && awaited_async_generator == nullptr) {
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
    // generator_throw() can finish a delegated awaitable while handling an
    // exception. Its return value is already in the await-result register.
    if (active_generator != nullptr && active_generator->delegated_result_ready) {
      active_generator->delegated_result_ready = false;
      value_set_invalid(active_generator->awaiting);
      return XlangVMOpFlow::Next;
    }
    Value send_value = !awaited_generator->started || regs[in.dst].tag == ValueTag::Invalid
        ? Value::none() : regs[in.dst];
    value_set_invalid(regs[in.dst]);
    bool done = false;
    Value yielded_or_returned;
    std::string await_error;
    Value awaited_value = regs[in.a];
    if (!generator_send(awaited_value, std::move(send_value), done, yielded_or_returned, await_error)) {
      if (active_generator != nullptr) value_set_invalid(active_generator->awaiting);
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
  if (awaited_async_generator != nullptr) {
    Value send_value = !awaited_async_generator->started || regs[in.dst].tag == ValueTag::Invalid
        ? Value::none() : regs[in.dst];
    value_set_invalid(regs[in.dst]);
    bool done = false;
    Value yielded_or_returned;
    std::string await_error;
    if (!async_generator_awaitable_send(
            runtime, regs[in.a], std::move(send_value), done,
            yielded_or_returned, await_error)) {
      if (active_generator != nullptr) value_set_invalid(active_generator->awaiting);
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending))
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(await_error.empty() ? "await failed" : await_error)
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (done) {
      if (active_generator != nullptr) value_set_invalid(active_generator->awaiting);
      value_assign_fast(regs[in.dst], yielded_or_returned);
      return XlangVMOpFlow::Next;
    }
    if (active_generator == nullptr) {
      return raise_runtime_error("coroutine yielded outside an active coroutine")
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (!emit_monitoring_event(frame, kSysMonitoringEventPyYield, &yielded_or_returned) ||
        !emit_trace_event(frame, "return", yielded_or_returned) ||
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
    if (active_generator != nullptr) value_set_invalid(active_generator->awaiting);
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

template <typename EmitMonitoringEvent, typename EmitTraceEvent, typename EmitProfileEvent,
          typename RaiseRuntimeError, typename RaiseExceptionValue>
XLANG3_HOT_INLINE XlangVMOpFlow yield_from(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    VMFrame& frame,
    std::vector<VMFrame>& frames,
    size_t frame_count,
    GeneratorObject* generator,
    RuntimeResult& result,
    EmitMonitoringEvent&& emit_monitoring_event,
    EmitTraceEvent&& emit_trace_event,
    EmitProfileEvent&& emit_profile_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
  if (generator == nullptr) {
    return raise_runtime_error("yield from used outside generator")
        ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
  }
  // A delegated throw can finish the iterator before this instruction resumes.
  // generator_throw places its return value in the send register in that case.
  if (value_truthy(regs[in.c]) && generator->awaiting.tag == ValueTag::Invalid) {
    generator->delegated_result_ready = false;
    value_assign_fast(regs[in.dst], regs[in.b]);
    value_set_bool(regs[in.c], false);
    return XlangVMOpFlow::Next;
  }

  const Value sent = value_truthy(regs[in.c]) ? regs[in.b] : Value::none();
  const Value* delegate = &regs[in.a];
  if (regs[in.a].tag == ValueTag::Object && regs[in.a].as.obj != nullptr &&
      regs[in.a].as.obj->kind == ObjectKind::ProtocolIterator) {
    auto* protocol = reinterpret_cast<ProtocolIteratorObject*>(regs[in.a].as.obj);
    if (!protocol->use_getitem) delegate = &protocol->iterator;
  }
  Value yielded_or_returned;
  bool done = false;
  std::string error;
  if (value_as_generator(regs[in.a]) != nullptr) {
    Value iterator = regs[in.a];
    if (!generator_send(iterator, sent, done, yielded_or_returned, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending))
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error.empty() ? "yield from failed" : error)
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  } else if (sent.tag == ValueTag::None && value_as_instance(*delegate) != nullptr) {
    Value next_method;
    if (!object_get_attr(*delegate, "__next__", next_method, error)) {
      return raise_exception_value(runtime.make_exception("AttributeError", error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (!runtime_call_callable(runtime, next_method, nullptr, 0, yielded_or_returned, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        auto* exception_class = value_as_class(runtime.exception_type(pending));
        if (exception_class != nullptr && exception_class->name == "StopIteration") {
          std::string ignored;
          if (!object_get_attr(pending, "value", yielded_or_returned, ignored)) {
            value_set_none(yielded_or_returned);
          }
          done = true;
        } else {
          return raise_exception_value(std::move(pending))
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
      } else {
        return raise_runtime_error(error.empty() ? "yield from failed" : error)
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
    }
  } else if (sent.tag == ValueTag::None) {
    if (!sequence_iter_next(regs[in.a], done, yielded_or_returned, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending))
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(error.empty() ? "yield from failed" : error)
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
  } else {
    Value send_method;
    if (!object_get_attr(*delegate, "send", send_method, error)) {
      return raise_exception_value(runtime.make_exception("AttributeError", error))
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (!runtime_call_callable(runtime, send_method, &sent, 1, yielded_or_returned, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        auto* exception_class = value_as_class(runtime.exception_type(pending));
        if (exception_class != nullptr && exception_class->name == "StopIteration") {
          std::string ignored;
          if (!object_get_attr(pending, "value", yielded_or_returned, ignored)) {
            value_set_none(yielded_or_returned);
          }
          done = true;
        } else {
          return raise_exception_value(std::move(pending))
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
      } else {
        return raise_runtime_error(error.empty() ? "yield from failed" : error)
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
    }
  }
  if (done) {
    value_set_invalid(generator->awaiting);
    value_set_bool(regs[in.c], false);
    value_assign_fast(regs[in.dst], yielded_or_returned);
    return XlangVMOpFlow::Next;
  }
  if (!emit_monitoring_event(frame, kSysMonitoringEventPyYield, &yielded_or_returned) ||
      !emit_trace_event(frame, "return", yielded_or_returned) ||
      !emit_profile_event(frame, "return", yielded_or_returned)) {
    return XlangVMOpFlow::ReturnResult;
  }
  value_set_bool(regs[in.c], true);
  auto* state = new GeneratorVMState();
  state->frames = std::move(frames);
  state->frame_count = frame_count;
  state->send_target = in.b;
  if (generator->vm_state_cleanup != nullptr && generator->vm_state != nullptr) {
    generator->vm_state_cleanup(generator->vm_state);
  }
  generator->vm_state = state;
  generator->vm_state_cleanup = destroy_generator_vm_state;
  generator->done = false;
  value_assign_fast(generator->awaiting, *delegate);
  value_assign_fast(result.value, yielded_or_returned);
  return XlangVMOpFlow::ReturnResult;
}

XLANG3_HOT_INLINE void pop() {}

} // namespace xlang3::xlang_vm::ops
