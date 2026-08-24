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
#include "task_objects.h"

#include "runtime_lock.h"
#include "xlang3/generator.h"
#include "xlang3/interpreter.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <memory>

namespace xlang3 {

namespace {

constexpr const char* kTaskNativeType = "task.Task";

XlangTaskState* task_state_from_value(const Value& value, std::string& error) {
  auto* state = static_cast<XlangTaskState*>(instance_get_native_data(value, kTaskNativeType));
  if (state == nullptr) {
    error = "invalid Task object";
  }
  return state;
}

bool task_check_argc(uint32_t argc, uint32_t expected, const char* name, std::string& error) {
  if (argc == expected) {
    return true;
  }
  error = std::string(name) + " expected " + std::to_string(expected) + " arguments, got " + std::to_string(argc);
  return false;
}

bool task_target_is_callable(const Value& value) {
  return value_as_function(value) != nullptr || value_as_native_function(value) != nullptr;
}

RuntimeResult task_call_target(XlangTaskState& state) {
  RuntimeResult result;
  CallArgsView call_args;
  call_args.leading = state.args.empty() ? nullptr : state.args.data();
  call_args.leading_count = static_cast<uint32_t>(state.args.size());

  if (auto* fn = value_as_function(state.target)) {
    Interpreter interpreter(*state.runtime);
    return interpreter.run_function_value(fn, call_args);
  }

  if (auto* native = value_as_native_function(state.target)) {
    std::string error;
    XlangRuntimeExecutionGuard execution_guard;
    const bool ok = native->fast_callback != nullptr
        ? native->fast_callback(
              *state.runtime,
              call_args.leading,
              call_args.leading_count,
              nullptr,
              nullptr,
              0,
              result.value,
              error,
              native->user_data)
        : native->callback(
              *state.runtime,
              call_args.leading,
              call_args.leading_count,
              result.value,
              error,
              native->user_data);
    if (!ok) {
      result.errors.push_back(error.empty() ? "native task target failed" : error);
    }
    return result;
  }

  result.errors.push_back("task target is not callable");
  return result;
}

bool task_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc < 2 || argc > 3) {
    error = "Task() expected callable and optional args tuple";
    return false;
  }
  if (value_as_instance(args[0]) == nullptr) {
    error = "Task.__init__ expected self";
    return false;
  }
  if (!task_target_is_callable(args[1])) {
    error = "Task target must be callable";
    return false;
  }

  auto* state = new XlangTaskState();
  state->runtime = &runtime;
  value_assign_fast(state->target, args[1]);
  if (argc == 3 && !xlang_task_tuple_to_args(args[2], state->args, error)) {
    delete state;
    return false;
  }
  if (!instance_set_native_data(args[0], kTaskNativeType, state, xlang_task_state_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool task_start(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (!task_check_argc(argc, 1, "Task.start()", error)) {
    return false;
  }
  auto* state = task_state_from_value(args[0], error);
  if (state == nullptr || !xlang_task_start_state(*state, error)) {
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool task_join(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (!task_check_argc(argc, 1, "Task.join()", error)) {
    return false;
  }
  return xlang_task_join_value(args[0], out, error);
}

bool task_result(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (!task_check_argc(argc, 1, "Task.result()", error)) {
    return false;
  }
  return xlang_task_join_value(args[0], out, error);
}

bool task_done(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (!task_check_argc(argc, 1, "Task.done()", error)) {
    return false;
  }
  bool done = false;
  if (!xlang_task_done_value(args[0], done, error)) {
    return false;
  }
  value_set_bool(out, done);
  return true;
}

} // namespace

bool xlang_task_tuple_to_args(const Value& value, std::vector<Value>& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Tuple) {
    error = "task args must be a tuple";
    return false;
  }
  auto* tuple = reinterpret_cast<TupleObject*>(value.as.obj);
  out = tuple->items;
  return true;
}

bool xlang_task_start_state(XlangTaskState& state, std::string& error) {
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.started) {
      error = "tasks can only be started once";
      return false;
    }
    state.started = true;
  }

  state.worker = std::thread([&state]() {
    RuntimeResult result = task_call_target(state);
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      if (!result.errors.empty()) {
        state.error = result.errors.front();
      } else {
        value_assign_fast(state.result, result.value);
      }
      state.done = true;
    }
    state.done_cv.notify_all();
  });
  return true;
}

bool xlang_task_join_state(XlangTaskState& state, Value& out, std::string& error) {
  std::thread worker;
  {
    std::unique_lock<std::mutex> lock(state.mutex);
    if (!state.started) {
      error = "Task has not been started";
      return false;
    }
    state.done_cv.wait(lock, [&state]() { return !state.started || state.done; });
    if (state.worker.joinable()) {
      worker = std::move(state.worker);
    }
  }
  if (worker.joinable()) {
    worker.join();
  }
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.error.empty()) {
      error = state.error;
      return false;
    }
    value_assign_fast(out, state.result);
  }
  return true;
}

bool xlang_task_is_done_state(XlangTaskState& state) {
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.done;
}

void xlang_task_state_cleanup(void* data) {
  auto* state = static_cast<XlangTaskState*>(data);
  if (state == nullptr) {
    return;
  }
  if (state->worker.joinable()) {
    if (state->worker.get_id() == std::this_thread::get_id()) {
      state->worker.detach();
    } else {
      state->worker.join();
    }
  }
  delete state;
}

bool xlang_task_spawn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (argc < 1 || argc > 2) {
    error = "task.spawn() expected callable and optional args tuple";
    return false;
  }
  if (!task_target_is_callable(args[0])) {
    error = "task.spawn() first argument must be callable";
    return false;
  }

  Value task_class = xlang_task_make_task_class(runtime);
  out = Value::instance(task_class);
  auto* state = new XlangTaskState();
  state->runtime = &runtime;
  value_assign_fast(state->target, args[0]);
  if (argc == 2 && !xlang_task_tuple_to_args(args[1], state->args, error)) {
    delete state;
    return false;
  }
  if (!instance_set_native_data(out, kTaskNativeType, state, xlang_task_state_cleanup, error)) {
    delete state;
    return false;
  }
  if (!xlang_task_start_state(*state, error)) {
    return false;
  }
  return true;
}

bool xlang_task_completed(Runtime& runtime, const Value& result, Value& out, std::string& error) {
  Value task_class = xlang_task_make_task_class(runtime);
  out = Value::instance(task_class);
  auto* state = new XlangTaskState();
  state->runtime = &runtime;
  state->started = true;
  state->done = true;
  value_assign_fast(state->result, result);
  if (!instance_set_native_data(out, kTaskNativeType, state, xlang_task_state_cleanup, error)) {
    delete state;
    return false;
  }
  return true;
}

bool xlang_task_await_value(Runtime& runtime, const Value& value, Value& out, std::string& error) {
  if (async_generator_awaitable_await(runtime, value, out, error)) {
    return true;
  }
  if (!error.empty()) {
    return false;
  }
  std::string task_error;
  if (task_state_from_value(value, task_error) == nullptr) {
    value_assign_fast(out, value);
    return true;
  }
  return xlang_task_join_value(value, out, error);
}

bool xlang_task_join_value(const Value& task, Value& out, std::string& error) {
  auto* state = task_state_from_value(task, error);
  if (state == nullptr) {
    return false;
  }
  return xlang_task_join_state(*state, out, error);
}

bool xlang_task_done_value(const Value& task, bool& done, std::string& error) {
  auto* state = task_state_from_value(task, error);
  if (state == nullptr) {
    return false;
  }
  done = xlang_task_is_done_state(*state);
  return true;
}

bool xlang_task_await_all(Runtime& runtime, const Value& tasks, Value& out, std::string& error) {
  (void)runtime;
  auto* list = value_as_list(tasks);
  if (list == nullptr) {
    error = "task.await_all() expected a list of Task objects";
    return false;
  }

  std::vector<Value> results;
  results.reserve(list->items.size());
  for (const auto& item : list->items) {
    Value result;
    if (!xlang_task_await_value(runtime, item, result, error)) {
      return false;
    }
    results.push_back(result);
  }
  out = Value::list(std::move(results));
  return true;
}

Value xlang_task_make_task_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("task.Task.__init__", task_init)});
  attrs.push_back({"start", runtime.make_native_function("task.Task.start", task_start)});
  attrs.push_back({"join", runtime.make_native_function("task.Task.join", task_join)});
  attrs.push_back({"result", runtime.make_native_function("task.Task.result", task_result)});
  attrs.push_back({"done", runtime.make_native_function("task.Task.done", task_done)});
  return Value::class_object("Task", std::move(attrs));
}

} // namespace xlang3
