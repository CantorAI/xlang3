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
#include "xlang3/sequence.h"

namespace xlang3 {

namespace {

bool thread_start_new_thread(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc < 1 || argc > 2) {
    error = "_thread.start_new_thread() expected function and optional args";
    return false;
  }
  if (value_as_function(args[0]) == nullptr && value_as_native_function(args[0]) == nullptr &&
      value_as_bound_method(args[0]) == nullptr) {
    error = "_thread.start_new_thread() first argument must be callable";
    return false;
  }
  std::vector<Value> thread_args;
  if (argc == 2 && !xlang_thread_tuple_to_args(args[1], thread_args, error)) {
    return false;
  }
  int64_t ident = 0;
  if (!xlang_thread_start_detached(runtime, args[0], std::move(thread_args), ident, error)) {
    return false;
  }
  value_set_int64(out, ident);
  return true;
}

bool is_thread_callable(const Value& value) {
  return value_as_function(value) != nullptr ||
      value_as_native_function(value) != nullptr ||
      value_as_bound_method(value) != nullptr;
}

const Value* find_keyword(const NativeKeywordArg* kwargs, uint32_t kwargc, const char* name) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && kwargs[i].value != nullptr && std::string(kwargs[i].name) == name) {
      return kwargs[i].value;
    }
  }
  return nullptr;
}

bool thread_start_joinable_thread(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc < 1 || argc > 2) {
    error = "_thread.start_joinable_thread() expected function and optional args";
    return false;
  }
  if (!is_thread_callable(args[0])) {
    error = "_thread.start_joinable_thread() first argument must be callable";
    return false;
  }

  const Value* handle = find_keyword(kwargs, kwargc, "handle");
  if (handle == nullptr) {
    error = "_thread.start_joinable_thread() missing required keyword argument 'handle'";
    return false;
  }

  std::vector<Value> thread_args;
  if (argc == 2 && !xlang_thread_tuple_to_args(args[1], thread_args, error)) {
    return false;
  }

  auto state = std::make_shared<XlangThreadState>();
  state->runtime = &runtime;
  value_assign_fast(state->target, args[0]);
  state->args = std::move(thread_args);
  if (const Value* daemon = find_keyword(kwargs, kwargc, "daemon")) {
    state->daemon = value_truthy(*daemon);
  }

  Value mutable_handle = *handle;
  if (!xlang_thread_handle_set_thread(mutable_handle, state, error)) {
    return false;
  }
  if (!xlang_thread_start_state(std::move(state), error)) {
    return false;
  }

  int64_t ident = 0;
  bool has_ident = false;
  if (!xlang_thread_handle_ident(*handle, ident, has_ident, error)) {
    return false;
  }
  if (has_ident) {
    value_set_int64(out, ident);
  } else {
    value_set_none(out);
  }
  return true;
}

bool thread_allocate_lock(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)args;
  (void)user_data;
  if (argc != 0) {
    error = "_thread.allocate_lock() expected no arguments";
    return false;
  }
  out = xlang_thread_make_lock_instance(runtime);
  if (out.tag == ValueTag::Invalid) {
    error = "failed to allocate lock";
    return false;
  }
  return true;
}

bool thread_get_ident(
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
    error = "_thread.get_ident() expected no arguments";
    return false;
  }
  value_set_int64(out, xlang_thread_current_ident());
  return true;
}

bool thread_get_main_thread_ident(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  return thread_get_ident(runtime, args, argc, out, error, user_data);
}

bool thread_get_native_id(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  return thread_get_ident(runtime, args, argc, out, error, user_data);
}

bool thread_daemon_threads_allowed(
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
    error = "_thread.daemon_threads_allowed() expected no arguments";
    return false;
  }
  value_set_bool(out, true);
  return true;
}

bool thread_is_main_interpreter(
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
    error = "_thread._is_main_interpreter() expected no arguments";
    return false;
  }
  value_set_bool(out, true);
  return true;
}

bool thread_shutdown(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)args;
  (void)user_data;
  if (argc != 0) {
    error = "_thread._shutdown() expected no arguments";
    return false;
  }
  xlang_thread_join_runtime_threads(&runtime);
  value_set_none(out);
  return true;
}

bool thread_make_thread_handle(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc > 1) {
    error = "_thread._make_thread_handle() expected optional ident";
    return false;
  }
  int64_t ident = 0;
  bool done = false;
  if (argc == 1 && args[0].tag != ValueTag::None) {
    if (!value_int_like_to_i64(args[0], ident)) {
      error = "thread ident must be an integer";
      return false;
    }
  }
  out = xlang_thread_make_handle_instance(runtime, ident, done);
  if (out.tag == ValueTag::Invalid) {
    error = "failed to allocate thread handle";
    return false;
  }
  return true;
}

bool thread_stack_size(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc > 1) {
    error = "_thread.stack_size() expected at most 1 argument";
    return false;
  }
  if (argc == 1 && args[0].tag != ValueTag::Int64) {
    error = "size must be an integer";
    return false;
  }
  value_set_int64(out, 0);
  return true;
}

bool thread_exit(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)args;
  (void)user_data;
  (void)out;
  if (argc != 0) {
    error = "_thread.exit() expected no arguments";
    return false;
  }
  error = "_thread.exit() is not implemented yet";
  return false;
}

bool thread_except_hook_args(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  (void)runtime;
  if (argc != 1) {
    error = "_thread._ExceptHookArgs() expected one sequence";
    return false;
  }

  std::vector<Value> items;
  if (auto* tuple = value_as_tuple(args[0])) {
    items = tuple->items;
  } else if (auto* list = value_as_list(args[0])) {
    items = list->items;
  } else {
    error = "_thread._ExceptHookArgs() argument must be a sequence";
    return false;
  }
  if (items.size() != 4) {
    error = "_thread._ExceptHookArgs() expected a sequence of four items";
    return false;
  }

  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_thread")});
  attrs.push_back({"__qualname__", Value::string("_ExceptHookArgs")});
  Value klass = Value::class_object("_ExceptHookArgs", std::move(attrs));
  out = Value::instance(std::move(klass));
  std::string ignored;
  object_set_attr(out, "exc_type", items[0], ignored);
  object_set_attr(out, "exc_value", items[1], ignored);
  object_set_attr(out, "exc_traceback", items[2], ignored);
  object_set_attr(out, "thread", items[3], ignored);
  object_set_attr(out, "_tuple", Value::tuple(std::move(items)), ignored);
  return true;
}

bool thread_excepthook(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  (void)runtime;
  if (argc != 1) {
    error = "_thread._excepthook() expected one argument";
    return false;
  }
  value_set_none(out);
  return true;
}

} // namespace

Value register_low_level_thread_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_thread");
  builder.function("start_new_thread", thread_start_new_thread)
      .function("start_new", thread_start_new_thread)
      .function("start_joinable_thread", nullptr, nullptr, false, thread_start_joinable_thread)
      .function("allocate_lock", thread_allocate_lock)
      .function("get_ident", thread_get_ident)
      .function("get_native_id", thread_get_native_id)
      .function("_get_main_thread_ident", thread_get_main_thread_ident)
      .function("daemon_threads_allowed", thread_daemon_threads_allowed)
      .function("_is_main_interpreter", thread_is_main_interpreter)
      .function("_shutdown", thread_shutdown)
      .function("_make_thread_handle", thread_make_thread_handle)
      .function("stack_size", thread_stack_size)
      .function("exit", thread_exit)
      .function("_excepthook", thread_excepthook)
      .function("_ExceptHookArgs", thread_except_hook_args)
      .value("error", runtime.find_builtin("RuntimeError") ? *runtime.find_builtin("RuntimeError") : Value::class_object("error", {}))
      .value("TIMEOUT_MAX", Value::number(4294967.0))
      .value("LockType", xlang_thread_make_lock_class(runtime))
      .value("RLock", xlang_thread_make_rlock_class(runtime))
      .value("_ThreadHandle", xlang_thread_make_handle_class(runtime))
      .value("_local", xlang_thread_make_local_class(runtime));
  auto module = builder.finish();
  runtime.register_module("_thread", module);
  return module;
}

void register_thread_modules(Runtime& runtime) {
  (void)register_low_level_thread_module(runtime);
}

} // namespace xlang3
