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
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

#include "../thread/runtime_lock.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace xlang3 {

namespace {

constexpr const char* kSemLockNativeType = "_multiprocessing.SemLock";

struct SharedSemLockState {
  std::mutex mutex;
  std::condition_variable condition;
  int64_t value = 0;
  int64_t max_value = 1;
  int64_t kind = 1;
  std::thread::id owner;
  uint32_t recursion_count = 0;
};

struct SemLockState {
  std::shared_ptr<SharedSemLockState> shared;
  int64_t handle = 0;
  // CPython keeps ownership and acquisition counts per wrapper even though
  // the Windows kernel semaphore is shared across processes.
  std::thread::id last_thread;
  int64_t acquisition_count = 0;
};

#if !defined(_WIN32)
std::atomic<int64_t> g_next_semlock_handle{1};
std::mutex g_semlock_registry_mutex;
std::unordered_map<int64_t, std::weak_ptr<SharedSemLockState>> g_semlock_registry;
#endif

void semlock_cleanup(void* data) {
  auto* state = static_cast<SemLockState*>(data);
#if defined(_WIN32)
  // `_multiprocessing.SemLock.handle` crosses the stdlib's Windows spawn
  // boundary. It must be a kernel handle, not a process-local registry key.
  if (state->handle != 0) {
    (void)::CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(state->handle)));
  }
#endif
  delete state;
}

#if defined(_WIN32)
bool semlock_windows_value(SemLockState* state, int64_t& value, DWORD& error_code) {
  const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<intptr_t>(state->handle));
  LONG previous = 0;
  switch (::WaitForSingleObjectEx(handle, 0, FALSE)) {
    case WAIT_OBJECT_0:
      if (!::ReleaseSemaphore(handle, 1, &previous)) {
        error_code = ::GetLastError();
        return false;
      }
      value = static_cast<int64_t>(previous) + 1;
      return true;
    case WAIT_TIMEOUT:
      value = 0;
      return true;
    default:
      error_code = ::GetLastError();
      return false;
  }
}
#endif

SemLockState* semlock_state(const Value& self, std::string& error) {
  auto* state = static_cast<SemLockState*>(instance_get_native_data(self, kSemLockNativeType));
  if (state == nullptr || !state->shared) {
    error = "invalid SemLock object";
    return nullptr;
  }
  return state;
}

bool semlock_number(const Value& value, double& out) {
  if (value.tag == ValueTag::Int64) {
    out = static_cast<double>(value.as.i64);
    return true;
  }
  if (value.tag == ValueTag::Double) {
    out = value.as.f64;
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1.0 : 0.0;
    return true;
  }
  return false;
}

void semlock_publish_attrs(Value& self, const SemLockState& state, const Value& name) {
  std::string ignored;
  object_set_attr(self, "handle", Value::int64(state.handle), ignored);
  object_set_attr(self, "kind", Value::int64(state.shared->kind), ignored);
  object_set_attr(self, "maxvalue", Value::int64(state.shared->max_value), ignored);
  object_set_attr(self, "name", name, ignored);
}

bool semlock_attach(
    Value& self,
    std::shared_ptr<SharedSemLockState> shared,
    int64_t handle,
    const Value& name,
    std::string& error) {
  auto* state = new SemLockState{std::move(shared), handle};
  if (!instance_set_native_data(self, kSemLockNativeType, state, semlock_cleanup, error)) {
#if defined(_WIN32)
    if (handle != 0) {
      (void)::CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(handle)));
    }
#endif
    delete state;
    return false;
  }
  semlock_publish_attrs(self, *state, name);
  return true;
}

bool semlock_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 6 || args[1].tag != ValueTag::Int64 || args[2].tag != ValueTag::Int64 ||
      args[3].tag != ValueTag::Int64) {
    error = "SemLock() expected kind, value, maxvalue, name, unlink";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int64_t kind = args[1].as.i64;
  const int64_t value = args[2].as.i64;
  const int64_t max_value = args[3].as.i64;
  if ((kind != 0 && kind != 1) || value < 0 || max_value <= 0 || value > max_value
#if defined(_WIN32)
      || max_value > std::numeric_limits<LONG>::max()
#endif
  ) {
    error = "invalid SemLock parameters";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  auto shared = std::make_shared<SharedSemLockState>();
  shared->kind = kind;
  shared->value = value;
  shared->max_value = max_value;
#if defined(_WIN32)
  // CPython uses unnamed Windows semaphores; the handle itself is duplicated
  // into spawned children by multiprocessing.reduction. The Python-level name
  // remains metadata and is not an operating-system object name on Windows.
  HANDLE native_handle = ::CreateSemaphoreA(
      nullptr,
      static_cast<LONG>(value),
      static_cast<LONG>(max_value),
      nullptr);
  if (native_handle == nullptr) {
    const DWORD code = ::GetLastError();
    error = "CreateSemaphore failed with Win32 error " + std::to_string(code);
    runtime.raise_class_error("OSError", error);
    return false;
  }
  const int64_t handle = static_cast<int64_t>(reinterpret_cast<intptr_t>(native_handle));
#else
  const int64_t handle = g_next_semlock_handle.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(g_semlock_registry_mutex);
    g_semlock_registry[handle] = shared;
  }
#endif
  Value self = args[0];
  const Value name = value_truthy(args[5]) ? Value::none() : args[4];
  if (!semlock_attach(self, std::move(shared), handle, name, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool semlock_acquire(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "SemLock.acquire() expected optional block and timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = semlock_state(args[0], error);
  if (state == nullptr) return false;
  const bool blocking = argc < 2 || value_truthy(args[1]);
  bool has_timeout = argc == 3 && args[2].tag != ValueTag::None;
  double timeout = 0.0;
  if (has_timeout && !semlock_number(args[2], timeout)) {
    error = "timeout must be a number";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  if (has_timeout && !std::isfinite(timeout)) {
    error = "timeout is too large";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  if (has_timeout && timeout < 0.0) timeout = 0.0;
  if (has_timeout && timeout * 1000.0 >= 0.5 * static_cast<double>(INFINITE)) {
    error = "timeout is too large";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
#else
  if (has_timeout && timeout < 0.0) has_timeout = false;
#endif

  auto shared = state->shared;
#if defined(_WIN32)
  const auto current = std::this_thread::get_id();
  {
    std::lock_guard<std::mutex> lock(shared->mutex);
    if (shared->kind == 0 && state->acquisition_count > 0 && state->last_thread == current) {
      ++state->acquisition_count;
      value_set_bool(out, true);
      return true;
    }
  }
  DWORD wait_ms = INFINITE;
  if (!blocking) {
    wait_ms = 0;
  } else if (has_timeout) {
    wait_ms = static_cast<DWORD>(timeout * 1000.0 + 0.5);
  }
  const HANDLE handle = reinterpret_cast<HANDLE>(static_cast<intptr_t>(state->handle));
  // Most locks are uncontended. Match CPython by trying the zero-time kernel
  // wait before releasing the runtime lock; only the contended path suspends.
  DWORD wait_result = ::WaitForSingleObjectEx(handle, 0, FALSE);
  if (wait_result == WAIT_OBJECT_0) {
    std::lock_guard<std::mutex> lock(shared->mutex);
    state->last_thread = current;
    ++state->acquisition_count;
    value_set_bool(out, true);
    return true;
  }
  if (wait_result == WAIT_FAILED) {
    const DWORD code = ::GetLastError();
    error = "WaitForSingleObject(SemLock) failed with Win32 error " + std::to_string(code);
    runtime.raise_class_error("OSError", error);
    return false;
  }
  if (!blocking) {
    value_set_bool(out, false);
    return true;
  }
  {
    // Another Python thread must be able to run and release a held semaphore.
    XlangRuntimeExecutionSuspension suspension;
    wait_result = ::WaitForSingleObjectEx(handle, wait_ms, FALSE);
  }
  if (wait_result == WAIT_TIMEOUT) {
    value_set_bool(out, false);
    return true;
  }
  if (wait_result != WAIT_OBJECT_0) {
    const DWORD code = wait_result == WAIT_FAILED ? ::GetLastError() : ERROR_INVALID_HANDLE;
    error = "WaitForSingleObject(SemLock) failed with Win32 error " + std::to_string(code);
    runtime.raise_class_error("OSError", error);
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(shared->mutex);
    state->last_thread = current;
    ++state->acquisition_count;
  }
  value_set_bool(out, true);
  return true;
#else
  std::unique_lock<std::mutex> lock(shared->mutex);
  const auto current = std::this_thread::get_id();
  if (shared->kind == 0 && shared->owner == current && shared->recursion_count != 0) {
    ++shared->recursion_count;
    value_set_bool(out, true);
    return true;
  }
  auto available = [&]() { return shared->value > 0; };
  if (!available()) {
    if (!blocking) {
      value_set_bool(out, false);
      return true;
    }
    XlangRuntimeExecutionSuspension suspension;
    if (has_timeout) {
      if (!shared->condition.wait_for(lock, std::chrono::duration<double>(timeout), available)) {
        value_set_bool(out, false);
        return true;
      }
    } else {
      shared->condition.wait(lock, available);
    }
  }
  --shared->value;
  if (shared->kind == 0) {
    shared->owner = current;
    shared->recursion_count = 1;
  }
  value_set_bool(out, true);
  return true;
#endif
}

bool semlock_release(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SemLock.release() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = semlock_state(args[0], error);
  if (state == nullptr) return false;
  auto shared = state->shared;
#if defined(_WIN32)
  std::lock_guard<std::mutex> lock(shared->mutex);
  if (shared->kind == 0) {
    if (state->last_thread != std::this_thread::get_id() || state->acquisition_count == 0) {
      error = "attempt to release recursive lock not owned by thread";
      runtime.raise_class_error("AssertionError", error);
      return false;
    }
    if (state->acquisition_count > 1) {
      --state->acquisition_count;
      value_set_none(out);
      return true;
    }
  }
  if (!::ReleaseSemaphore(
          reinterpret_cast<HANDLE>(static_cast<intptr_t>(state->handle)), 1, nullptr)) {
    const DWORD code = ::GetLastError();
    if (code == ERROR_TOO_MANY_POSTS) {
      error = "semaphore or lock released too many times";
      runtime.raise_class_error("ValueError", error);
    } else {
      error = "ReleaseSemaphore failed with Win32 error " + std::to_string(code);
      runtime.raise_class_error("OSError", error);
    }
    return false;
  }
  --state->acquisition_count;
  value_set_none(out);
  return true;
#else
  {
    std::lock_guard<std::mutex> lock(shared->mutex);
    if (shared->kind == 0) {
      if (shared->owner != std::this_thread::get_id() || shared->recursion_count == 0) {
        error = "attempt to release recursive lock not owned by thread";
        runtime.raise_class_error("AssertionError", error);
        return false;
      }
      if (--shared->recursion_count != 0) {
        value_set_none(out);
        return true;
      }
      shared->owner = std::thread::id();
    }
    if (shared->value >= shared->max_value) {
      error = "semaphore or lock released too many times";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    ++shared->value;
  }
  shared->condition.notify_one();
  value_set_none(out);
  return true;
#endif
}

bool semlock_enter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  return semlock_acquire(runtime, args, argc, out, error, data);
}

bool semlock_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "SemLock.__exit__ expected exception details";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return semlock_release(runtime, args, 1, out, error, nullptr);
}

bool semlock_is_zero(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "SemLock._is_zero() expected no arguments"; return false; }
  auto* state = semlock_state(args[0], error);
  if (state == nullptr) return false;
#if defined(_WIN32)
  int64_t value = 0;
  DWORD code = ERROR_SUCCESS;
  if (!semlock_windows_value(state, value, code)) {
    error = "WaitForSingleObject(SemLock) failed with Win32 error " + std::to_string(code);
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_bool(out, value == 0);
#else
  std::lock_guard<std::mutex> lock(state->shared->mutex);
  value_set_bool(out, state->shared->value == 0);
#endif
  return true;
}

bool semlock_is_mine(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "SemLock._is_mine() expected no arguments"; return false; }
  auto* state = semlock_state(args[0], error);
  if (state == nullptr) return false;
#if defined(_WIN32)
  std::lock_guard<std::mutex> lock(state->shared->mutex);
  value_set_bool(out, state->acquisition_count > 0 && state->last_thread == std::this_thread::get_id());
#else
  std::lock_guard<std::mutex> lock(state->shared->mutex);
  value_set_bool(out, state->shared->owner == std::this_thread::get_id() && state->shared->recursion_count != 0);
#endif
  return true;
}

bool semlock_count(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "SemLock._count() expected no arguments"; return false; }
  auto* state = semlock_state(args[0], error);
  if (state == nullptr) return false;
#if defined(_WIN32)
  std::lock_guard<std::mutex> lock(state->shared->mutex);
  value_set_int64(out, state->acquisition_count);
#else
  std::lock_guard<std::mutex> lock(state->shared->mutex);
  value_set_int64(out, state->shared->owner == std::this_thread::get_id() ? state->shared->recursion_count : 0);
#endif
  return true;
}

bool semlock_get_value(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "SemLock._get_value() expected no arguments"; return false; }
  auto* state = semlock_state(args[0], error);
  if (state == nullptr) return false;
#if defined(_WIN32)
  int64_t value = 0;
  DWORD code = ERROR_SUCCESS;
  if (!semlock_windows_value(state, value, code)) {
    error = "WaitForSingleObject(SemLock) failed with Win32 error " + std::to_string(code);
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_int64(out, value);
#else
  std::lock_guard<std::mutex> lock(state->shared->mutex);
  value_set_int64(out, state->shared->value);
#endif
  return true;
}

bool semlock_after_fork(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "SemLock._after_fork() expected no arguments"; return false; }
#if defined(_WIN32)
  if (auto* state = semlock_state(args[0], error); state != nullptr) {
    state->acquisition_count = 0;
    state->last_thread = std::thread::id();
  } else {
    return false;
  }
#else
  if (auto* state = semlock_state(args[0], error); state != nullptr) {
    std::lock_guard<std::mutex> lock(state->shared->mutex);
    state->shared->owner = std::thread::id();
    state->shared->recursion_count = 0;
  } else {
    return false;
  }
#endif
  value_set_none(out);
  return true;
}

bool semlock_rebuild(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 5 || value_as_class(args[0]) == nullptr || args[1].tag != ValueTag::Int64 ||
      args[2].tag != ValueTag::Int64 || args[3].tag != ValueTag::Int64) {
    error = "SemLock._rebuild() expected handle, kind, maxvalue, name";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int64_t handle = args[1].as.i64;
  std::shared_ptr<SharedSemLockState> shared;
#if defined(_WIN32)
  shared = std::make_shared<SharedSemLockState>();
  shared->kind = args[2].as.i64;
  shared->max_value = args[3].as.i64;
  shared->value = 0;
#else
  {
    std::lock_guard<std::mutex> lock(g_semlock_registry_mutex);
    auto it = g_semlock_registry.find(handle);
    if (it != g_semlock_registry.end()) shared = it->second.lock();
  }
  if (!shared) {
    shared = std::make_shared<SharedSemLockState>();
    shared->kind = args[2].as.i64;
    shared->max_value = args[3].as.i64;
    shared->value = shared->max_value;
    std::lock_guard<std::mutex> lock(g_semlock_registry_mutex);
    g_semlock_registry[handle] = shared;
  }
#endif
  out = Value::instance(args[0]);
  return semlock_attach(out, std::move(shared), handle, args[4], error);
}

bool multiprocessing_sem_unlink(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "sem_unlink() expected a name"; return false; }
  value_set_none(out);
  return true;
}

Value make_semlock_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"SEM_VALUE_MAX", Value::int64(std::numeric_limits<int>::max())});
  attrs.push_back({"__init__", runtime.make_native_function("_multiprocessing.SemLock.__init__", semlock_init)});
  attrs.push_back({"acquire", runtime.make_native_function("_multiprocessing.SemLock.acquire", semlock_acquire)});
  attrs.push_back({"release", runtime.make_native_function("_multiprocessing.SemLock.release", semlock_release)});
  attrs.push_back({"__enter__", runtime.make_native_function("_multiprocessing.SemLock.__enter__", semlock_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("_multiprocessing.SemLock.__exit__", semlock_exit)});
  attrs.push_back({"_is_zero", runtime.make_native_function("_multiprocessing.SemLock._is_zero", semlock_is_zero)});
  attrs.push_back({"_is_mine", runtime.make_native_function("_multiprocessing.SemLock._is_mine", semlock_is_mine)});
  attrs.push_back({"_count", runtime.make_native_function("_multiprocessing.SemLock._count", semlock_count)});
  attrs.push_back({"_get_value", runtime.make_native_function("_multiprocessing.SemLock._get_value", semlock_get_value)});
  attrs.push_back({"_after_fork", runtime.make_native_function("_multiprocessing.SemLock._after_fork", semlock_after_fork)});
  attrs.push_back({"_rebuild", Value::class_method(
      runtime.make_native_function("_multiprocessing.SemLock._rebuild", semlock_rebuild))});
  return Value::class_object("SemLock", std::move(attrs));
}

bool multiprocessing_socket_unavailable(
    Runtime& runtime,
    const Value*,
    uint32_t,
    Value&,
    std::string& error,
    void* user_data) {
  error = std::string("_multiprocessing.") + static_cast<const char*>(user_data) +
          " is unavailable in this runtime";
  runtime.raise_class_error("NotImplementedError", error);
  return false;
}

Value unavailable_function(Runtime& runtime, const char* name) {
  return runtime.make_native_function(
      std::string("_multiprocessing.") + name,
      multiprocessing_socket_unavailable,
      const_cast<char*>(name));
}

} // namespace

void register_multiprocessing_module(Runtime& runtime) {
  Value semlock_class = make_semlock_class(runtime);
  NativeModuleBuilder builder(runtime, "_multiprocessing");
  builder.value("__doc__", Value::none())
      .value("SemLock", semlock_class)
      .function("sem_unlink", multiprocessing_sem_unlink)
      .value("closesocket", unavailable_function(runtime, "closesocket"))
      .value("send", unavailable_function(runtime, "send"))
      .value("recv", unavailable_function(runtime, "recv"));
  runtime.register_module("_multiprocessing", builder.finish());
}

} // namespace xlang3
