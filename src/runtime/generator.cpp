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
#include "xlang3/generator.h"

#include "xlang3/builtin_methods.h"
#include "xlang3/builtins.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/interpreter.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"

#include "runtime/memory/object_cache_lifetime.h"

#ifndef XLANG3_EMBEDDED
#include "task_objects.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <sstream>
#include <type_traits>

namespace xlang3 {

namespace {

thread_local uint32_t g_generator_resume_depth = 0;

struct GeneratorFreeList {
  ~GeneratorFreeList() {
    memory::object_caches_alive = false;
    for (size_t index = 0; index < size; ++index) {
      auto* generator = items[index];
      gc_untrack_object(&generator->header);
      delete generator;
    }
  }

  std::array<GeneratorObject*, 256> items{};
  size_t size = 0;
};

thread_local GeneratorFreeList generator_free_list;

struct GeneratorResumeGuard {
  GeneratorResumeGuard() { ++g_generator_resume_depth; }
  ~GeneratorResumeGuard() { --g_generator_resume_depth; }
};

template <typename T>
T* allocate_generator_object(ObjectKind kind) {
  if constexpr (std::is_same_v<T, GeneratorObject>) {
    if (memory::object_caches_alive && generator_free_list.size != 0) {
      auto* obj = generator_free_list.items[--generator_free_list.size];
      generator_free_list.items[generator_free_list.size] = nullptr;
      obj->header.kind = kind;
      obj->header.refcnt = 1;
      xlang_perf_count_object_alloc(kind);
      // Recycled zero-ref generators keep their GC index, so this call is a
      // cheap state check instead of a lock and vector insertion.
      gc_track_object(&obj->header);
      return obj;
    }
  }
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(kind);
  gc_track_object(&obj->header);
  return obj;
}

void make_async_generator_awaitable(
    Value generator,
    AsyncGenAwaitableKind kind,
    std::vector<Value> args,
    Value& out) {
  out.tag = ValueTag::Object;
  auto* obj = allocate_generator_object<AsyncGenAwaitableObject>(ObjectKind::AsyncGeneratorAwaitable);
  obj->kind = kind;
  obj->generator = std::move(generator);
  obj->args = std::move(args);
  out.as.obj = &obj->header;
}

void raise_stop_iteration_with_value(Runtime& runtime, const Value& return_value) {
  Value exception = runtime.make_exception("StopIteration", "");
  std::string ignored;
  object_set_attr(exception, "value", return_value, ignored);
  if (return_value.tag == ValueTag::None) {
    object_set_attr(exception, "args", Value::tuple({}), ignored);
  } else {
    object_set_attr(exception, "args", Value::tuple({return_value}), ignored);
    object_set_attr(exception, "message", return_value, ignored);
  }
  runtime.set_pending_exception(std::move(exception));
}

bool emit_generator_throw_monitoring_event(GeneratorObject& obj, const Value& exception, std::string& error) {
  if (obj.runtime == nullptr || !sys_monitoring_event_may_dispatch(kSysMonitoringEventPyThrow)) {
    return true;
  }
  auto* function = value_as_function(obj.function);
  if (function == nullptr || function->module == nullptr) {
    return true;
  }
  Value code = Value::code(function->module, function->function_id);
  return sys_monitoring_dispatch_event(*obj.runtime, kSysMonitoringEventPyThrow, code, -1, &exception, error);
}

bool exception_has_class_name(Runtime& runtime, const Value& exception, const char* class_name) {
  auto* klass = value_as_class(runtime.exception_type(exception));
  return klass != nullptr && klass->name == class_name;
}

void clear_expected_generator_exit(Runtime& runtime) {
  Value pending;
  if (runtime.take_pending_exception(pending) && !exception_has_class_name(runtime, pending, "GeneratorExit")) {
    runtime.set_pending_exception(std::move(pending));
  }
  if (exception_has_class_name(runtime, runtime.active_exception(), "GeneratorExit")) {
    runtime.clear_active_exception();
  }
}

void clear_generator_vm_states(GeneratorObject& generator) {
  if (generator.vm_state_cleanup != nullptr && generator.vm_state != nullptr) {
    generator.vm_state_cleanup(generator.vm_state);
  }
  if (generator.vm_state_reuse_cleanup != nullptr && generator.vm_state_reuse != nullptr) {
    generator.vm_state_reuse_cleanup(generator.vm_state_reuse);
  }
  generator.vm_state = nullptr;
  generator.vm_state_cleanup = nullptr;
  generator.vm_state_reuse = nullptr;
  generator.vm_state_reuse_cleanup = nullptr;
}

} // namespace

Value Value::generator(
    Runtime* runtime,
    Value function,
    std::vector<Value> args,
    bool is_async,
    bool is_coroutine,
    bool args_bound) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_generator_object<GeneratorObject>(ObjectKind::Generator);
  obj->runtime = runtime;
  obj->function = std::move(function);
  obj->args = std::move(args);
  obj->is_async = is_async;
  obj->is_coroutine = is_coroutine;
  obj->args_bound = args_bound;
  value_set_none(obj->return_value);
  value_set_none(obj->origin);
  if (runtime != nullptr && is_coroutine) {
    const int64_t depth = sys_coroutine_origin_tracking_depth();
    if (depth > 0) {
      std::vector<Value> origin;
      Value frame = runtime->current_frame_snapshot();
      for (int64_t index = 0; index < depth && value_as_frame(frame) != nullptr; ++index) {
        Value code;
        Value filename;
        Value name;
        Value line;
        std::string ignored;
        if (!object_get_attr(frame, "f_code", code, ignored) ||
            !object_get_attr(code, "co_filename", filename, ignored) ||
            !object_get_attr(code, "co_name", name, ignored) ||
            !object_get_attr(frame, "f_lineno", line, ignored)) {
          break;
        }
        origin.push_back(Value::tuple({filename, line, name}));
        auto* current = value_as_frame(frame);
        if (current->back.tag == ValueTag::None || current->back.tag == ValueTag::Invalid) break;
        frame = current->back;
      }
      obj->origin = Value::tuple(std::move(origin));
    }
  }
  v.as.obj = &obj->header;
  return v;
}

void generator_release_object(Object* object) {
  if (object->kind == ObjectKind::Generator) {
    auto* generator = reinterpret_cast<GeneratorObject*>(object);
    clear_generator_vm_states(*generator);
    value_set_invalid(generator->function);
    value_set_invalid(generator->pending_send);
    value_set_invalid(generator->pending_throw);
    value_set_invalid(generator->return_value);
    value_set_invalid(generator->awaiting);
    value_set_invalid(generator->origin);
    generator->args.clear();
    generator->runtime = nullptr;
    generator->inline_parent = nullptr;
    generator->has_pending_send = false;
    generator->has_pending_throw = false;
    generator->delegated_result_ready = false;
    generator->args_bound = false;
    generator->started = false;
    generator->running = false;
    generator->is_async = false;
    generator->is_coroutine = false;
    generator->is_await_iterator = false;
    generator->has_observed_continuation = false;
    generator->has_active_suspended_exception_handlers = false;
    generator->delegation_trampoline_result_ready = false;
    generator->done = false;
    // Coroutine workloads create hundreds of thousands of short-lived
    // generators. Keep a bounded thread-local cache and its GC index to avoid
    // both heap churn and the global tracker lock on create/final release.
    if (memory::object_caches_alive && generator->args.capacity() <= 8 &&
        generator_free_list.size < generator_free_list.items.size()) {
      generator_free_list.items[generator_free_list.size++] = generator;
      return;
    }
    gc_untrack_object(&generator->header);
    delete generator;
    return;
  }
  delete reinterpret_cast<AsyncGenAwaitableObject*>(object);
}

std::string generator_to_string(const Value& value) {
  if (auto* awaitable = value_as_async_generator_awaitable(value)) {
    if (awaitable->kind == AsyncGenAwaitableKind::AThrow) {
      return "<async_generator_athrow object>";
    }
    if (awaitable->kind == AsyncGenAwaitableKind::AClose) {
      return "<async_generator_aclose object>";
    }
    return "<async_generator_asend object>";
  }
  if (auto* generator = value_as_generator(value); generator != nullptr) {
    std::string name;
    if (auto* function = value_as_function(generator->function)) {
      name = function->qualname;
      if (name.empty() && function->module != nullptr &&
          function->function_id < function->module->functions.size()) {
        const auto& metadata =
            function->module->functions[function->function_id];
        name = metadata.qualname.empty() ? metadata.name : metadata.qualname;
      }
    }
    if (!name.empty()) {
      std::ostringstream address;
      address << std::hex
              << reinterpret_cast<uintptr_t>(generator);
      const char* kind = generator->is_coroutine
          ? "coroutine"
          : generator->is_async ? "async_generator" : "generator";
      return "<" + std::string(kind) + " object " + name + " at 0x" +
          address.str() + ">";
    }
    if (generator->is_coroutine) return "<coroutine object>";
    if (generator->is_async) return "<async_generator object>";
  }
  return "<generator object>";
}

bool generator_truthy(const Value&) {
  return true;
}

bool generator_get_iter(const Value& generator, Value& out, std::string& error) {
  if (value_as_generator(generator) == nullptr) {
    error = "object is not a generator";
    return false;
  }
  value_assign_fast(out, generator);
  return true;
}

bool generator_iter_next(Value& generator, bool& done, Value& out, std::string& error) {
  return generator_send(generator, Value::none(), done, out, error);
}

namespace {

bool generator_can_trampoline(const GeneratorObject& generator) {
  if (generator.runtime == nullptr || generator.is_async || generator.is_coroutine ||
      generator.has_observed_continuation ||
      generator.has_active_suspended_exception_handlers || xlang_perf_enabled()) {
    return false;
  }
  const Runtime& runtime = *generator.runtime;
  const auto active_hook = [](const Value& hook) {
    return hook.tag != ValueTag::Invalid && hook.tag != ValueTag::None;
  };
  return !runtime.debug_step_active() && !active_hook(runtime.debug_hook()) &&
      !active_hook(runtime.trace_function()) && !active_hook(runtime.profile_function()) &&
      !sys_monitoring_event_may_dispatch(kSysMonitoringEventAll);
}

bool try_generator_delegation_trampoline(
    Value& root_value, Value send_value, bool& handled, bool& done, Value& out, std::string& error) {
  handled = false;
  auto* root = value_as_generator(root_value);
  if (root == nullptr || !generator_can_trampoline(*root)) return true;

  // Preserve the ordinary VM path whenever any continuation is observable,
  // has an active exception handler, is async, or has already completed.
  // In the unobserved synchronous case, a yield-from chain is only a stack of
  // saved forwarding frames: resume its leaf directly and unwind parents only
  // when a child finishes. This removes one interpreter entry and yield event
  // propagation per forwarding level for each yielded item.
  constexpr size_t kMaxDelegationDepth = 200;
  // Only [0, chain_size) is read, and the scan writes each slot before use.
  // Avoid clearing the full maximum-depth buffer on every generator resume.
  std::array<GeneratorObject*, kMaxDelegationDepth> chain;
  size_t chain_size = 0;
  GeneratorObject* current = root;
  while (current != nullptr) {
    if (!generator_can_trampoline(*current) || current->running || chain_size == chain.size()) {
      return true;
    }
    chain[chain_size++] = current;
    auto* child = value_as_generator(current->awaiting);
    if (child == nullptr) break;
    if (child->done || child->running || child->runtime != root->runtime) return true;
    current = child;
  }
  if (chain_size < 2) return true;

  struct RunningChainGuard {
    std::array<GeneratorObject*, kMaxDelegationDepth>& chain;
    size_t count;
    ~RunningChainGuard() {
      for (size_t i = 0; i < count; ++i) chain[i]->running = false;
    }
  } running_guard{chain, chain_size};
  for (size_t i = 0; i < chain_size; ++i) {
    chain[i]->running = true;
    chain[i]->started = true;
  }

  handled = true;
  size_t target_index = chain_size - 1;
  bool forward_send = true;
  while (true) {
    bool target_done = false;
    Value target_out;
    bool resumed = false;
    if (target_index == 0) {
      resumed = generator_send(root_value,
          chain[0]->delegation_trampoline_result_ready ? Value::none() : send_value,
          target_done, target_out, error);
    } else {
      Value target_value;
      value_assign_fast(target_value, chain[target_index - 1]->awaiting);
      const Value target_send = forward_send ? send_value : Value::none();
      resumed = generator_send(target_value, target_send, target_done, target_out, error);
    }
    forward_send = false;
    if (!resumed) {
      // With no suspended handlers, a delegated exception is unhandled at
      // every forwarding level. Retire those continuations while preserving
      // the original pending exception for the caller.
      for (size_t i = 0; i <= target_index; ++i) {
        clear_generator_vm_states(*chain[i]);
        chain[i]->done = true;
        chain[i]->has_pending_send = false;
        chain[i]->delegation_trampoline_result_ready = false;
        value_set_invalid(chain[i]->pending_send);
        value_set_invalid(chain[i]->awaiting);
      }
      return false;
    }
    if (!target_done) {
      done = false;
      value_assign_fast(out, target_out);
      return true;
    }
    if (target_index == 0) {
      done = true;
      value_assign_fast(out, target_out);
      return true;
    }

    GeneratorObject* parent = chain[target_index - 1];
    value_set_invalid(parent->awaiting);
    value_assign_fast(parent->pending_send, target_out);
    parent->has_pending_send = true;
    parent->delegation_trampoline_result_ready = true;
    --target_index;
  }
}

} // namespace

bool generator_begin_any_consume(GeneratorObject& generator) {
  if (!generator_can_trampoline(generator) ||
      generator.has_active_suspended_exception_handlers ||
      generator.running || generator.done) {
    return false;
  }
  generator.consume_for_any = true;
  return true;
}

void generator_end_any_consume(GeneratorObject& generator) {
  generator.consume_for_any = false;
}

bool generator_send(Value& generator, Value value, bool& done, Value& out, std::string& error) {
  auto* obj = value_as_generator(generator);
  if (obj == nullptr) {
    error = "invalid generator";
    return false;
  }
  if (obj->done) {
    if (obj->is_coroutine) {
      error = "cannot reuse already awaited coroutine";
      Value exception = obj->runtime->make_exception("RuntimeError", error);
      value_assign_fast(out, exception);
      obj->runtime->set_pending_exception(std::move(exception));
      return false;
    }
    done = true;
    value_set_none(out);
    return true;
  }
  if (!obj->started && value.tag != ValueTag::None) {
    error = "can't send non-None value to a just-started generator";
    return false;
  }
  if (obj->runtime == nullptr) {
    error = "generator has invalid runtime";
    return false;
  }
  bool trampoline_handled = false;
  if (!try_generator_delegation_trampoline(
          generator, value, trampoline_handled, done, out, error)) {
    return false;
  }
  if (trampoline_handled) return true;
  // ``yield from`` resumes delegated generators through this native call path,
  // so each level consumes host stack even though it is a Python generator
  // frame.  Guard it before Windows exhausts its C stack.
  constexpr uint32_t kSafeGeneratorResumeLimit = 200;
  const uint32_t effective_recursion_limit = std::min(
      static_cast<uint32_t>(obj->runtime->recursion_limit()), kSafeGeneratorResumeLimit);
  if (g_generator_resume_depth >= effective_recursion_limit) {
    error = "maximum recursion depth exceeded";
    obj->runtime->raise_class_error("RecursionError", error);
    return false;
  }
  GeneratorResumeGuard resume_guard;
  obj->started = true;
  if (!obj->delegation_trampoline_result_ready) {
    value_assign_fast(obj->pending_send, value);
    obj->has_pending_send = true;
  }
  Interpreter interpreter(*obj->runtime);
  obj->running = true;
  RuntimeResult result = interpreter.resume_generator(*obj, out, done);
  obj->running = false;
  if (done) {
    obj->done = true;
    clear_generator_vm_states(*obj);
    value_set_invalid(obj->pending_send);
    value_set_invalid(obj->pending_throw);
    value_set_invalid(obj->awaiting);
    obj->args.clear();
    obj->has_pending_send = false;
    obj->has_pending_throw = false;
    obj->delegated_result_ready = false;
    obj->delegation_trampoline_result_ready = false;
    value_set_none(obj->return_value);
  }
  if (!result.errors.empty()) {
    clear_generator_vm_states(*obj);
    if (result.exception.tag != ValueTag::Invalid) {
      value_assign_fast(out, result.exception);
    }
    error = result.errors.front();
    return false;
  }
  return true;
}

bool generator_close(Value& generator, Value& out, std::string& error) {
  auto* obj = value_as_generator(generator);
  if (obj == nullptr) {
    error = "invalid generator";
    return false;
  }
  if (!obj->done && obj->awaiting.tag != ValueTag::Invalid) {
    Value awaiting = obj->awaiting;
    Value ignored;
    std::string close_error;
    if (value_as_generator(awaiting) != nullptr) {
      if (!generator_close(awaiting, ignored, close_error)) {
        error = std::move(close_error);
        return false;
      }
    } else {
      Value close_method;
      if (object_get_attr(awaiting, "close", close_method, close_error)) {
        if (!runtime_call_callable(*obj->runtime, close_method, nullptr, 0,
                                   ignored, error)) return false;
      } else {
        Value pending;
        if (obj->runtime->take_pending_exception(pending) &&
            !exception_has_class_name(*obj->runtime, pending, "AttributeError")) {
          obj->runtime->set_pending_exception(std::move(pending));
          error = std::move(close_error);
          return false;
        }
      }
    }
    value_set_invalid(obj->awaiting);
  }
  if (!obj->done && obj->vm_state != nullptr && obj->runtime != nullptr) {
    Value exception = obj->runtime->make_exception("GeneratorExit", "");
    value_assign_fast(obj->pending_throw, exception);
    obj->has_pending_throw = true;
    obj->started = true;
    Interpreter interpreter(*obj->runtime);
    bool done = false;
    Value yielded;
    obj->running = true;
    RuntimeResult result = interpreter.resume_generator(*obj, yielded, done);
    obj->running = false;
    if (!result.errors.empty()) {
      if (result.errors.front().find("GeneratorExit") == std::string::npos) {
        clear_generator_vm_states(*obj);
        error = result.errors.front();
        return false;
      }
      clear_expected_generator_exit(*obj->runtime);
    } else if (!done) {
      error = "generator ignored GeneratorExit";
      obj->runtime->raise_class_error("RuntimeError", error);
      return false;
    } else {
      clear_expected_generator_exit(*obj->runtime);
    }
  }
  clear_generator_vm_states(*obj);
  obj->done = true;
  value_set_invalid(obj->pending_send);
  value_set_invalid(obj->pending_throw);
  value_set_invalid(obj->awaiting);
  obj->args.clear();
  obj->has_pending_send = false;
  obj->has_pending_throw = false;
  obj->delegated_result_ready = false;
  value_set_none(obj->return_value);
  value_set_none(out);
  return true;
}

bool generator_throw(Value& generator, const Value* args, uint32_t argc, Value& out, std::string& error) {
  auto* obj = value_as_generator(generator);
  if (obj == nullptr) {
    error = "invalid generator";
    return false;
  }
  if (argc < 1 || argc > 3) {
    error = "generator.throw expected 1 to 3 arguments";
    return false;
  }
  if (obj->runtime == nullptr) {
    error = "generator has invalid runtime";
    return false;
  }
  Value exception;
  if (auto* klass = value_as_class(args[0])) {
    const std::string message = argc >= 2 ? value_to_string(args[1]) : std::string{};
    exception = obj->runtime->make_exception_from_class(args[0], message);
  } else {
    value_assign_fast(exception, args[0]);
  }
  bool delegated_completed = false;
  Value delegated_return;
  if (!obj->done && obj->awaiting.tag != ValueTag::Invalid) {
    Value awaiting = obj->awaiting;
    Value delegated_out;
    std::string delegated_error;
    bool delegated_resumed = false;
    if (auto* async_awaitable = value_as_async_generator_awaitable(awaiting)) {
      if (async_awaitable->consumed) {
        delegated_error = "cannot reuse already awaited async generator awaitable";
      } else {
        async_awaitable->started = true;
        delegated_resumed = generator_throw(
            async_awaitable->generator, args, argc, delegated_out, delegated_error);
        if (delegated_resumed) {
          auto* async_generator = value_as_generator(async_awaitable->generator);
          if (async_generator != nullptr && async_generator->awaiting.tag != ValueTag::Invalid) {
            value_assign_fast(out, delegated_out);
            return true;
          }
          async_awaitable->consumed = true;
          value_assign_fast(delegated_return, delegated_out);
          delegated_completed = true;
        } else {
          async_awaitable->consumed = true;
          auto* async_generator = value_as_generator(async_awaitable->generator);
          if (async_generator != nullptr && async_generator->done) {
            Value delegated_exception;
            if (obj->runtime->take_pending_exception(delegated_exception)) {
              if (exception_has_class_name(
                      *obj->runtime, delegated_exception, "StopIteration")) {
                obj->runtime->set_pending_exception(
                    obj->runtime->make_exception("StopAsyncIteration", ""));
              } else {
                obj->runtime->set_pending_exception(
                    std::move(delegated_exception));
              }
            }
          }
        }
      }
    } else if (value_as_generator(awaiting) != nullptr) {
      delegated_resumed = generator_throw(awaiting, args, argc, delegated_out, delegated_error);
      if (delegated_resumed) {
        value_assign_fast(out, delegated_out);
        return true;
      }
    } else {
      Value throw_method;
      if (object_get_attr(awaiting, "throw", throw_method, delegated_error)) {
        delegated_resumed = runtime_call_callable(
            *obj->runtime, throw_method, args, argc, delegated_out,
            delegated_error);
        if (delegated_resumed) {
          value_assign_fast(out, delegated_out);
          return true;
        }
      } else {
        Value pending;
        if (obj->runtime->take_pending_exception(pending) &&
            !exception_has_class_name(*obj->runtime, pending, "AttributeError")) {
          obj->runtime->set_pending_exception(std::move(pending));
          error = std::move(delegated_error);
          return false;
        }
      }
    }
    Value delegated_exception;
    if (!delegated_completed && obj->runtime->take_pending_exception(delegated_exception)) {
      if (exception_has_class_name(*obj->runtime, delegated_exception, "StopIteration")) {
        std::string ignored;
        if (!object_get_attr(delegated_exception, "value", delegated_return, ignored)) {
          value_set_none(delegated_return);
        }
        delegated_completed = true;
      } else {
        value_assign_fast(exception, delegated_exception);
      }
    } else if (!delegated_completed && value_as_instance(delegated_out) != nullptr) {
      if (exception_has_class_name(*obj->runtime, delegated_out, "StopIteration")) {
        std::string ignored;
        if (!object_get_attr(delegated_out, "value", delegated_return, ignored)) {
          value_set_none(delegated_return);
        }
        delegated_completed = true;
      } else {
        value_assign_fast(exception, delegated_out);
      }
    } else if (!delegated_completed && obj->runtime->active_exception().tag != ValueTag::Invalid) {
      value_assign_fast(exception, obj->runtime->active_exception());
    }
    value_set_invalid(obj->awaiting);
  }
  if (obj->done || obj->vm_state == nullptr) {
    if (!obj->done && !obj->started) {
      obj->started = true;
      obj->done = true;
    }
    obj->runtime->set_active_exception(exception);
    value_assign_fast(out, exception);
    error = value_to_string(exception);
    return false;
  }
  if (delegated_completed) {
    value_assign_fast(obj->pending_send, delegated_return);
    obj->has_pending_send = true;
    obj->delegated_result_ready = true;
    value_set_invalid(obj->pending_throw);
    obj->has_pending_throw = false;
  } else {
    if (!emit_generator_throw_monitoring_event(*obj, exception, error)) {
      return false;
    }
    value_assign_fast(obj->pending_throw, exception);
    obj->has_pending_throw = true;
  }
  obj->started = true;
  Interpreter interpreter(*obj->runtime);
  bool done = false;
  obj->running = true;
  RuntimeResult result = interpreter.resume_generator(*obj, out, done);
  obj->running = false;
  if (done) {
    obj->done = true;
    clear_generator_vm_states(*obj);
    value_set_invalid(obj->pending_send);
    value_set_invalid(obj->pending_throw);
    value_set_invalid(obj->awaiting);
    obj->args.clear();
    obj->has_pending_send = false;
    obj->has_pending_throw = false;
    obj->delegated_result_ready = false;
    value_set_none(obj->return_value);
  }
  if (!result.errors.empty()) {
    clear_generator_vm_states(*obj);
    if (result.exception.tag != ValueTag::Invalid) {
      value_assign_fast(out, result.exception);
    }
    error = result.errors.front();
    return false;
  }
  if (done) {
    Value return_value;
    value_assign_fast(return_value, out);
    Value stop = obj->runtime->make_exception("StopIteration", "");
    std::string ignored;
    object_set_attr(stop, "value", return_value, ignored);
    object_set_attr(stop, "args", return_value.tag == ValueTag::None
        ? Value::tuple({}) : Value::tuple({return_value}), ignored);
    value_assign_fast(out, stop);
    obj->runtime->set_pending_exception(std::move(stop));
    error = "generator raised StopIteration";
    return false;
  }
  return true;
}

bool async_generator_awaitable_send(
    Runtime& runtime,
    const Value& value,
    Value send_value,
    bool& done,
    Value& out,
    std::string& error) {
  auto* state = value_as_async_generator_awaitable(value);
  if (state == nullptr) {
    error.clear();
    return false;
  }
  if (state->consumed) {
    error = "cannot reuse already awaited async generator awaitable";
    return false;
  }
  auto* generator = value_as_generator(state->generator);
  if (generator == nullptr) {
    error = "async generator awaitable has invalid generator";
    return false;
  }

  bool generator_done = false;
  bool resumed = false;
  if (state->started) {
    resumed = generator_send(
        state->generator, std::move(send_value), generator_done, out, error);
  } else {
    state->started = true;
    switch (state->kind) {
      case AsyncGenAwaitableKind::ANext:
        resumed = generator_send(
            state->generator, Value::none(), generator_done, out, error);
        break;
      case AsyncGenAwaitableKind::ASend:
        if (state->args.empty()) {
          error = "async_generator.asend missing value";
          return false;
        }
        resumed = generator_send(
            state->generator, state->args[0], generator_done, out, error);
        break;
      case AsyncGenAwaitableKind::AThrow:
        resumed = generator_throw(
            state->generator,
            state->args.data(),
            static_cast<uint32_t>(state->args.size()),
            out,
            error);
        break;
      case AsyncGenAwaitableKind::AClose:
        // aclose() is still handled by the established close path.  It does
        // not participate in this resumable path until cleanup itself awaits.
        resumed = generator_close(state->generator, out, error);
        generator_done = resumed;
        break;
    }
  }

  if (!resumed) {
    state->consumed = true;
    if (state->kind == AsyncGenAwaitableKind::AThrow && generator->done &&
        exception_has_class_name(runtime, out, "StopIteration")) {
      Value pending;
      runtime.take_pending_exception(pending);
      error = "async generator exhausted";
      runtime.raise_class_error("StopAsyncIteration", error);
      return false;
    }
    if (value_as_instance(out) != nullptr) {
      runtime.set_pending_exception(out);
    }
    if (error.empty()) {
      error = "async generator await failed";
    }
    return false;
  }

  if (generator->awaiting.tag != ValueTag::Invalid) {
    done = false;
    return true;
  }

  state->consumed = true;
  done = true;
  if (generator_done &&
      (state->kind == AsyncGenAwaitableKind::ANext ||
       state->kind == AsyncGenAwaitableKind::ASend ||
       state->kind == AsyncGenAwaitableKind::AThrow)) {
    error = "async generator exhausted";
    runtime.raise_class_error("StopAsyncIteration", error);
    return false;
  }
  return true;
}

bool async_generator_awaitable_await(Runtime& runtime, const Value& value, Value& out, std::string& error) {
  bool done = false;
  if (!async_generator_awaitable_send(
          runtime, value, Value::none(), done, out, error)) {
    return false;
  }
  if (!done) {
    error = "async generator awaitable suspended without an event-loop scheduler";
    return false;
  }
  return true;
}

namespace {

bool async_generator_awaitable_await_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (!method_check_argc(argc, 1, "async_generator_awaitable.__await__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_async_generator_awaitable(args[0]) == nullptr) {
    error = "object is not an async generator awaitable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool async_generator_awaitable_next_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (!method_check_argc(argc, 1, "async_generator_awaitable.__next__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool done = false;
  if (!async_generator_awaitable_send(
          runtime, args[0], Value::none(), done, out, error)) {
    if (runtime.active_exception().tag == ValueTag::Invalid && error.empty()) {
      runtime.raise_class_error("RuntimeError", "async generator await failed");
    }
    return false;
  }
  if (!done) {
    return true;
  }
  raise_stop_iteration_with_value(runtime, out);
  return false;
}

bool async_generator_awaitable_send_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "async_generator_awaitable.send() expected value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool done = false;
  if (!async_generator_awaitable_send(runtime, args[0], args[1], done, out, error)) {
    return false;
  }
  if (!done) {
    return true;
  }
  raise_stop_iteration_with_value(runtime, out);
  return false;
}

bool async_generator_awaitable_throw_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 4) {
    error = "async_generator_awaitable.throw expected 1 to 3 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = value_as_async_generator_awaitable(args[0]);
  if (state == nullptr) {
    error = "object is not an async generator awaitable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (state->consumed) {
    error = "cannot reuse already awaited async generator awaitable";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  state->started = true;
  bool resumed = generator_throw(state->generator, args + 1, argc - 1, out, error);
  if (!resumed) {
    state->consumed = true;
    if (value_as_instance(out) != nullptr) runtime.set_pending_exception(out);
    return false;
  }
  auto* generator = value_as_generator(state->generator);
  if (generator != nullptr && generator->awaiting.tag != ValueTag::Invalid) return true;
  state->consumed = true;
  raise_stop_iteration_with_value(runtime, out);
  return false;
}

bool async_generator_awaitable_close_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (!method_check_argc(argc, 1, "async_generator_awaitable.close", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = value_as_async_generator_awaitable(args[0]);
  if (state == nullptr) {
    error = "object is not an async generator awaitable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (state->consumed) {
    value_set_none(out);
    return true;
  }
  state->consumed = true;
  if (!generator_close(state->generator, out, error)) {
    if (runtime.active_exception().tag == ValueTag::Invalid) {
      runtime.raise_class_error("RuntimeError", error);
    }
    return false;
  }
  return true;
}

bool generator_send_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "generator.send", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value generator = args[0];
  bool done = false;
  if (!generator_send(generator, args[1], done, out, error)) {
    if (value_as_instance(out) != nullptr) {
      runtime.set_pending_exception(out);
      error.clear();
    } else {
      runtime.raise_class_error("TypeError", error);
    }
    return false;
  }
  if (done) {
    error.clear();
    raise_stop_iteration_with_value(runtime, out);
    return false;
  }
  return true;
}

bool generator_next_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "generator.__next__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value generator = args[0];
  bool done = false;
  if (!generator_send(generator, Value::none(), done, out, error)) {
    if (value_as_instance(out) != nullptr) {
      runtime.set_pending_exception(out);
      error.clear();
    } else {
      runtime.raise_class_error("TypeError", error);
    }
    return false;
  }
  if (done) {
    error.clear();
    raise_stop_iteration_with_value(runtime, out);
    return false;
  }
  return true;
}

bool generator_iter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "generator.__iter__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return generator_get_iter(args[0], out, error);
}

bool generator_aiter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "async_generator.__aiter__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  if (generator == nullptr || !generator->is_async || generator->is_coroutine) {
    error = "object is not an async generator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool generator_anext_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "async_generator.__anext__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  if (generator == nullptr || !generator->is_async || generator->is_coroutine) {
    error = "object is not an async generator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  make_async_generator_awaitable(args[0], AsyncGenAwaitableKind::ANext, {}, out);
  return true;
}

bool generator_asend_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "async_generator.asend", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  if (generator == nullptr || !generator->is_async || generator->is_coroutine) {
    error = "object is not an async generator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  make_async_generator_awaitable(args[0], AsyncGenAwaitableKind::ASend, {args[1]}, out);
  return true;
}

bool generator_aclose_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "async_generator.aclose", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  if (generator == nullptr || !generator->is_async || generator->is_coroutine) {
    error = "object is not an async generator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  make_async_generator_awaitable(args[0], AsyncGenAwaitableKind::AClose, {}, out);
  return true;
}

bool generator_athrow_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "async_generator.athrow expected 1 to 3 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  if (generator == nullptr || !generator->is_async || generator->is_coroutine) {
    error = "object is not an async generator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> throw_args;
  throw_args.reserve(argc - 1);
  for (uint32_t i = 1; i < argc; ++i) {
    throw_args.push_back(args[i]);
  }
  make_async_generator_awaitable(args[0], AsyncGenAwaitableKind::AThrow, std::move(throw_args), out);
  return true;
}

bool generator_close_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "generator.close", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value generator = args[0];
  if (!generator_close(generator, out, error)) {
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  return true;
}

bool generator_throw_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "generator.throw expected 1 to 3 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value generator = args[0];
  if (!generator_throw(generator, args + 1, argc - 1, out, error)) {
    if (value_as_instance(out) != nullptr) {
      runtime.set_pending_exception(out);
    } else if (auto* klass = value_as_class(args[1])) {
      runtime.raise_class_error(klass->name, error);
    } else {
      runtime.set_active_exception(args[1]);
    }
    return false;
  }
  return true;
}

bool coroutine_await_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "coroutine.__await__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* generator = value_as_generator(args[0]);
  if (generator == nullptr || !generator->is_coroutine) {
    error = "object is not a coroutine";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

static BuiltinMethodSpec kGeneratorMethods[] = {
      {"__await__", "coroutine.__await__", coroutine_await_method},
      {"__iter__", "generator.__iter__", generator_iter_method},
      {"__next__", "generator.__next__", generator_next_method},
      {"__aiter__", "async_generator.__aiter__", generator_aiter_method},
      {"__anext__", "async_generator.__anext__", generator_anext_method},
      {"asend", "async_generator.asend", generator_asend_method},
      {"aclose", "async_generator.aclose", generator_aclose_method},
      {"athrow", "async_generator.athrow", generator_athrow_method},
      {"close", "generator.close", generator_close_method},
      {"send", "generator.send", generator_send_method},
      {"throw", "generator.throw", generator_throw_method},
};

static BuiltinMethodSpec kAsyncGeneratorAwaitableMethods[] = {
      {"__await__", "async_generator_awaitable.__await__", async_generator_awaitable_await_method},
      {"__iter__", "async_generator_awaitable.__iter__", async_generator_awaitable_await_method},
      {"__next__", "async_generator_awaitable.__next__", async_generator_awaitable_next_method},
      {"send", "async_generator_awaitable.send", async_generator_awaitable_send_method},
      {"throw", "async_generator_awaitable.throw", async_generator_awaitable_throw_method},
      {"close", "async_generator_awaitable.close", async_generator_awaitable_close_method},
};

} // namespace

void frame_set_generator_owner(Runtime& runtime, Value& frame,
                               const GeneratorObject& generator) {
  auto* frame_object = value_as_frame(frame);
  if (frame_object == nullptr ||
      frame_object->generator_ref.tag != ValueTag::Invalid)
    return;
  Value borrowed;
  borrowed.tag = ValueTag::Object;
  borrowed.flags = kXlangValueBorrowedRefFlag;
  borrowed.as.obj = const_cast<Object*>(&generator.header);
  frame_object->generator_ref = make_weakref_ref(runtime, borrowed);
}

bool generator_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_async_generator_awaitable(object) != nullptr) {
    return bind_builtin_method_from_table(
        object,
        name,
        kAsyncGeneratorAwaitableMethods,
        std::size(kAsyncGeneratorAwaitableMethods),
        out);
  }
  auto* generator = value_as_generator(object);
  if (generator == nullptr) {
    return false;
  }
  if (name == "gi_running" || name == "ag_running" || name == "cr_running") {
    value_set_bool(out, generator->running);
    return true;
  }
  if (name == "gi_suspended" || name == "ag_suspended" || name == "cr_suspended") {
    value_set_bool(out, generator->started && !generator->running && !generator->done);
    return true;
  }
  if (name == "gi_frame" || name == "ag_frame" || name == "cr_frame") {
    if (generator->done) {
      value_set_none(out);
      return true;
    }
    if (generator_vm_frame_snapshot(*generator, out)) {
      return true;
    }
    if (auto* function = value_as_function(generator->function)) {
      std::vector<std::pair<Value, Value>> entries;
      if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        const auto& fn = function->module->functions[function->function_id];
        const size_t count = std::min(fn.locals.size(), generator->args.size());
        entries.reserve(count + fn.free_vars.size());
        for (size_t i = 0; i < count; ++i) {
          if (!fn.locals[i].empty() && fn.locals[i][0] != '#' && generator->args[i].tag != ValueTag::Invalid) {
            entries.push_back({Value::string(fn.locals[i]), generator->args[i]});
          }
        }
        for (size_t i = 0; i < fn.free_vars.size() && i < function->closure.size(); ++i) {
          if (fn.free_vars[i].empty() || fn.free_vars[i][0] == '#') {
            continue;
          }
          const Value* free_value = &function->closure[i];
          if (auto* cell = value_as_cell(*free_value)) {
            free_value = &cell->value;
          }
          if (free_value->tag != ValueTag::Invalid) {
            entries.push_back({Value::string(fn.free_vars[i]), *free_value});
          }
        }
      }
      out = Value::frame(
          function->module,
          function->function_id,
          function->globals_module,
          0,
          Value::dict(std::move(entries)),
          Value::none(),
          Value::none());
      if (generator->runtime != nullptr)
        frame_set_generator_owner(*generator->runtime, out, *generator);
      return true;
    }
    value_set_none(out);
    return true;
  }
  if (name == "gi_code" || name == "ag_code" || name == "cr_code") {
    if (auto* function = value_as_function(generator->function)) {
      std::string ignored;
      if (object_get_attr(generator->function, "__code__", out, ignored)) {
        return true;
      }
    }
    value_set_none(out);
    return true;
  }
  if ((name == "__name__" || name == "__qualname__") && generator->function.tag != ValueTag::Invalid) {
    if (auto* function = value_as_function(generator->function)) {
      const std::string& value = name == "__name__"
          ? (function->module != nullptr && function->function_id < function->module->functions.size()
                ? function->module->functions[function->function_id].name : function->qualname)
          : function->qualname;
      out = Value::string(value);
      return true;
    }
  }
  if (name == "cr_origin" && generator->is_coroutine) {
    value_assign_fast(out, generator->origin);
    return true;
  }
  if (name == "cr_await" && generator->is_coroutine) {
    if (generator->awaiting.tag == ValueTag::Invalid) value_set_none(out);
    else value_assign_fast(out, generator->awaiting);
    return true;
  }
  if ((name == "__aiter__" || name == "__anext__" || name == "asend" || name == "athrow" || name == "aclose") &&
      (!generator->is_async || generator->is_coroutine)) {
    return false;
  }
  if (name == "__await__" && !generator->is_coroutine) {
    return false;
  }
  return bind_builtin_method_from_table(object, name, kGeneratorMethods, std::size(kGeneratorMethods), out);
}

} // namespace xlang3
