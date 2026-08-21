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

namespace xlang3 {

Value register_low_level_thread_module(Runtime& runtime);

namespace {

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

} // namespace

void register_thread_modules(Runtime& runtime) {
  (void)register_low_level_thread_module(runtime);

  NativeModuleBuilder builder(runtime, "threading");
  builder.value("Thread", xlang_thread_make_thread_class(runtime))
      .function("Lock", threading_lock)
      .function("get_ident", threading_get_ident)
      .function("active_count", threading_active_count)
      .function("settrace", threading_settrace)
      .function("gettrace", threading_gettrace);
  runtime.register_module("threading", builder.finish());
}

} // namespace xlang3
