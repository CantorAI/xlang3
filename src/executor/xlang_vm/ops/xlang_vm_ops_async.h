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
#include "xlang3/builtins.h"

#ifndef XLANG3_EMBEDDED
#include "task_objects.h"
#endif

#include <string>
#include <vector>

namespace xlang3::xlang_vm::ops {

// Drive the non-generator iterators allowed by __await__. CPython's _asyncio
// FutureIter uses the native am_send protocol; ordinary Python await iterators
// expose the same behavior through send()/next() and StopIteration.value.
inline bool send_await_iterator(Runtime& runtime, const Value& iterator,
                                const Value& sent, bool& done, Value& out,
                                std::string& error) {
  auto handle_exception = [&]() {
    Value exception;
    if (!runtime.take_pending_exception(exception)) return false;
    if (value_as_instance(exception) != nullptr) {
      auto* klass = value_as_class(runtime.exception_type(exception));
      if (klass != nullptr && class_has_builtin_base_name(klass, "StopIteration")) {
        done = true;
        return object_get_attr(exception, "value", out, error);
      }
    }
    runtime.set_pending_exception(std::move(exception));
    return false;
  };
  Value method;
  if (!attribute_get(iterator, "send", method, error)) {
    error.clear();
    if (sent.tag != ValueTag::None) {
      error = "await iterator does not support sending a non-None value";
      return false;
    }
    if (!attribute_get(iterator, "__next__", method, error)) return false;
    if (!runtime_call_callable(runtime, method, nullptr, 0, out, error))
      return handle_exception();
  } else if (!runtime_call_callable(runtime, method, &sent, 1, out, error)) {
    return handle_exception();
  }
  done = false;
  return true;
}

template <typename EmitMonitoringEvent, typename EmitTraceEvent, typename EmitProfileEvent,
          typename RaiseRuntimeError, typename RaiseExceptionValue, typename PushFrame>
XLANG3_HOT_INLINE XlangVMOpFlow await_op(
    const ir::Instr& in,
    Runtime& runtime,
    XlangVMSmallRegisterBuffer& regs,
    size_t& ip,
    VMFrame& frame,
    std::vector<VMFrame>& frames,
    size_t& frame_count,
    GeneratorObject* active_generator,
    PushFrame&& push_frame,
    RuntimeResult& result,
    EmitMonitoringEvent&& emit_monitoring_event,
    EmitTraceEvent&& emit_trace_event,
    EmitProfileEvent&& emit_profile_event,
    RaiseRuntimeError&& raise_runtime_error,
    RaiseExceptionValue&& raise_exception_value) {
#ifndef XLANG3_EMBEDDED
  GeneratorObject* logical_generator = frame.coroutine_owner != nullptr
      ? frame.coroutine_owner : active_generator;
  auto* awaited_generator = value_as_generator(regs[in.a]);
  auto* awaited_async_generator = value_as_async_generator_awaitable(regs[in.a]);
  bool protocol_await_iterator = false;
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
        Value iter_method, next_method, iter_result;
        if (!attribute_get(await_iterator, "__iter__", iter_method, method_error) ||
            !attribute_get(await_iterator, "__next__", next_method, method_error) ||
            !runtime_call_callable(runtime, iter_method, nullptr, 0, iter_result, method_error) ||
            !value_is(iter_result, await_iterator)) {
          return raise_runtime_error("__await__() returned non-iterator")
              ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
        }
        protocol_await_iterator = true;
      }
      if (awaited_generator != nullptr) awaited_generator->is_await_iterator = true;
      value_assign_fast(regs[in.a], await_iterator);
    } else {
      // The suspended frame retains the iterator in its await register. On
      // resume, identify that exact object before attempting __await__ again.
      protocol_await_iterator = logical_generator != nullptr &&
          value_is(logical_generator->awaiting, regs[in.a]);
    }
  }
  if (protocol_await_iterator) {
    if (logical_generator != nullptr && logical_generator->delegated_result_ready) {
      logical_generator->delegated_result_ready = false;
      value_set_invalid(logical_generator->awaiting);
      return XlangVMOpFlow::Next;
    }
    const Value sent = regs[in.dst].tag == ValueTag::Invalid ? Value::none() : regs[in.dst];
    value_set_invalid(regs[in.dst]);
    bool done = false;
    Value yielded_or_returned;
    std::string await_error;
    if (!send_await_iterator(runtime, regs[in.a], sent, done, yielded_or_returned, await_error)) {
      if (logical_generator != nullptr) value_set_invalid(logical_generator->awaiting);
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending))
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(await_error.empty() ? "await failed" : await_error)
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (done) {
      if (logical_generator != nullptr) value_set_invalid(logical_generator->awaiting);
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
    auto* state = acquire_generator_vm_state(active_generator);
    for (size_t index = 0; index < frame_count; ++index) {
      auto* owner = frames[index].coroutine_owner;
      if (owner != nullptr && owner != active_generator && owner->inline_parent != nullptr)
        owner->running = false;
    }
    state->frames = std::move(frames);
    state->frame_count = frame_count;
    state->send_target = in.dst;
    active_generator->has_observed_continuation =
        active_generator->has_observed_continuation ||
        generator_continuation_has_observers(runtime, state->frames, state->frame_count);
    active_generator->has_active_suspended_exception_handlers =
        generator_continuation_has_active_exception_handlers(state->frames, state->frame_count);
    if (active_generator->vm_state_cleanup != nullptr && active_generator->vm_state != nullptr) {
      active_generator->vm_state_cleanup(active_generator->vm_state);
    }
    active_generator->vm_state = state;
    active_generator->vm_state_cleanup = destroy_generator_vm_state;
    active_generator->done = false;
    if (logical_generator == active_generator)
      value_assign_fast(active_generator->awaiting, regs[in.a]);
    else if (logical_generator != nullptr)
      value_assign_fast(logical_generator->awaiting, regs[in.a]);
    value_assign_fast(result.value, yielded_or_returned);
    return XlangVMOpFlow::ReturnResult;
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
    if (logical_generator != nullptr && logical_generator->delegated_result_ready) {
      logical_generator->delegated_result_ready = false;
      value_set_invalid(logical_generator->awaiting);
      return XlangVMOpFlow::Next;
    }
#if !defined(XLANG3_DISABLE_AWAIT_COROUTINE_INLINE)
    auto* child_function = awaited_generator->is_coroutine && awaited_generator->args_bound &&
            awaited_generator->runtime == &runtime && !awaited_generator->started &&
            !awaited_generator->running && !awaited_generator->done &&
            awaited_generator->vm_state == nullptr && awaited_generator->vm_state_reuse == nullptr &&
            awaited_generator->pending_send.tag == ValueTag::Invalid &&
            awaited_generator->pending_throw.tag == ValueTag::Invalid &&
            awaited_generator->awaiting.tag == ValueTag::Invalid &&
            (awaited_generator->origin.tag == ValueTag::None ||
             awaited_generator->origin.tag == ValueTag::Invalid) &&
            awaited_generator->header.refcnt.load(std::memory_order_relaxed) == 1
        ? value_as_function(awaited_generator->function) : nullptr;
    const ir::Module* child_module = frame.module;
    auto child_module_owner = frame.module_owner;
    if (child_function != nullptr && child_function->module != nullptr) {
      child_module = child_function->module.get();
      child_module_owner = child_function->module;
    }
    bool observable = child_function != nullptr &&
        (logical_generator == nullptr || active_generator == nullptr ||
         runtime.trace_event_may_dispatch() || runtime.profile_event_may_dispatch() ||
         sys_monitoring_code_events(child_module, child_function->function_id) != 0);
    for (size_t index = 0; !observable && index < frame_count; ++index) {
      const auto& live = frames[index];
      observable = live.monitoring_events != 0 ||
          (live.trace_function.tag != ValueTag::Invalid && live.trace_function.tag != ValueTag::None);
    }
    if (!observable && child_function != nullptr &&
        child_function->function_id < child_module->functions.size() &&
        child_module->functions[child_function->function_id].is_coroutine &&
        awaited_generator->args.size() <= UINT32_MAX) {
      // CPython's exact-coroutine SEND continues on its current evaluator
      // stack. Do the same for a fresh, unaliased XLang coroutine by pushing
      // its bound frame onto this VM stack. The reference-count and observer
      // guards preserve the generic generator_send path for aliased objects,
      // tracing/monitoring, and every non-exact awaitable.
      CallArgsView child_args;
      child_args.leading = awaited_generator->args.data();
      child_args.leading_count = static_cast<uint32_t>(awaited_generator->args.size());
      GeneratorObject* parent_generator = logical_generator;
      Value child_value;
      value_assign_fast(child_value, regs[in.a]);
      ++ip;
      if (!push_frame(*child_module, child_function->function_id, child_args,
                      child_function->closure, child_function->defaults,
                      child_function->globals_module, std::move(child_module_owner), in.dst,
                      FrameReturnMode::StoreReturnValue, Value::invalid(), true)) {
        return result.errors.empty()
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      auto& child_frame = frames[frame_count - 1];
      child_frame.coroutine_owner = awaited_generator;
      child_frame.inline_coroutine_parent = parent_generator;
      child_frame.inline_coroutine_entry = true;
      awaited_generator->started = true;
      awaited_generator->running = true;
      awaited_generator->inline_parent = parent_generator;
      value_assign_fast(parent_generator->awaiting, child_value);
      return XlangVMOpFlow::SwitchFrame;
    }
#endif
    Value send_value = !awaited_generator->started || regs[in.dst].tag == ValueTag::Invalid
        ? Value::none() : regs[in.dst];
    value_set_invalid(regs[in.dst]);
    bool done = false;
    Value yielded_or_returned;
    std::string await_error;
    Value awaited_value = regs[in.a];
    if (!generator_send(awaited_value, std::move(send_value), done, yielded_or_returned, await_error)) {
      if (logical_generator != nullptr) value_set_invalid(logical_generator->awaiting);
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending)) ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(await_error.empty() ? "await failed" : await_error)
                 ? XlangVMOpFlow::ContinueLoop
                 : XlangVMOpFlow::ReturnResult;
    }
    if (done) {
      if (logical_generator != nullptr) value_set_invalid(logical_generator->awaiting);
      // StopIteration here is VM control flow used to transport the coroutine
      // result. Materialize the Python exception and trace tuple only when a
      // trace hook can observe the event; the normal await path should be a
      // direct value transfer with no exception-object allocation.
      if (frame.trace_function.tag != ValueTag::Invalid &&
          frame.trace_function.tag != ValueTag::None) {
        Value stop = runtime.make_exception("StopIteration", "");
        std::string ignored;
        object_set_attr(stop, "value", yielded_or_returned, ignored);
        object_set_attr(stop, "args", yielded_or_returned.tag == ValueTag::None
            ? Value::tuple({}) : Value::tuple({yielded_or_returned}), ignored);
        Value event_arg = Value::tuple({runtime.exception_type(stop), stop, Value::none()});
        if (!emit_trace_event(frame, "exception", event_arg)) {
          return XlangVMOpFlow::ReturnResult;
        }
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
    auto* state = acquire_generator_vm_state(active_generator);
    for (size_t index = 0; index < frame_count; ++index) {
      auto* owner = frames[index].coroutine_owner;
      if (owner != nullptr && owner != active_generator && owner->inline_parent != nullptr)
        owner->running = false;
    }
    state->frames = std::move(frames);
    state->frame_count = frame_count;
    state->send_target = in.dst;
    active_generator->has_observed_continuation =
        active_generator->has_observed_continuation ||
        generator_continuation_has_observers(runtime, state->frames, state->frame_count);
    active_generator->has_active_suspended_exception_handlers =
        generator_continuation_has_active_exception_handlers(state->frames, state->frame_count);
    if (active_generator->vm_state_cleanup != nullptr && active_generator->vm_state != nullptr) {
      active_generator->vm_state_cleanup(active_generator->vm_state);
    }
    active_generator->vm_state = state;
    active_generator->vm_state_cleanup = destroy_generator_vm_state;
    active_generator->done = false;
    if (logical_generator == active_generator)
      value_assign_fast(active_generator->awaiting, regs[in.a]);
    else if (logical_generator != nullptr)
      value_assign_fast(logical_generator->awaiting, regs[in.a]);
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
      if (logical_generator != nullptr) value_set_invalid(logical_generator->awaiting);
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        return raise_exception_value(std::move(pending))
            ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
      }
      return raise_runtime_error(await_error.empty() ? "await failed" : await_error)
          ? XlangVMOpFlow::ContinueLoop : XlangVMOpFlow::ReturnResult;
    }
    if (done) {
      if (logical_generator != nullptr) value_set_invalid(logical_generator->awaiting);
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
    auto* state = acquire_generator_vm_state(active_generator);
    for (size_t index = 0; index < frame_count; ++index) {
      auto* owner = frames[index].coroutine_owner;
      if (owner != nullptr && owner != active_generator && owner->inline_parent != nullptr)
        owner->running = false;
    }
    state->frames = std::move(frames);
    state->frame_count = frame_count;
    state->send_target = in.dst;
    active_generator->has_observed_continuation =
        active_generator->has_observed_continuation ||
        generator_continuation_has_observers(runtime, state->frames, state->frame_count);
    active_generator->has_active_suspended_exception_handlers =
        generator_continuation_has_active_exception_handlers(state->frames, state->frame_count);
    if (active_generator->vm_state_cleanup != nullptr && active_generator->vm_state != nullptr) {
      active_generator->vm_state_cleanup(active_generator->vm_state);
    }
    active_generator->vm_state = state;
    active_generator->vm_state_cleanup = destroy_generator_vm_state;
    active_generator->done = false;
    if (logical_generator == active_generator)
      value_assign_fast(active_generator->awaiting, regs[in.a]);
    else if (logical_generator != nullptr)
      value_assign_fast(logical_generator->awaiting, regs[in.a]);
    value_assign_fast(result.value, yielded_or_returned);
    return XlangVMOpFlow::ReturnResult;
  }
  std::string await_error;
  if (!xlang_task_await_value(runtime, regs[in.a], regs[in.dst], await_error)) {
    if (logical_generator != nullptr) value_set_invalid(logical_generator->awaiting);
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
    generator->delegation_trampoline_result_ready = false;
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
    // Delegate through the generator already held in the register. Copying a
    // Value here would add an incref/decref pair for every yield-from step,
    // which is costly for Python encoders and other chunk-producing generators.
    if (!generator_send(regs[in.a], sent, done, yielded_or_returned, error)) {
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
  auto* state = acquire_generator_vm_state(generator);
  state->frames = std::move(frames);
  state->frame_count = frame_count;
  state->send_target = in.b;
  generator->has_observed_continuation =
      generator->has_observed_continuation ||
      generator_continuation_has_observers(runtime, state->frames, state->frame_count);
  generator->has_active_suspended_exception_handlers =
      generator_continuation_has_active_exception_handlers(state->frames, state->frame_count);
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
