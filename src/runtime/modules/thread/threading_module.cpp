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

#include "xlang3/builtins.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace xlang3 {

Value register_low_level_thread_module(Runtime& runtime);

namespace {

constexpr const char* kThreadingEventNativeType = "threading.Event";
constexpr const char* kThreadingConditionNativeType = "threading.Condition";

struct ThreadingEventState {
  std::mutex mutex;
  std::condition_variable cv;
  bool set = false;
};

struct ThreadingConditionState {
  Value lock;
  std::mutex mutex;
  std::condition_variable cv;
};

void threading_event_cleanup(void* data) {
  delete static_cast<ThreadingEventState*>(data);
}

void threading_condition_cleanup(void* data) {
  delete static_cast<ThreadingConditionState*>(data);
}

bool threading_lock(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)args;
  (void)user_data;
  if (argc != 0) {
    error = "threading.Lock() expected no arguments";
    return false;
  }
  out = xlang_thread_make_lock_instance(runtime);
  if (out.tag == ValueTag::Invalid) {
    error = "failed to allocate lock";
    return false;
  }
  return true;
}

bool threading_rlock(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)args;
  (void)user_data;
  if (argc != 0) {
    error = "threading.RLock() expected no arguments";
    return false;
  }
  out = xlang_thread_make_rlock_instance(runtime);
  if (out.tag == ValueTag::Invalid) {
    error = "failed to allocate rlock";
    return false;
  }
  return true;
}

bool threading_get_ident(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)args;
  (void)user_data;
  if (argc != 0) {
    error = "threading.get_ident() expected no arguments";
    return false;
  }
  value_set_int64(out, xlang_thread_current_ident());
  return true;
}

bool threading_current_thread(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    error = "threading.current_thread() expected no arguments";
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("threading")});
  Value klass = Value::class_object("_MainThread", std::move(attrs));
  out = Value::instance(klass);
  object_set_attr(out, "name", Value::string("MainThread"), error);
  object_set_attr(out, "ident", Value::int64(xlang_thread_current_ident()), error);
  return true;
}

bool threading_active_count(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)args;
  (void)user_data;
  if (argc != 0) {
    error = "threading.active_count() expected no arguments";
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(xlang_thread_active_count()));
  return true;
}

bool threading_settrace(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "threading.settrace() expected one argument";
    return false;
  }
  runtime.set_thread_trace_function(args[0]);
  value_set_none(out);
  return true;
}

bool threading_gettrace(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    error = "threading.gettrace() expected no arguments";
    return false;
  }
  if (runtime.thread_trace_function().tag == ValueTag::Invalid) {
    value_set_none(out);
  } else {
    value_assign_fast(out, runtime.thread_trace_function());
  }
  return true;
}

bool threading_setprofile(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "threading.setprofile() expected one argument";
    return false;
  }
  runtime.set_thread_profile_function(args[0]);
  value_set_none(out);
  return true;
}

bool threading_getprofile(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    error = "threading.getprofile() expected no arguments";
    return false;
  }
  if (runtime.thread_profile_function().tag == ValueTag::Invalid) {
    value_set_none(out);
  } else {
    value_assign_fast(out, runtime.thread_profile_function());
  }
  return true;
}

bool threading_main_thread(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return threading_current_thread(runtime, args, argc, out, error, nullptr);
}

bool threading_enumerate(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value current;
  if (!threading_current_thread(runtime, args, argc, current, error, nullptr)) {
    return false;
  }
  out = Value::list({current});
  return true;
}

bool threading_event_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "threading.Event.__init__() expected no arguments";
    return false;
  }
  auto* state = new ThreadingEventState();
  if (!instance_set_native_data(args[0], kThreadingEventNativeType, state, threading_event_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

ThreadingEventState* threading_event_state(const Value& self, std::string& error) {
  auto* state = static_cast<ThreadingEventState*>(instance_get_native_data(self, kThreadingEventNativeType));
  if (state == nullptr) {
    error = "invalid threading.Event object";
  }
  return state;
}

bool threading_event_set(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "threading.Event.set() expected no arguments";
    return false;
  }
  auto* state = threading_event_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->set = true;
  }
  state->cv.notify_all();
  value_set_none(out);
  return true;
}

bool threading_event_clear(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "threading.Event.clear() expected no arguments";
    return false;
  }
  auto* state = threading_event_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->set = false;
  }
  value_set_none(out);
  return true;
}

bool threading_event_is_set(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "threading.Event.is_set() expected no arguments";
    return false;
  }
  auto* state = threading_event_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    value_set_bool(out, state->set);
  }
  return true;
}

bool threading_event_wait(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "threading.Event.wait() expected optional timeout";
    return false;
  }
  auto* state = threading_event_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::unique_lock<std::mutex> lock(state->mutex);
  if (state->set) {
    value_set_bool(out, true);
    return true;
  }
  if (argc == 2 && args[1].tag != ValueTag::None) {
    double seconds = 0.0;
    if (args[1].tag == ValueTag::Int64) {
      seconds = static_cast<double>(args[1].as.i64);
    } else if (args[1].tag == ValueTag::Double) {
      seconds = args[1].as.f64;
    } else {
      error = "threading.Event.wait() timeout must be a number or None";
      return false;
    }
    if (seconds <= 0.0) {
      value_set_bool(out, state->set);
      return true;
    }
    const auto timeout = std::chrono::duration<double>(seconds);
    const bool signaled = state->cv.wait_for(lock, timeout, [state]() { return state->set; });
    value_set_bool(out, signaled);
    return true;
  }
  state->cv.wait(lock, [state]() { return state->set; });
  value_set_bool(out, true);
  return true;
}

bool threading_condition_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "threading.Condition.__init__() expected optional lock";
    return false;
  }
  auto* state = new ThreadingConditionState();
  if (argc == 2) {
    value_assign_fast(state->lock, args[1]);
  } else {
    state->lock = xlang_thread_make_rlock_instance(runtime);
    if (state->lock.tag == ValueTag::Invalid) {
      delete state;
      error = "failed to allocate condition lock";
      return false;
    }
  }
  if (!instance_set_native_data(args[0], kThreadingConditionNativeType, state, threading_condition_cleanup, error)) {
    delete state;
    return false;
  }
  object_set_attr(const_cast<Value&>(args[0]), "_lock", state->lock, error);
  value_set_none(out);
  return true;
}

ThreadingConditionState* threading_condition_state(const Value& self, std::string& error) {
  auto* state = static_cast<ThreadingConditionState*>(instance_get_native_data(self, kThreadingConditionNativeType));
  if (state == nullptr) {
    error = "invalid threading.Condition object";
  }
  return state;
}

bool threading_condition_wait(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "threading.Condition.wait() expected optional timeout";
    return false;
  }
  auto* state = threading_condition_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (!xlang_lock_release_value(state->lock, error)) {
    return false;
  }
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (argc == 2 && args[1].tag != ValueTag::None) {
      double seconds = 0.0;
      if (args[1].tag == ValueTag::Int64) {
        seconds = static_cast<double>(args[1].as.i64);
      } else if (args[1].tag == ValueTag::Double) {
        seconds = args[1].as.f64;
      } else {
        xlang_lock_acquire_value(state->lock, true, error);
        error = "threading.Condition.wait() timeout must be a number or None";
        return false;
      }
      if (seconds > 0.0) {
        state->cv.wait_for(lock, std::chrono::duration<double>(seconds));
      }
    } else {
      state->cv.wait(lock);
    }
  }
  if (!xlang_lock_acquire_value(state->lock, true, error)) {
    return false;
  }
  value_set_bool(out, true);
  return true;
}

bool threading_condition_acquire(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "threading.Condition.acquire() expected optional blocking";
    return false;
  }
  auto* state = threading_condition_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  bool blocking = true;
  if (argc == 2) {
    blocking = value_truthy(args[1]);
  }
  if (!xlang_lock_acquire_value(state->lock, blocking, error)) {
    return false;
  }
  value_set_bool(out, true);
  return true;
}

bool threading_condition_release(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "threading.Condition.release() expected no arguments";
    return false;
  }
  auto* state = threading_condition_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (!xlang_lock_release_value(state->lock, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool threading_condition_enter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value acquired;
  if (!threading_condition_acquire(runtime, args, argc, acquired, error, nullptr)) {
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool threading_condition_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "threading.Condition.__exit__() expected exc_type, exc_val, exc_tb";
    return false;
  }
  if (!threading_condition_release(runtime, args, 1, out, error, nullptr)) {
    return false;
  }
  value_set_bool(out, false);
  return true;
}

bool threading_condition_notify(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "threading.Condition.notify() expected optional count";
    return false;
  }
  auto* state = threading_condition_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  int64_t count = 1;
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      error = "threading.Condition.notify() count must be an integer";
      return false;
    }
    count = args[1].as.i64;
  }
  for (int64_t i = 0; i < count; ++i) {
    state->cv.notify_one();
  }
  value_set_none(out);
  return true;
}

bool threading_condition_notify_all(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "threading.Condition.notify_all() expected no arguments";
    return false;
  }
  auto* state = threading_condition_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->cv.notify_all();
  value_set_none(out);
  return true;
}

Value make_event_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("threading.Event.__init__", threading_event_init)});
  attrs.push_back({"set", runtime.make_native_function("threading.Event.set", threading_event_set)});
  attrs.push_back({"clear", runtime.make_native_function("threading.Event.clear", threading_event_clear)});
  attrs.push_back({"is_set", runtime.make_native_function("threading.Event.is_set", threading_event_is_set)});
  attrs.push_back({"isSet", runtime.make_native_function("threading.Event.isSet", threading_event_is_set)});
  attrs.push_back({"wait", runtime.make_native_function("threading.Event.wait", threading_event_wait)});
  return Value::class_object("Event", std::move(attrs));
}

Value make_condition_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("threading.Condition.__init__", threading_condition_init)});
  attrs.push_back({"acquire", runtime.make_native_function("threading.Condition.acquire", threading_condition_acquire)});
  attrs.push_back({"release", runtime.make_native_function("threading.Condition.release", threading_condition_release)});
  attrs.push_back({"__enter__", runtime.make_native_function("threading.Condition.__enter__", threading_condition_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("threading.Condition.__exit__", threading_condition_exit)});
  attrs.push_back({"wait", runtime.make_native_function("threading.Condition.wait", threading_condition_wait)});
  attrs.push_back({"notify", runtime.make_native_function("threading.Condition.notify", threading_condition_notify)});
  attrs.push_back({"notify_all", runtime.make_native_function("threading.Condition.notify_all", threading_condition_notify_all)});
  attrs.push_back({"notifyAll", runtime.make_native_function("threading.Condition.notifyAll", threading_condition_notify_all)});
  return Value::class_object("Condition", std::move(attrs));
}

Value make_local_class() {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("threading")});
  return Value::class_object("local", std::move(attrs));
}

} // namespace

void register_thread_modules(Runtime& runtime) {
  (void)register_low_level_thread_module(runtime);

  NativeModuleBuilder builder(runtime, "threading");
  Value lock_fn = runtime.make_native_function("threading.Lock", threading_lock);
  Value rlock_fn = runtime.make_native_function("threading.RLock", threading_rlock);
  builder.value("Thread", xlang_thread_make_thread_class(runtime))
      .value("Lock", lock_fn)
      .value("RLock", rlock_fn)
      .value("Event", make_event_class(runtime))
      .value("Condition", make_condition_class(runtime))
      .value("local", make_local_class())
      .function("get_ident", threading_get_ident)
      .function("current_thread", threading_current_thread)
      .function("main_thread", threading_main_thread)
      .function("enumerate", threading_enumerate)
      .function("active_count", threading_active_count)
      .function("settrace", threading_settrace)
      .function("gettrace", threading_gettrace)
      .function("setprofile", threading_setprofile)
      .function("getprofile", threading_getprofile);
  runtime.register_module("threading", builder.finish());
}

} // namespace xlang3
