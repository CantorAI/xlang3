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

namespace xlang3 {

Value register_low_level_thread_module(Runtime& runtime);

namespace {

constexpr const char* kThreadingEventNativeType = "threading.Event";

struct ThreadingEventState {
  bool set = false;
};

void threading_event_cleanup(void* data) {
  delete static_cast<ThreadingEventState*>(data);
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
  value_set_int64(out, 1);
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

bool threading_setprofile(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "threading.setprofile() expected one argument";
    return false;
  }
  value_set_none(out);
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
  state->set = true;
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
  state->set = false;
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
  value_set_bool(out, state->set);
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
  value_set_bool(out, state->set);
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

} // namespace

void register_thread_modules(Runtime& runtime) {
  (void)register_low_level_thread_module(runtime);

  NativeModuleBuilder builder(runtime, "threading");
  Value lock_fn = runtime.make_native_function("threading.Lock", threading_lock);
  builder.value("Thread", xlang_thread_make_thread_class(runtime))
      .value("Lock", lock_fn)
      .value("RLock", lock_fn)
      .value("Event", make_event_class(runtime))
      .function("get_ident", threading_get_ident)
      .function("current_thread", threading_current_thread)
      .function("active_count", threading_active_count)
      .function("settrace", threading_settrace)
      .function("gettrace", threading_gettrace)
      .function("setprofile", threading_setprofile);
  runtime.register_module("threading", builder.finish());
}

} // namespace xlang3
