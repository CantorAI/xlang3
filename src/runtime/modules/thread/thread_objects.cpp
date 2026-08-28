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

#include <cstdio>
#include <chrono>
#include <functional>
#include <memory>

namespace xlang3 {

namespace {

constexpr const char* kThreadNativeType = "threading.Thread";
constexpr const char* kLockNativeType = "_thread.LockType";
constexpr const char* kRLockNativeType = "_thread.RLock";

using XlangThreadStateHandle = std::shared_ptr<XlangThreadState>;

std::mutex g_thread_registry_mutex;
std::vector<std::shared_ptr<XlangThreadState>> g_thread_registry;

XlangThreadStateHandle* thread_state_handle_from_self(const Value& self, std::string& error) {
  auto* handle = static_cast<XlangThreadStateHandle*>(instance_get_native_data(self, kThreadNativeType));
  if (handle == nullptr || !*handle) {
    error = "invalid Thread object";
    return nullptr;
  }
  return handle;
}

XlangThreadState* thread_state_from_self(const Value& self, std::string& error) {
  auto* handle = thread_state_handle_from_self(self, error);
  return handle == nullptr ? nullptr : handle->get();
}

XlangLockState* lock_state_from_self(const Value& self, std::string& error) {
  auto* state = static_cast<XlangLockState*>(instance_get_native_data(self, kLockNativeType));
  if (state == nullptr) {
    error = "invalid lock object";
  }
  return state;
}

XlangRLockState* rlock_state_from_self(const Value& self, std::string& error) {
  auto* state = static_cast<XlangRLockState*>(instance_get_native_data(self, kRLockNativeType));
  if (state == nullptr) {
    error = "invalid rlock object";
  }
  return state;
}

bool is_none(const Value& value) {
  return value.tag == ValueTag::None;
}

bool parse_blocking_arg(const Value* args, uint32_t argc, bool& blocking) {
  blocking = true;
  if (argc >= 2) {
    blocking = value_truthy(args[1]);
  }
  return true;
}

void report_thread_error(const std::string& error) {
  if (error.empty()) {
    return;
  }
  std::fprintf(stderr, "Exception in thread: %s\n", error.c_str());
  std::fflush(stderr);
}

bool parse_lock_acquire_args(
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    bool& blocking,
    std::string& error) {
  blocking = true;
  if (argc < 1 || argc > 3) {
    error = "lock.acquire() expected optional blocking/timeout";
    return false;
  }
  parse_blocking_arg(args, argc, blocking);
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      error = "lock.acquire() received invalid keyword argument";
      return false;
    }
    if (name == "blocking") {
      blocking = value_truthy(*kwargs[i].value);
    } else if (name == "timeout") {
      // Accepted for CPython shape. Timed waits are wired later; blocking=False
      // already takes the non-blocking fast path used by debug adapters.
    } else {
      error = "lock.acquire() got an unexpected keyword argument '" + name + "'";
      return false;
    }
  }
  return true;
}

bool parse_thread_init_args(
    const Value* args,
    uint32_t argc,
    Value& target,
    std::vector<Value>& thread_args,
    std::string& name,
    bool& daemon,
    std::string& error) {
  if (argc == 1) {
    value_set_none(target);
    return true;
  }

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
    if (argc >= 4 && !is_none(args[3])) {
      if (auto* str = value_as_string(args[3])) {
        name = string_object_to_string(*str);
      } else {
        name = value_to_string(args[3]);
      }
    }
    if (argc >= 5 && !xlang_thread_tuple_to_args(args[4], thread_args, error)) {
      return false;
    }
    if (argc == 6) {
      daemon = value_truthy(args[5]);
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
  std::string name;
  bool daemon = false;
  if (!parse_thread_init_args(args, argc, target, thread_args, name, daemon, error)) {
    return false;
  }
  if (target.tag != ValueTag::None && value_as_function(target) == nullptr && value_as_native_function(target) == nullptr &&
      value_as_bound_method(target) == nullptr) {
    error = "Thread target must be callable";
    return false;
  }

  auto* handle = new XlangThreadStateHandle(std::make_shared<XlangThreadState>());
  auto& state = **handle;
  state.runtime = &runtime;
  value_assign_fast(state.target, target);
  state.args = std::move(thread_args);
  state.name = name.empty() ? "Thread" : name;
  state.daemon = daemon;
  if (!instance_set_native_data(args[0], kThreadNativeType, handle, xlang_thread_state_cleanup, error)) {
    delete handle;
    return false;
  }
  std::string ignored;
  object_set_attr(const_cast<Value&>(args[0]), "_is_stopped", Value::boolean(true), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "name", Value::string(state.name), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "daemon", Value::boolean(state.daemon), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "ident", Value::none(), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "native_id", Value::none(), ignored);
  value_set_none(out);
  return true;
}

bool thread_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 1) {
    return thread_init(runtime, args, argc, out, error, user_data);
  }
  Value target = Value::none();
  std::vector<Value> thread_args;
  std::string name;
  bool daemon = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string kw_name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      continue;
    }
    if (kw_name == "target") {
      value_assign_fast(target, *kwargs[i].value);
    } else if (kw_name == "args") {
      if (!xlang_thread_tuple_to_args(*kwargs[i].value, thread_args, error)) {
        return false;
      }
    } else if (kw_name == "kwargs") {
      if (kwargs[i].value->tag != ValueTag::None) {
        error = "Thread kwargs are not implemented yet";
        return false;
      }
    } else if (kw_name == "name") {
      if (auto* str = value_as_string(*kwargs[i].value)) {
        name = string_object_to_string(*str);
      } else if (kwargs[i].value->tag != ValueTag::None) {
        name = value_to_string(*kwargs[i].value);
      }
    } else if (kw_name == "daemon") {
      daemon = value_truthy(*kwargs[i].value);
    }
  }
  if (target.tag != ValueTag::None &&
      value_as_function(target) == nullptr &&
      value_as_native_function(target) == nullptr &&
      value_as_bound_method(target) == nullptr) {
    error = "Thread target must be callable";
    return false;
  }
  auto* handle = new XlangThreadStateHandle(std::make_shared<XlangThreadState>());
  auto& state = **handle;
  state.runtime = &runtime;
  value_assign_fast(state.target, target);
  state.args = std::move(thread_args);
  state.name = name.empty() ? "Thread" : name;
  state.daemon = daemon;
  if (!instance_set_native_data(args[0], kThreadNativeType, handle, xlang_thread_state_cleanup, error)) {
    delete handle;
    return false;
  }
  std::string ignored;
  object_set_attr(const_cast<Value&>(args[0]), "_is_stopped", Value::boolean(true), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "name", Value::string(state.name), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "daemon", Value::boolean(state.daemon), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "ident", Value::none(), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "native_id", Value::none(), ignored);
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
  auto* handle = thread_state_handle_from_self(args[0], error);
  if (handle == nullptr || !xlang_thread_start_state(*handle, error)) {
    return false;
  }
  std::string ignored;
  object_set_attr(const_cast<Value&>(args[0]), "_is_stopped", Value::boolean(false), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "ident", Value::int64((*handle)->ident), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "native_id", Value::int64((*handle)->ident), ignored);
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
  if (argc < 1 || argc > 2) {
    error = "Thread.join() expected optional timeout";
    return false;
  }
  auto* state = thread_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (argc == 2 && args[1].tag != ValueTag::None) {
    double seconds = 0.0;
    if (args[1].tag == ValueTag::Int64) {
      seconds = static_cast<double>(args[1].as.i64);
    } else if (args[1].tag == ValueTag::Double) {
      seconds = args[1].as.f64;
    } else {
      error = "Thread.join() timeout must be a number or None";
      return false;
    }
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->done) {
      state->done_cv.wait_for(lock, std::chrono::duration<double>(seconds), [state]() { return !state->started || state->done; });
    }
    if (!state->done) {
      value_set_none(out);
      return true;
    }
  }
  xlang_thread_join_state(*state);
  std::string ignored;
  object_set_attr(const_cast<Value&>(args[0]), "_is_stopped", Value::boolean(true), ignored);
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

bool lock_release(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data);

bool lock_acquire(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  bool blocking = true;
  if (!parse_lock_acquire_args(args, argc, nullptr, 0, blocking, error)) {
    return false;
  }
  auto* state = lock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!blocking && state->locked) {
      value_set_bool(out, false);
      return true;
    }
    state->cv.wait(lock, [state]() { return !state->locked; });
    state->locked = true;
  }
  value_set_bool(out, true);
  return true;
}

bool lock_acquire_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  bool blocking = true;
  if (!parse_lock_acquire_args(args, argc, kwargs, kwargc, blocking, error)) {
    return false;
  }
  auto* state = lock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!blocking && state->locked) {
      value_set_bool(out, false);
      return true;
    }
    state->cv.wait(lock, [state]() { return !state->locked; });
    state->locked = true;
  }
  value_set_bool(out, true);
  return true;
}

bool lock_enter(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  Value acquired;
  if (!lock_acquire(runtime, args, argc, acquired, error, user_data)) {
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool lock_exit(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 4) {
    error = "lock.__exit__() expected exc_type, exc_val, exc_tb";
    return false;
  }
  if (!lock_release(runtime, args, 1, out, error, user_data)) {
    return false;
  }
  value_set_bool(out, false);
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

bool rlock_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "RLock.__init__ expected no arguments";
    return false;
  }
  auto* state = new XlangRLockState();
  if (!instance_set_native_data(args[0], kRLockNativeType, state, xlang_rlock_state_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool rlock_acquire(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  bool blocking = true;
  if (!parse_lock_acquire_args(args, argc, nullptr, 0, blocking, error)) {
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  const auto current = std::this_thread::get_id();
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (state->depth != 0 && state->owner == current) {
      ++state->depth;
      value_set_bool(out, true);
      return true;
    }
    if (!blocking && state->depth != 0) {
      value_set_bool(out, false);
      return true;
    }
    state->cv.wait(lock, [state]() { return state->depth == 0; });
    state->owner = current;
    state->depth = 1;
  }
  value_set_bool(out, true);
  return true;
}

bool rlock_acquire_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  bool blocking = true;
  if (!parse_lock_acquire_args(args, argc, kwargs, kwargc, blocking, error)) {
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  const auto current = std::this_thread::get_id();
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (state->depth != 0 && state->owner == current) {
      ++state->depth;
      value_set_bool(out, true);
      return true;
    }
    if (!blocking && state->depth != 0) {
      value_set_bool(out, false);
      return true;
    }
    state->cv.wait(lock, [state]() { return state->depth == 0; });
    state->owner = current;
    state->depth = 1;
  }
  value_set_bool(out, true);
  return true;
}

bool rlock_release(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "RLock.release() expected no arguments";
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  bool notify = false;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->depth == 0 || state->owner != std::this_thread::get_id()) {
      error = "cannot release un-acquired lock";
      return false;
    }
    --state->depth;
    if (state->depth == 0) {
      state->owner = std::thread::id();
      notify = true;
    }
  }
  if (notify) {
    state->cv.notify_one();
  }
  value_set_none(out);
  return true;
}

bool rlock_enter(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  Value acquired;
  if (!rlock_acquire(runtime, args, argc, acquired, error, user_data)) {
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool rlock_exit(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 4) {
    error = "RLock.__exit__() expected exc_type, exc_val, exc_tb";
    return false;
  }
  if (!rlock_release(runtime, args, 1, out, error, user_data)) {
    return false;
  }
  value_set_bool(out, false);
  return true;
}

} // namespace

bool xlang_lock_acquire_value(const Value& lock_value, bool blocking, std::string& error) {
  if (auto* state = static_cast<XlangRLockState*>(instance_get_native_data(lock_value, "_thread.RLock"))) {
    const auto current = std::this_thread::get_id();
    std::unique_lock<std::mutex> lock(state->mutex);
    if (state->depth != 0 && state->owner == current) {
      ++state->depth;
      return true;
    }
    if (!blocking && state->depth != 0) {
      return false;
    }
    state->cv.wait(lock, [state]() { return state->depth == 0; });
    state->owner = current;
    state->depth = 1;
    return true;
  }
  if (auto* state = static_cast<XlangLockState*>(instance_get_native_data(lock_value, "_thread.LockType"))) {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!blocking && state->locked) {
      return false;
    }
    state->cv.wait(lock, [state]() { return !state->locked; });
    state->locked = true;
    return true;
  }
  error = "invalid lock object";
  return false;
}

bool xlang_lock_release_value(const Value& lock_value, std::string& error) {
  if (auto* state = static_cast<XlangRLockState*>(instance_get_native_data(lock_value, "_thread.RLock"))) {
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->depth == 0 || state->owner != std::this_thread::get_id()) {
        error = "cannot release un-acquired lock";
        return false;
      }
      --state->depth;
      if (state->depth == 0) {
        state->owner = std::thread::id();
        notify = true;
      }
    }
    if (notify) {
      state->cv.notify_one();
    }
    return true;
  }
  if (auto* state = static_cast<XlangLockState*>(instance_get_native_data(lock_value, "_thread.LockType"))) {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (!state->locked) {
        error = "release unlocked lock";
        return false;
      }
      state->locked = false;
    }
    state->cv.notify_one();
    return true;
  }
  error = "invalid lock object";
  return false;
}

int64_t xlang_thread_current_ident() {
  return static_cast<int64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0x7fffffffffffffffll);
}

size_t xlang_thread_active_count() {
  std::lock_guard<std::mutex> registry_lock(g_thread_registry_mutex);
  size_t count = 1;
  auto it = g_thread_registry.begin();
  while (it != g_thread_registry.end()) {
    auto state = *it;
    if (!state) {
      it = g_thread_registry.erase(it);
      continue;
    }
    if (xlang_thread_is_alive_state(*state)) {
      ++count;
    }
    ++it;
  }
  return count;
}

std::vector<int64_t> xlang_thread_active_idents() {
  std::lock_guard<std::mutex> registry_lock(g_thread_registry_mutex);
  std::vector<int64_t> idents;
  idents.push_back(xlang_thread_current_ident());
  auto it = g_thread_registry.begin();
  while (it != g_thread_registry.end()) {
    auto state = *it;
    if (!state) {
      it = g_thread_registry.erase(it);
      continue;
    }
    if (xlang_thread_is_alive_state(*state)) {
      std::lock_guard<std::mutex> state_lock(state->mutex);
      if (state->ident != 0) {
        idents.push_back(state->ident);
      }
    }
    ++it;
  }
  return idents;
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

bool xlang_thread_start_state(std::shared_ptr<XlangThreadState> state, std::string& error) {
  if (!state) {
    error = "invalid Thread object";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->started) {
      error = "threads can only be started once";
      return false;
    }
    state->started = true;
  }

  state->worker = std::thread([state]() {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->ident = xlang_thread_current_ident();
    }
    state->done_cv.notify_all();
    if (state->runtime->thread_trace_function().tag != ValueTag::Invalid &&
        state->runtime->thread_trace_function().tag != ValueTag::None) {
      state->runtime->set_trace_function(state->runtime->thread_trace_function());
    }
    if (state->runtime->thread_profile_function().tag != ValueTag::Invalid &&
        state->runtime->thread_profile_function().tag != ValueTag::None) {
      state->runtime->set_profile_function(state->runtime->thread_profile_function());
    }
    Interpreter interpreter(*state->runtime);
    CallArgsView call_args;
    call_args.leading = state->args.empty() ? nullptr : state->args.data();
    call_args.leading_count = static_cast<uint32_t>(state->args.size());
    RuntimeResult result;
    if (auto* fn = value_as_function(state->target)) {
      result = interpreter.run_function_value(fn, call_args);
    } else if (auto* bound = value_as_bound_method(state->target)) {
      std::vector<Value> bound_args;
      bound_args.reserve(state->args.size() + 1);
      bound_args.push_back(bound->self);
      for (const auto& arg : state->args) {
        bound_args.push_back(arg);
      }
      CallArgsView bound_call_args;
      bound_call_args.leading = bound_args.data();
      bound_call_args.leading_count = static_cast<uint32_t>(bound_args.size());
      if (auto* fn = value_as_function(bound->function)) {
        result = interpreter.run_function_value(fn, bound_call_args);
      } else if (auto* native = value_as_native_function(bound->function)) {
        Value ignored;
        std::string error;
        XlangRuntimeExecutionGuard execution_lock;
        if (!native->callback(
                *state->runtime,
                bound_call_args.leading,
                bound_call_args.leading_count,
                ignored,
                error,
                native->user_data)) {
          result.errors.push_back(error.empty() ? "native thread target failed" : error);
        }
      } else {
        result.errors.push_back("bound thread target is not callable");
      }
    } else if (auto* native = value_as_native_function(state->target)) {
      Value ignored;
      std::string error;
      XlangRuntimeExecutionGuard execution_lock;
      if (!native->callback(
              *state->runtime,
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
      std::lock_guard<std::mutex> lock(state->mutex);
      if (!result.errors.empty()) {
        state->error = result.errors.front();
      }
      state->done = true;
    }
    if (!result.errors.empty()) {
      report_thread_error(result.errors.front());
    }
    state->done_cv.notify_all();
  });
  {
    std::lock_guard<std::mutex> registry_lock(g_thread_registry_mutex);
    g_thread_registry.push_back(state);
  }
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->done_cv.wait(lock, [&state]() { return state->ident != 0 || state->done; });
  }
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

  state->worker = std::thread([state]() {
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->ident = xlang_thread_current_ident();
      state->started = true;
    }
    state->done_cv.notify_all();

    if (state->runtime->thread_trace_function().tag != ValueTag::Invalid &&
        state->runtime->thread_trace_function().tag != ValueTag::None) {
      state->runtime->set_trace_function(state->runtime->thread_trace_function());
    }
    if (state->runtime->thread_profile_function().tag != ValueTag::Invalid &&
        state->runtime->thread_profile_function().tag != ValueTag::None) {
      state->runtime->set_profile_function(state->runtime->thread_profile_function());
    }
    Interpreter interpreter(*state->runtime);
    CallArgsView call_args;
    call_args.leading = state->args.empty() ? nullptr : state->args.data();
    call_args.leading_count = static_cast<uint32_t>(state->args.size());
    if (auto* fn = value_as_function(state->target)) {
      (void)interpreter.run_function_value(fn, call_args);
    } else if (auto* bound = value_as_bound_method(state->target)) {
      std::vector<Value> bound_args;
      bound_args.reserve(state->args.size() + 1);
      bound_args.push_back(bound->self);
      for (const auto& arg : state->args) {
        bound_args.push_back(arg);
      }
      CallArgsView bound_call_args;
      bound_call_args.leading = bound_args.data();
      bound_call_args.leading_count = static_cast<uint32_t>(bound_args.size());
      if (auto* fn = value_as_function(bound->function)) {
        (void)interpreter.run_function_value(fn, bound_call_args);
      } else if (auto* native = value_as_native_function(bound->function)) {
        Value ignored;
        std::string callback_error;
        XlangRuntimeExecutionGuard execution_lock;
        (void)native->callback(
            *state->runtime,
            bound_call_args.leading,
            bound_call_args.leading_count,
            ignored,
            callback_error,
            native->user_data);
      }
    } else if (auto* native = value_as_native_function(state->target)) {
      Value ignored;
      std::string callback_error;
      XlangRuntimeExecutionGuard execution_lock;
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
  {
    std::lock_guard<std::mutex> registry_lock(g_thread_registry_mutex);
    g_thread_registry.push_back(state);
  }
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

void xlang_thread_join_runtime_threads(Runtime* runtime) {
  std::vector<std::shared_ptr<XlangThreadState>> threads;
  {
    std::lock_guard<std::mutex> registry_lock(g_thread_registry_mutex);
    for (const auto& state : g_thread_registry) {
      if (state && state->runtime == runtime) {
        threads.push_back(state);
      }
    }
  }

  for (auto& state : threads) {
    xlang_thread_join_state(*state);
  }

  {
    std::lock_guard<std::mutex> registry_lock(g_thread_registry_mutex);
    auto it = g_thread_registry.begin();
    while (it != g_thread_registry.end()) {
      auto state = *it;
      if (!state || (state->runtime == runtime && !xlang_thread_is_alive_state(*state))) {
        it = g_thread_registry.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void xlang_thread_state_cleanup(void* data) {
  auto* handle = static_cast<XlangThreadStateHandle*>(data);
  if (handle == nullptr) {
    return;
  }
  if (*handle && (*handle)->worker.joinable()) {
    (*handle)->worker.detach();
  }
  delete handle;
}

void xlang_lock_state_cleanup(void* data) {
  delete static_cast<XlangLockState*>(data);
}

void xlang_rlock_state_cleanup(void* data) {
  delete static_cast<XlangRLockState*>(data);
}

Value xlang_thread_make_thread_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("threading.Thread.__init__", thread_init, nullptr, nullptr, nullptr, false, thread_init_kw)});
  attrs.push_back({"start", runtime.make_native_function("threading.Thread.start", thread_start)});
  attrs.push_back({"join", runtime.make_native_function("threading.Thread.join", thread_join)});
  attrs.push_back({"is_alive", runtime.make_native_function("threading.Thread.is_alive", thread_is_alive)});
  return Value::class_object("Thread", std::move(attrs));
}

Value xlang_thread_make_lock_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("_thread.LockType.__init__", lock_init)});
  attrs.push_back({"acquire", runtime.make_native_function("_thread.LockType.acquire", lock_acquire, nullptr, nullptr, nullptr, false, lock_acquire_kw)});
  attrs.push_back({"release", runtime.make_native_function("_thread.LockType.release", lock_release)});
  attrs.push_back({"locked", runtime.make_native_function("_thread.LockType.locked", lock_locked)});
  attrs.push_back({"__enter__", runtime.make_native_function("_thread.LockType.__enter__", lock_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("_thread.LockType.__exit__", lock_exit)});
  return Value::class_object("LockType", std::move(attrs));
}

Value xlang_thread_make_rlock_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("_thread.RLock.__init__", rlock_init)});
  attrs.push_back({"acquire", runtime.make_native_function("_thread.RLock.acquire", rlock_acquire, nullptr, nullptr, nullptr, false, rlock_acquire_kw)});
  attrs.push_back({"release", runtime.make_native_function("_thread.RLock.release", rlock_release)});
  attrs.push_back({"__enter__", runtime.make_native_function("_thread.RLock.__enter__", rlock_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("_thread.RLock.__exit__", rlock_exit)});
  return Value::class_object("RLock", std::move(attrs));
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

Value xlang_thread_make_rlock_instance(Runtime& runtime) {
  Value lock_class = xlang_thread_make_rlock_class(runtime);
  Value instance = Value::instance(lock_class);
  std::string error;
  auto* state = new XlangRLockState();
  if (!instance_set_native_data(instance, kRLockNativeType, state, xlang_rlock_state_cleanup, error)) {
    delete state;
    return Value::invalid();
  }
  return instance;
}

} // namespace xlang3
