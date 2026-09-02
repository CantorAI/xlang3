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
#include "xlang3/interpreter.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"

#ifndef XLANG3_EMBEDDED
#include "task_objects.h"
#endif

namespace xlang3 {

namespace {

template <typename T>
T* allocate_generator_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(kind);
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
  v.as.obj = &obj->header;
  return v;
}

void generator_release_object(Object* object) {
  if (object->kind == ObjectKind::Generator) {
    auto* generator = reinterpret_cast<GeneratorObject*>(object);
    if (generator->vm_state_cleanup != nullptr && generator->vm_state != nullptr) {
      generator->vm_state_cleanup(generator->vm_state);
    }
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
  if (auto* generator = value_as_generator(value); generator != nullptr && generator->is_coroutine) {
    return "<coroutine object>";
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

bool generator_send(Value& generator, Value value, bool& done, Value& out, std::string& error) {
  auto* obj = value_as_generator(generator);
  if (obj == nullptr) {
    error = "invalid generator";
    return false;
  }
  if (obj->done) {
    done = true;
    value_assign_fast(out, obj->return_value);
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
  obj->started = true;
  value_assign_fast(obj->pending_send, value);
  obj->has_pending_send = true;
  Interpreter interpreter(*obj->runtime);
  obj->running = true;
  RuntimeResult result = interpreter.resume_generator(*obj, out, done);
  obj->running = false;
  if (done) {
    obj->done = true;
  }
  if (!result.errors.empty()) {
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
  if (obj->vm_state_cleanup != nullptr && obj->vm_state != nullptr) {
    obj->vm_state_cleanup(obj->vm_state);
  }
  obj->vm_state = nullptr;
  obj->vm_state_cleanup = nullptr;
  obj->done = true;
  value_set_invalid(obj->pending_send);
  value_set_invalid(obj->pending_throw);
  obj->has_pending_send = false;
  obj->has_pending_throw = false;
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
  if (obj->done || obj->vm_state == nullptr) {
    obj->runtime->set_active_exception(exception);
    value_assign_fast(out, exception);
    error = value_to_string(exception);
    return false;
  }
  if (!emit_generator_throw_monitoring_event(*obj, exception, error)) {
    return false;
  }
  value_assign_fast(obj->pending_throw, exception);
  obj->has_pending_throw = true;
  obj->started = true;
  Interpreter interpreter(*obj->runtime);
  bool done = false;
  obj->running = true;
  RuntimeResult result = interpreter.resume_generator(*obj, out, done);
  obj->running = false;
  if (done) {
    obj->done = true;
  }
  if (!result.errors.empty()) {
    error = result.errors.front();
    return false;
  }
  return true;
}

bool async_generator_awaitable_await(Runtime& runtime, const Value& value, Value& out, std::string& error) {
  auto* state = value_as_async_generator_awaitable(value);
  if (state == nullptr) {
    error.clear();
    return false;
  }
  if (state->consumed) {
    error = "cannot reuse already awaited async generator awaitable";
    return false;
  }
  state->consumed = true;

  bool done = false;
  switch (state->kind) {
    case AsyncGenAwaitableKind::ANext:
      if (!generator_send(state->generator, Value::none(), done, out, error)) {
        return false;
      }
      break;
    case AsyncGenAwaitableKind::ASend:
      if (state->args.empty()) {
        error = "async_generator.asend missing value";
        return false;
      }
      if (!generator_send(state->generator, state->args[0], done, out, error)) {
        return false;
      }
      break;
    case AsyncGenAwaitableKind::AThrow:
      if (!generator_throw(
              state->generator,
              state->args.data(),
              static_cast<uint32_t>(state->args.size()),
              out,
              error)) {
        if (value_as_instance(out) != nullptr) {
          runtime.set_pending_exception(out);
        }
        if (error.empty()) {
          error = "async generator throw failed";
        }
        return false;
      }
      return true;
    case AsyncGenAwaitableKind::AClose:
      return generator_close(state->generator, out, error);
  }

  if (done) {
    error = "async generator exhausted";
    runtime.raise_class_error("StopAsyncIteration", error);
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
  if (!async_generator_awaitable_await(runtime, args[0], out, error)) {
    if (runtime.active_exception().tag == ValueTag::Invalid && error.empty()) {
      runtime.raise_class_error("RuntimeError", "async generator await failed");
    }
    return false;
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
  return async_generator_awaitable_next_method(runtime, args, 1, out, error, nullptr);
}

bool generator_send_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "generator.send", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value generator = args[0];
  bool done = false;
  if (!generator_send(generator, args[1], done, out, error)) {
    runtime.raise_class_error("TypeError", error);
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
    runtime.raise_class_error("TypeError", error);
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

static constexpr BuiltinMethodSpec kGeneratorMethods[] = {
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

static constexpr BuiltinMethodSpec kAsyncGeneratorAwaitableMethods[] = {
      {"__await__", "async_generator_awaitable.__await__", async_generator_awaitable_await_method},
      {"__iter__", "async_generator_awaitable.__iter__", async_generator_awaitable_await_method},
      {"__next__", "async_generator_awaitable.__next__", async_generator_awaitable_next_method},
      {"send", "async_generator_awaitable.send", async_generator_awaitable_send_method},
};

} // namespace

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
    if (auto* function = value_as_function(generator->function)) {
      out = Value::frame(
          function->module,
          function->function_id,
          function->globals_module,
          0,
          Value::dict({}),
          Value::none(),
          Value::none());
      return true;
    }
    value_set_none(out);
    return true;
  }
  if (name == "gi_code" || name == "ag_code" || name == "cr_code") {
    if (auto* function = value_as_function(generator->function)) {
      out = Value::code(function->module, function->function_id);
      return true;
    }
    value_set_none(out);
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
