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

#include "xlang3/functional_iterators.h"
#include "xlang3/interpreter.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "runtime_lock.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <unordered_map>

namespace xlang3 {

namespace {

constexpr const char* kLockNativeType = "_thread.LockType";
constexpr const char* kRLockNativeType = "_thread.RLock";
constexpr const char* kThreadHandleNativeType = "_thread._ThreadHandle";
constexpr const char* kThreadLocalNativeType = "_thread._local";

std::mutex g_thread_registry_mutex;
std::vector<std::shared_ptr<XlangThreadState>> g_thread_registry;

struct XlangThreadHandleState {
  std::shared_ptr<XlangThreadState> thread;
  std::mutex mutex;
  std::condition_variable done_cv;
  int64_t ident = 0;
  bool done = false;
};

struct XlangThreadLocalState {
  std::mutex mutex;
  Runtime* runtime = nullptr;
  Value owner_class;
  std::vector<Value> init_args;
  std::vector<std::pair<std::string, Value>> init_kwargs;
  std::unordered_map<int64_t, std::shared_ptr<Value>> attrs_by_thread;
  std::unordered_set<int64_t> initialized_threads;
  std::unordered_set<int64_t> initializing_threads;
};

XlangLockState* lock_state_from_self(const Value& self, std::string& error) {
  auto* state = static_cast<XlangLockState*>(instance_get_native_data(self, kLockNativeType));
  if (state == nullptr) {
    error = "invalid lock object";
  }
  return state;
}

XlangThreadHandleState* thread_handle_state_from_self(const Value& self, std::string& error) {
  auto* state = static_cast<XlangThreadHandleState*>(instance_get_native_data(self, kThreadHandleNativeType));
  if (state == nullptr) {
    error = "invalid thread handle";
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
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    bool& blocking,
    bool& has_timeout,
    double& timeout,
    std::string& error) {
  blocking = true;
  has_timeout = false;
  timeout = -1.0;
  if (argc < 1 || argc > 3) {
    error = "lock.acquire() expected optional blocking/timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  parse_blocking_arg(args, argc, blocking);
  const Value* timeout_value = argc >= 3 ? &args[2] : nullptr;
  has_timeout = timeout_value != nullptr;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      error = "lock.acquire() received invalid keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (name == "blocking") {
      if (argc >= 2) {
        error = "lock.acquire() got multiple values for argument 'blocking'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      blocking = value_truthy(*kwargs[i].value);
    } else if (name == "timeout") {
      if (timeout_value != nullptr) {
        error = "lock.acquire() got multiple values for argument 'timeout'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      timeout_value = kwargs[i].value;
      has_timeout = true;
    } else {
      error = "lock.acquire() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (timeout_value != nullptr) {
    if (timeout_value->tag == ValueTag::Int64) {
      timeout = static_cast<double>(timeout_value->as.i64);
    } else if (timeout_value->tag == ValueTag::Double) {
      timeout = timeout_value->as.f64;
    } else {
      error = "timeout value must be a number";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (!std::isfinite(timeout) || timeout > 4294967.0) {
      error = "timestamp out of range for platform time_t";
      runtime.raise_class_error("OverflowError", error);
      return false;
    }
    if (timeout < 0.0 && timeout != -1.0) {
      error = "timeout value must be positive";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (!blocking && timeout != -1.0) {
      error = "can't specify a timeout for a non-blocking call";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (timeout == -1.0) {
      has_timeout = false;
    }
  }
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

bool lock_repr(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "LockType.__repr__ expected no arguments";
    return false;
  }
  auto* state = lock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  bool locked = false;
  {
    std::lock_guard<std::mutex> guard(state->mutex);
    locked = state->locked;
  }
  std::ostringstream stream;
  stream << '<' << (locked ? "locked" : "unlocked")
         << " _thread.lock object at 0x" << std::hex
         << reinterpret_cast<uintptr_t>(state) << '>';
  out = Value::string(stream.str());
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
  bool blocking = true;
  bool has_timeout = false;
  double timeout = -1.0;
  if (!parse_lock_acquire_args(runtime, args, argc, nullptr, 0, blocking, has_timeout, timeout, error)) {
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
    if (has_timeout && !state->cv.wait_for(
                           lock,
                           std::chrono::duration<double>(timeout),
                           [state]() { return !state->locked; })) {
      value_set_bool(out, false);
      return true;
    }
    if (!has_timeout) {
      state->cv.wait(lock, [state]() { return !state->locked; });
    }
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
  bool has_timeout = false;
  double timeout = -1.0;
  if (!parse_lock_acquire_args(runtime, args, argc, kwargs, kwargc, blocking, has_timeout, timeout, error)) {
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
    if (has_timeout && !state->cv.wait_for(
                           lock,
                           std::chrono::duration<double>(timeout),
                           [state]() { return !state->locked; })) {
      value_set_bool(out, false);
      return true;
    }
    if (!has_timeout) {
      state->cv.wait(lock, [state]() { return !state->locked; });
    }
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
  (void)user_data;
  if (argc < 1) {
    error = "RLock.__init__ expected self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc > 1) {
    Value warnings;
    Value warn;
    if (!runtime.import_module("warnings", warnings, error) ||
        !module_get_attr(warnings, "warn", warn, error)) {
      return false;
    }
    const Value* category = runtime.find_builtin("DeprecationWarning");
    Value warning_args[] = {
        Value::string("Passing arguments to RLock() is deprecated"),
        category == nullptr ? Value::none() : *category,
    };
    Value ignored;
    if (!runtime_call_callable(runtime, warn, warning_args, 2, ignored, error)) {
      return false;
    }
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
  bool has_timeout = false;
  double timeout = -1.0;
  if (!parse_lock_acquire_args(runtime, args, argc, nullptr, 0, blocking, has_timeout, timeout, error)) {
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
    if (has_timeout && !state->cv.wait_for(
                           lock,
                           std::chrono::duration<double>(timeout),
                           [state]() { return state->depth == 0; })) {
      value_set_bool(out, false);
      return true;
    }
    if (!has_timeout) {
      state->cv.wait(lock, [state]() { return state->depth == 0; });
    }
    state->owner = current;
    state->owner_ident = xlang_thread_current_ident();
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
  bool has_timeout = false;
  double timeout = -1.0;
  if (!parse_lock_acquire_args(runtime, args, argc, kwargs, kwargc, blocking, has_timeout, timeout, error)) {
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
    if (has_timeout && !state->cv.wait_for(
                           lock,
                           std::chrono::duration<double>(timeout),
                           [state]() { return state->depth == 0; })) {
      value_set_bool(out, false);
      return true;
    }
    if (!has_timeout) {
      state->cv.wait(lock, [state]() { return state->depth == 0; });
    }
    state->owner = current;
    state->owner_ident = xlang_thread_current_ident();
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
      state->owner_ident = 0;
      notify = true;
    }
  }
  if (notify) {
    state->cv.notify_one();
  }
  value_set_none(out);
  return true;
}

bool rlock_locked(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "RLock.locked() expected no arguments";
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    value_set_bool(out, state->depth != 0);
  }
  return true;
}

bool rlock_is_owned(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "RLock._is_owned() expected no arguments";
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    value_set_bool(out, state->depth != 0 && state->owner == std::this_thread::get_id());
  }
  return true;
}

bool rlock_release_save(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "RLock._release_save() expected no arguments";
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  uint32_t depth = 0;
  int64_t owner_ident = 0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->depth == 0 || state->owner != std::this_thread::get_id()) {
      error = "cannot release un-acquired lock";
      return false;
    }
    depth = state->depth;
    owner_ident = state->owner_ident;
    state->depth = 0;
    state->owner = std::thread::id();
    state->owner_ident = 0;
  }
  state->cv.notify_one();
  out = Value::tuple({Value::int64(static_cast<int64_t>(depth)), Value::int64(owner_ident)});
  return true;
}

bool rlock_acquire_restore(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 2) {
    error = "RLock._acquire_restore() expected state";
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  auto* saved = value_as_tuple(args[1]);
  if (saved == nullptr || saved->items.empty() || saved->items[0].tag != ValueTag::Int64) {
    error = "RLock._acquire_restore() expected saved state";
    return false;
  }
  uint32_t depth = static_cast<uint32_t>(saved->items[0].as.i64 <= 0 ? 1 : saved->items[0].as.i64);
  int64_t owner_ident = xlang_thread_current_ident();
  if (saved->items.size() >= 2 && saved->items[1].tag == ValueTag::Int64) {
    owner_ident = saved->items[1].as.i64;
  }
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    state->cv.wait(lock, [state]() { return state->depth == 0; });
    state->owner = std::this_thread::get_id();
    state->owner_ident = owner_ident;
    state->depth = depth;
  }
  value_set_none(out);
  return true;
}

bool rlock_at_fork_reinit(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "RLock._at_fork_reinit() expected no arguments";
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->owner = std::thread::id();
    state->owner_ident = 0;
    state->depth = 0;
  }
  state->cv.notify_all();
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
    if (!result.errors.empty() && !state->runtime->finalizing()) {
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
  state->daemon = true;
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
  xlang_thread_join_state_for(state, 0.0, false);
}

void xlang_thread_join_state_for(XlangThreadState& state, double timeout_seconds, bool has_timeout) {
  std::thread worker;
  {
    std::unique_lock<std::mutex> lock(state.mutex);
    if (has_timeout) {
      if (timeout_seconds <= 0.0) {
        if (!state.done && state.started) {
          return;
        }
      } else {
        const auto timeout = std::chrono::duration<double>(timeout_seconds);
        if (!state.done_cv.wait_for(lock, timeout, [&state]() { return !state.started || state.done; })) {
          return;
        }
      }
    } else {
      state.done_cv.wait(lock, [&state]() { return !state.started || state.done; });
    }
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
  // A running thread may start another non-daemon thread while it is being
  // joined.  Repeat the snapshot until no non-daemon runtime thread remains.
  for (;;) {
    std::vector<std::shared_ptr<XlangThreadState>> threads;
    {
      std::lock_guard<std::mutex> registry_lock(g_thread_registry_mutex);
      for (const auto& state : g_thread_registry) {
        if (state && state->runtime == runtime && !state->daemon &&
            xlang_thread_is_alive_state(*state)) {
          threads.push_back(state);
        }
      }
    }
    if (threads.empty()) {
      break;
    }
    for (auto& state : threads) {
      xlang_thread_join_state(*state);
    }
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

void xlang_thread_detach_runtime_daemon_threads(Runtime* runtime) {
  std::lock_guard<std::mutex> registry_lock(g_thread_registry_mutex);
  auto it = g_thread_registry.begin();
  while (it != g_thread_registry.end()) {
    auto state = *it;
    if (!state || state->runtime != runtime || !state->daemon) {
      ++it;
      continue;
    }
    {
      std::lock_guard<std::mutex> state_lock(state->mutex);
      if (state->worker.joinable()) {
        state->worker.detach();
      }
    }
    it = g_thread_registry.erase(it);
  }
}

void xlang_lock_state_cleanup(void* data) {
  delete static_cast<XlangLockState*>(data);
}

void xlang_rlock_state_cleanup(void* data) {
  delete static_cast<XlangRLockState*>(data);
}

void xlang_thread_handle_state_cleanup(void* data) {
  delete static_cast<XlangThreadHandleState*>(data);
}

bool thread_handle_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "_ThreadHandle.__init__ expected no arguments";
    return false;
  }
  auto* state = new XlangThreadHandleState();
  if (!instance_set_native_data(args[0], kThreadHandleNativeType, state, xlang_thread_handle_state_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool thread_handle_ident_get(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "_ThreadHandle.ident expected no arguments";
    return false;
  }
  auto* state = thread_handle_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  int64_t ident = 0;
  bool has_ident = false;
  {
    if (state->thread) {
      std::lock_guard<std::mutex> lock(state->thread->mutex);
      ident = state->thread->ident;
      has_ident = ident != 0;
    } else {
      ident = state->ident;
      has_ident = ident != 0;
    }
  }
  if (!has_ident) {
    value_set_none(out);
  } else {
    value_set_int64(out, ident);
  }
  return true;
}

bool thread_handle_is_done(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "_ThreadHandle.is_done expected no arguments";
    return false;
  }
  auto* state = thread_handle_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  bool done = false;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    done = state->done;
  }
  if (state->thread) {
    std::lock_guard<std::mutex> lock(state->thread->mutex);
    done = state->thread->done;
  }
  value_set_bool(out, done);
  return true;
}

bool thread_handle_join(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc < 1 || argc > 2) {
    error = "_ThreadHandle.join expected optional timeout";
    return false;
  }
  auto* state = thread_handle_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  bool has_timeout = false;
  double timeout_seconds = 0.0;
  if (argc == 2 && args[1].tag != ValueTag::None) {
    has_timeout = true;
    if (args[1].tag == ValueTag::Int64) {
      timeout_seconds = static_cast<double>(args[1].as.i64);
    } else if (args[1].tag == ValueTag::Double) {
      timeout_seconds = args[1].as.f64;
    } else {
      error = "timeout value must be a number";
      return false;
    }
  }
  if (state->thread) {
    xlang_thread_join_state_for(*state->thread, timeout_seconds, has_timeout);
  } else {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (has_timeout) {
      if (timeout_seconds > 0.0) {
        state->done_cv.wait_for(
            lock, std::chrono::duration<double>(timeout_seconds),
            [&state]() { return state->done; });
      }
    } else {
      state->done_cv.wait(lock, [&state]() { return state->done; });
    }
  }
  value_set_none(out);
  return true;
}

bool thread_handle_set_done(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "_ThreadHandle._set_done expected no arguments";
    return false;
  }
  auto* state = thread_handle_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->thread) {
    {
      std::lock_guard<std::mutex> lock(state->thread->mutex);
      state->thread->done = true;
    }
    state->thread->done_cv.notify_all();
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->done = true;
  }
  state->done_cv.notify_all();
  value_set_none(out);
  return true;
}

XlangThreadLocalState* thread_local_state_from_self(const Value& self, std::string& error) {
  auto* state = static_cast<XlangThreadLocalState*>(instance_get_native_data(self, kThreadLocalNativeType));
  if (state == nullptr) {
    error = "invalid thread local object";
  }
  return state;
}

std::shared_ptr<Value> thread_local_attrs_for_current_thread(XlangThreadLocalState& state) {
  const int64_t ident = xlang_thread_current_ident();
  std::lock_guard<std::mutex> lock(state.mutex);
  auto it = state.attrs_by_thread.find(ident);
  if (it != state.attrs_by_thread.end()) {
    return it->second;
  }
  auto attrs = std::make_shared<Value>(Value::dict({}));
  auto inserted = state.attrs_by_thread.emplace(ident, std::move(attrs));
  return inserted.first->second;
}

bool thread_local_needs_init_for_current_thread(XlangThreadLocalState& state) {
  const int64_t ident = xlang_thread_current_ident();
  std::lock_guard<std::mutex> lock(state.mutex);
  if (state.initialized_threads.find(ident) != state.initialized_threads.end() ||
      state.initializing_threads.find(ident) != state.initializing_threads.end()) {
    return false;
  }
  state.initializing_threads.insert(ident);
  return true;
}

void thread_local_finish_init_for_current_thread(XlangThreadLocalState& state, bool initialized) {
  const int64_t ident = xlang_thread_current_ident();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.initializing_threads.erase(ident);
  if (initialized) {
    state.initialized_threads.insert(ident);
  }
}

bool thread_local_ensure_current_thread_initialized(const Value& self, XlangThreadLocalState& state, std::string& error) {
  thread_local_attrs_for_current_thread(state);
  if (state.runtime == nullptr || state.owner_class.tag == ValueTag::Invalid ||
      !thread_local_needs_init_for_current_thread(state)) {
    return true;
  }

  bool initialized = false;
  Value init;
  std::string init_error;
  if (object_lookup_class_attr(state.owner_class, "__init__", init, init_error) && init.tag != ValueTag::Invalid) {
    if (auto* native = value_as_native_function(init)) {
      if (native->name == "_thread._local.__init__") {
        initialized = true;
      }
    }
    if (!initialized) {
      std::vector<Value> args;
      args.reserve(state.init_args.size() + 1);
      args.push_back(self);
      for (const auto& arg : state.init_args) {
        args.push_back(arg);
      }
      Value ignored;
      if (!runtime_call_callable_kw(
              *state.runtime,
              init,
              args.data(),
              static_cast<uint32_t>(args.size()),
              state.init_kwargs,
              ignored,
              error)) {
        thread_local_finish_init_for_current_thread(state, false);
        return false;
      }
      initialized = true;
    }
  } else {
    initialized = true;
  }
  thread_local_finish_init_for_current_thread(state, initialized);
  return true;
}

void xlang_thread_local_state_cleanup(void* data) {
  delete static_cast<XlangThreadLocalState*>(data);
}

bool thread_local_get_attr(const Value& self, const std::string& name, Value& out, std::string& error);
bool thread_local_set_attr(Value& self, const std::string& name, const Value& value, std::string& error);
bool thread_local_delete_attr(Value& self, const std::string& name, std::string& error);

bool thread_local_attach_native_state(Value& self, std::string& error) {
  if (instance_get_native_data(self, kThreadLocalNativeType) != nullptr) {
    return true;
  }
  auto* state = new XlangThreadLocalState();
  if (!instance_set_native_data(self, kThreadLocalNativeType, state, xlang_thread_local_state_cleanup, error)) {
    delete state;
    return false;
  }
  if (!instance_set_native_attr_hooks(self, thread_local_get_attr, thread_local_set_attr, thread_local_delete_attr, error)) {
    return false;
  }
  return true;
}

bool thread_local_get_attr(const Value& self, const std::string& name, Value& out, std::string& error) {
  auto* state = thread_local_state_from_self(self, error);
  if (state == nullptr) {
    return false;
  }
  if (!thread_local_ensure_current_thread_initialized(self, *state, error)) {
    return false;
  }
  std::shared_ptr<Value> attrs = thread_local_attrs_for_current_thread(*state);
  if (name == "__dict__") {
    value_assign_fast(out, *attrs);
    return true;
  }
  return mapping_get_item(*attrs, Value::string(name), out, error);
}

bool thread_local_set_attr(Value& self, const std::string& name, const Value& value, std::string& error) {
  if (name == "__dict__" || name == "__class__") {
    error = "attribute '" + name + "' is read-only";
    return false;
  }
  auto* state = thread_local_state_from_self(self, error);
  if (state == nullptr) {
    return false;
  }
  if (!thread_local_ensure_current_thread_initialized(self, *state, error)) {
    return false;
  }
  std::shared_ptr<Value> attrs = thread_local_attrs_for_current_thread(*state);
  return mapping_set_item(*attrs, Value::string(name), value, error);
}

bool thread_local_delete_attr(Value& self, const std::string& name, std::string& error) {
  if (name == "__dict__" || name == "__class__") {
    error = "attribute '" + name + "' is read-only";
    return false;
  }
  auto* state = thread_local_state_from_self(self, error);
  if (state == nullptr) {
    return false;
  }
  if (!thread_local_ensure_current_thread_initialized(self, *state, error)) {
    return false;
  }
  std::shared_ptr<Value> attrs = thread_local_attrs_for_current_thread(*state);
  return mapping_delete_item(*attrs, Value::string(name), error);
}

bool thread_local_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "_local.__new__ expected a class";
    return false;
  }
  if (value_as_class(args[0]) == nullptr) {
    error = "_local.__new__ first argument must be a class";
    return false;
  }
  out = Value::instance(args[0]);
  if (!thread_local_attach_native_state(out, error)) {
    return false;
  }
  auto* state = thread_local_state_from_self(out, error);
  if (state != nullptr) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->runtime = &runtime;
    value_assign_fast(state->owner_class, args[0]);
    state->init_args.clear();
    state->init_args.reserve(argc > 0 ? argc - 1 : 0);
    for (uint32_t i = 1; i < argc; ++i) {
      state->init_args.push_back(args[i]);
    }
    state->initialized_threads.insert(xlang_thread_current_ident());
  }
  return state != nullptr;
}

bool thread_local_new_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!thread_local_new(runtime, args, argc, out, error, user_data)) {
    return false;
  }
  auto* state = thread_local_state_from_self(out, error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> lock(state->mutex);
  state->init_kwargs.clear();
  state->init_kwargs.reserve(kwargc);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "_local.__new__ received an invalid keyword argument";
      return false;
    }
    state->init_kwargs.emplace_back(kwargs[i].name, *kwargs[i].value);
  }
  return true;
}

bool rlock_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1) {
    error = "RLock.__init__ expected self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value self_only[] = {args[0]};
  if (!rlock_init(runtime, self_only, 1, out, error, user_data)) {
    return false;
  }
  if (argc > 1 || kwargc > 0) {
    Value warnings;
    Value warn;
    if (!runtime.import_module("warnings", warnings, error) ||
        !module_get_attr(warnings, "warn", warn, error)) {
      return false;
    }
    const Value* category = runtime.find_builtin("DeprecationWarning");
    Value warning_args[] = {
        Value::string("Passing arguments to RLock() is deprecated"),
        category == nullptr ? Value::none() : *category,
    };
    Value ignored;
    if (!runtime_call_callable(runtime, warn, warning_args, 2, ignored, error)) {
      return false;
    }
  }
  return true;
}

bool rlock_repr(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "RLock.__repr__ expected no arguments";
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  size_t depth = 0;
  int64_t owner = 0;
  {
    std::lock_guard<std::mutex> guard(state->mutex);
    depth = state->depth;
    owner = state->owner_ident;
  }
  std::ostringstream stream;
  stream << '<' << (depth == 0 ? "unlocked" : "locked")
         << " _thread.RLock object owner=" << owner
         << " count=" << depth << " at 0x" << std::hex
         << reinterpret_cast<uintptr_t>(state) << '>';
  out = Value::string(stream.str());
  return true;
}

bool rlock_recursion_count(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "RLock._recursion_count() expected no arguments";
    return false;
  }
  auto* state = rlock_state_from_self(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::lock_guard<std::mutex> guard(state->mutex);
  value_set_int64(
      out,
      state->depth != 0 && state->owner == std::this_thread::get_id()
          ? static_cast<int64_t>(state->depth)
          : 0);
  return true;
}

bool thread_local_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "_local.__init__ expected no arguments";
    return false;
  }
  Value self = args[0];
  if (!thread_local_attach_native_state(self, error)) return false;
  value_set_none(out);
  return true;
}

Value xlang_thread_make_lock_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_thread")});
  attrs.push_back({"__qualname__", Value::string("LockType")});
  attrs.push_back({"__init__", runtime.make_native_function("_thread.LockType.__init__", lock_init)});
  attrs.push_back({"__repr__", runtime.make_native_function("_thread.LockType.__repr__", lock_repr)});
  attrs.push_back({"acquire", runtime.make_native_function("_thread.LockType.acquire", lock_acquire, nullptr, nullptr, nullptr, false, lock_acquire_kw)});
  attrs.push_back({"release", runtime.make_native_function("_thread.LockType.release", lock_release)});
  attrs.push_back({"locked", runtime.make_native_function("_thread.LockType.locked", lock_locked)});
  attrs.push_back({"__enter__", runtime.make_native_function("_thread.LockType.__enter__", lock_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("_thread.LockType.__exit__", lock_exit)});
  return Value::class_object("LockType", std::move(attrs));
}

Value xlang_thread_make_rlock_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_thread")});
  attrs.push_back({"__qualname__", Value::string("RLock")});
  attrs.push_back({"__init__", runtime.make_native_function("_thread.RLock.__init__", rlock_init, nullptr, nullptr, nullptr, false, rlock_init_kw)});
  attrs.push_back({"__repr__", runtime.make_native_function("_thread.RLock.__repr__", rlock_repr)});
  attrs.push_back({"acquire", runtime.make_native_function("_thread.RLock.acquire", rlock_acquire, nullptr, nullptr, nullptr, false, rlock_acquire_kw)});
  attrs.push_back({"release", runtime.make_native_function("_thread.RLock.release", rlock_release)});
  attrs.push_back({"locked", runtime.make_native_function("_thread.RLock.locked", rlock_locked)});
  attrs.push_back({"_is_owned", runtime.make_native_function("_thread.RLock._is_owned", rlock_is_owned)});
  attrs.push_back({"_release_save", runtime.make_native_function("_thread.RLock._release_save", rlock_release_save)});
  attrs.push_back({"_acquire_restore", runtime.make_native_function("_thread.RLock._acquire_restore", rlock_acquire_restore)});
  attrs.push_back({"_recursion_count", runtime.make_native_function("_thread.RLock._recursion_count", rlock_recursion_count)});
  attrs.push_back({"_at_fork_reinit", runtime.make_native_function("_thread.RLock._at_fork_reinit", rlock_at_fork_reinit)});
  attrs.push_back({"__enter__", runtime.make_native_function("_thread.RLock.__enter__", rlock_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("_thread.RLock.__exit__", rlock_exit)});
  return Value::class_object("RLock", std::move(attrs));
}

Value xlang_thread_make_handle_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  Value ident_getter = runtime.make_native_function("_thread._ThreadHandle.ident", thread_handle_ident_get);
  attrs.push_back({"__init__", runtime.make_native_function("_thread._ThreadHandle.__init__", thread_handle_init)});
  attrs.push_back({"ident", Value::property(std::move(ident_getter), Value::none(), Value::none(), Value::none())});
  attrs.push_back({"is_done", runtime.make_native_function("_thread._ThreadHandle.is_done", thread_handle_is_done)});
  attrs.push_back({"join", runtime.make_native_function("_thread._ThreadHandle.join", thread_handle_join)});
  attrs.push_back({"_set_done", runtime.make_native_function("_thread._ThreadHandle._set_done", thread_handle_set_done)});
  return Value::class_object("_ThreadHandle", std::move(attrs));
}

Value xlang_thread_make_local_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__new__", runtime.make_native_function(
      "_thread._local.__new__",
      thread_local_new,
      nullptr,
      nullptr,
      nullptr,
      false,
      thread_local_new_kw)});
  attrs.push_back({"__init__", runtime.make_native_function("_thread._local.__init__", thread_local_init)});
  return Value::class_object("_local", std::move(attrs));
}

Value xlang_thread_make_lock_instance(Runtime& runtime) {
  Value lock_class;
  Value thread_module;
  std::string lookup_error;
  if (!runtime.import_module("_thread", thread_module, lookup_error) ||
      !module_get_attr(thread_module, "LockType", lock_class, lookup_error)) {
    lock_class = xlang_thread_make_lock_class(runtime);
  }
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
  Value lock_class;
  Value thread_module;
  std::string lookup_error;
  if (!runtime.import_module("_thread", thread_module, lookup_error) ||
      !module_get_attr(thread_module, "RLock", lock_class, lookup_error)) {
    lock_class = xlang_thread_make_rlock_class(runtime);
  }
  Value instance = Value::instance(lock_class);
  std::string error;
  auto* state = new XlangRLockState();
  if (!instance_set_native_data(instance, kRLockNativeType, state, xlang_rlock_state_cleanup, error)) {
    delete state;
    return Value::invalid();
  }
  return instance;
}

Value xlang_thread_make_handle_instance(Runtime& runtime, int64_t ident, bool done) {
  Value handle_class = xlang_thread_make_handle_class(runtime);
  Value instance = Value::instance(handle_class);
  std::string error;
  auto* state = new XlangThreadHandleState();
  state->ident = ident;
  state->done = done;
  if (!instance_set_native_data(instance, kThreadHandleNativeType, state, xlang_thread_handle_state_cleanup, error)) {
    delete state;
    return Value::invalid();
  }
  return instance;
}

bool xlang_thread_handle_set_thread(Value& handle, std::shared_ptr<XlangThreadState> thread, std::string& error) {
  auto* state = thread_handle_state_from_self(handle, error);
  if (state == nullptr) {
    return false;
  }
  state->thread = std::move(thread);
  state->done = false;
  return true;
}

bool xlang_thread_handle_ident(const Value& handle, int64_t& ident, bool& has_ident, std::string& error) {
  auto* state = thread_handle_state_from_self(handle, error);
  if (state == nullptr) {
    return false;
  }
  if (state->thread) {
    std::lock_guard<std::mutex> lock(state->thread->mutex);
    ident = state->thread->ident;
  } else {
    ident = state->ident;
  }
  has_ident = ident != 0;
  return true;
}

} // namespace xlang3
