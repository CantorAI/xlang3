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
  if (value_as_function(args[0]) == nullptr && value_as_native_function(args[0]) == nullptr) {
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

} // namespace

Value register_low_level_thread_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_thread");
  builder.function("start_new_thread", thread_start_new_thread)
      .function("start_new", thread_start_new_thread)
      .function("allocate_lock", thread_allocate_lock)
      .function("get_ident", thread_get_ident)
      .function("exit", thread_exit)
      .value("LockType", xlang_thread_make_lock_class(runtime));
  auto module = builder.finish();
  runtime.register_module("_thread", module);
  return module;
}

} // namespace xlang3
