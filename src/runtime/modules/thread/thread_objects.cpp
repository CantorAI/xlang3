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
#include "thread_objects.h"

#include "xlang3/interpreter.h"
#include "xlang3/object_model.h"
#include "runtime_lock.h"

#include <functional>
#include <memory>

namespace xlang3 {

namespace {

constexpr const char* kThreadNativeType = "threading.Thread";
constexpr const char* kLockNativeType = "_thread.LockType";

XlangThreadState* thread_state_from_self(const Value& self, std::string& error) {
  auto* state = static_cast<XlangThreadState*>(instance_get_native_data(self, kThreadNativeType));
  if (state == nullptr) {
    error = "invalid Thread object";
  }
  return state;
}

XlangLockState* lock_state_from_self(const Value& self, std::string& error) {
  auto* state = static_cast<XlangLockState*>(instance_get_native_data(self, kLockNativeType));
  if (state == nullptr) {
    error = "invalid lock object";
  }
  return state;
}

bool is_none(const Value& value) {
  return value.tag == ValueTag::None;
}

bool parse_thread_init_args(
    const Value* args,
    uint32_t argc,
    Value& target,
    std::vector<Value>& thread_args,
    std::string& error) {
  if (argc == 2 || argc == 3) {
    value_assign_fast(target, args[1]);
    if (argc == 3 && !xlang_thread_tuple_to_args(args[2], thread_args, error)) {
      return false;
    }
    return true;
  }

  if (argc >= 4 && argc <= 6) {
    if (!is_none(args[1])) {
      error = "Thread group must be None";
      return false;
    }
    value_assign_fast(target, args[2]);
    if (argc >= 5 && !xlang_thread_tuple_to_args(args[4], thread_args, error)) {
      return false;
    }
    if (argc == 6 && !is_none(args[5])) {
      error = "Thread kwargs are not implemented yet";
      return false;
    }
    return true;
  }

  error = "Thread() expected target/args or CPython positional form";
  return false;
}

bool thread_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "Thread.__init__ expected self";
    return false;
  }

  Value target;
  std::vector<Value> thread_args;
  if (!parse_thread_init_args(args, argc, target, thread_args, error)) {
    return false;
  }
  if (value_as_function(target) == nullptr && value_as_native_function(target) == nullptr) {
    error = "Thread target must be callable";
    return false;
  }

  auto* state = new XlangThreadState();
  state->runtime = &runtime;
  value_assign_fast(state->target, target);
  state->args = std::move(thread_args);
  if (!instance_set_native_data(args[0], kThreadNativeType, state, xlang_thread_state_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool thread_start(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "Thread.start() expected no arguments";
    return false;
  }
  auto* state = thread_state_from_self(args[0], error);
  if (state == nullptr || !xlang_thread_start_state(*state, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool thread_join(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "Thread.join() expected no arguments";
    return false;
  }
  auto* state = thread_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  xlang_thread_join_state(*state);
  if (!state->error.empty()) {
    error = state->error;
    return false;
  }
  value_set_none(out);
  return true;
}

bool thread_is_alive(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "Thread.is_alive() expected no arguments";
    return false;
  }
  auto* state = thread_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  value_set_bool(out, xlang_thread_is_alive_state(*state));
  return true;
}

bool lock_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "LockType.__init__ expected no arguments";
    return false;
  }
  auto* state = new XlangLockState();
  if (!instance_set_native_data(args[0], kLockNativeType, state, xlang_lock_state_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool lock_acquire(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "lock.acquire() expected no arguments";
    return false;
  }
  auto* state = lock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->cv.wait(lock, [state]() { return !state->locked; });
    state->locked = true;
  }
  value_set_bool(out, true);
  return true;
}

bool lock_release(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "lock.release() expected no arguments";
    return false;
  }
  auto* state = lock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->locked) {
      error = "release unlocked lock";
      return false;
    }
    state->locked = false;
  }
  state->cv.notify_one();
  value_set_none(out);
  return true;
}

bool lock_locked(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "lock.locked() expected no arguments";
    return false;
  }
  auto* state = lock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    value_set_bool(out, state->locked);
  }
  return true;
}

} // namespace

int64_t xlang_thread_current_ident() {
  return static_cast<int64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0x7fffffffffffffffll);
}

bool xlang_thread_tuple_to_args(const Value& value, std::vector<Value>& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Tuple) {
    error = "thread args must be a tuple";
    return false;
  }
  auto* tuple = reinterpret_cast<TupleObject*>(value.as.obj);
  out = tuple->items;
  return true;
}

bool xlang_thread_start_state(XlangThreadState& state, std::string& error) {
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.started) {
      error = "threads can only be started once";
      return false;
    }
    state.started = true;
  }

  state.worker = std::thread([&state]() {
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      state.ident = xlang_thread_current_ident();
    }
    Interpreter interpreter(*state.runtime);
    CallArgsView call_args;
    call_args.leading = state.args.empty() ? nullptr : state.args.data();
    call_args.leading_count = static_cast<uint32_t>(state.args.size());
    RuntimeResult result;
    if (auto* fn = value_as_function(state.target)) {
      result = interpreter.run_function_value(fn, call_args);
    } else if (auto* native = value_as_native_function(state.target)) {
      Value ignored;
      std::string error;
      std::unique_lock<std::recursive_mutex> execution_lock(xlang_runtime_execution_lock());
      if (!native->callback(
              *state.runtime,
              call_args.leading,
              call_args.leading_count,
              ignored,
              error,
              native->user_data)) {
        result.errors.push_back(error.empty() ? "native thread target failed" : error);
      }
    } else {
      result.errors.push_back("thread target is not callable");
    }
    {
      std::lock_guard<std::mutex> lock(state.mutex);
      if (!result.errors.empty()) {
        state.error = result.errors.front();
      }
      state.done = true;
    }
    state.done_cv.notify_all();
  });
  return true;
}

bool xlang_thread_start_detached(
    Runtime& runtime,
    Value target,
    std::vector<Value> args,
    int64_t& ident,
    std::string& error) {
  auto state = std::make_shared<XlangThreadState>();
  state->runtime = &runtime;
  value_assign_fast(state->target, target);
  state->args = std::move(args);

  std::thread worker([state]() {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->ident = xlang_thread_current_ident();
      state->started = true;
    }
    state->done_cv.notify_all();

    Interpreter interpreter(*state->runtime);
    CallArgsView call_args;
    call_args.leading = state->args.empty() ? nullptr : state->args.data();
    call_args.leading_count = static_cast<uint32_t>(state->args.size());
    if (auto* fn = value_as_function(state->target)) {
      (void)interpreter.run_function_value(fn, call_args);
    } else if (auto* native = value_as_native_function(state->target)) {
      Value ignored;
      std::string callback_error;
      std::unique_lock<std::recursive_mutex> execution_lock(xlang_runtime_execution_lock());
      (void)native->callback(
          *state->runtime,
          call_args.leading,
          call_args.leading_count,
          ignored,
          callback_error,
          native->user_data);
    }
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->done = true;
    }
    state->done_cv.notify_all();
  });

  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->done_cv.wait(lock, [&state]() { return state->started; });
    ident = state->ident;
  }
  worker.detach();
  return true;
}

void xlang_thread_join_state(XlangThreadState& state) {
  std::thread worker;
  {
    std::unique_lock<std::mutex> lock(state.mutex);
    state.done_cv.wait(lock, [&state]() { return !state.started || state.done; });
    if (state.worker.joinable()) {
      worker = std::move(state.worker);
    }
  }
  if (worker.joinable()) {
    worker.join();
  }
}

bool xlang_thread_is_alive_state(XlangThreadState& state) {
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.started && !state.done;
}

void xlang_thread_state_cleanup(void* data) {
  auto* state = static_cast<XlangThreadState*>(data);
  if (state == nullptr) {
    return;
  }
  if (state->worker.joinable()) {
    state->worker.detach();
  }
  delete state;
}

void xlang_lock_state_cleanup(void* data) {
  delete static_cast<XlangLockState*>(data);
}

Value xlang_thread_make_thread_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("threading.Thread.__init__", thread_init)});
  attrs.push_back({"start", runtime.make_native_function("threading.Thread.start", thread_start)});
  attrs.push_back({"join", runtime.make_native_function("threading.Thread.join", thread_join)});
  attrs.push_back({"is_alive", runtime.make_native_function("threading.Thread.is_alive", thread_is_alive)});
  return Value::class_object("Thread", std::move(attrs));
}

Value xlang_thread_make_lock_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("_thread.LockType.__init__", lock_init)});
  attrs.push_back({"acquire", runtime.make_native_function("_thread.LockType.acquire", lock_acquire)});
  attrs.push_back({"release", runtime.make_native_function("_thread.LockType.release", lock_release)});
  attrs.push_back({"locked", runtime.make_native_function("_thread.LockType.locked", lock_locked)});
  return Value::class_object("LockType", std::move(attrs));
}

Value xlang_thread_make_lock_instance(Runtime& runtime) {
  Value lock_class = xlang_thread_make_lock_class(runtime);
  Value instance = Value::instance(lock_class);
  std::string error;
  auto* state = new XlangLockState();
  if (!instance_set_native_data(instance, kLockNativeType, state, xlang_lock_state_cleanup, error)) {
    delete state;
    return Value::invalid();
  }
  return instance;
}

} // namespace xlang3
