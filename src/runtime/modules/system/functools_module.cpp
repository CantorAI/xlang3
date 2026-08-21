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

namespace xlang3 {

namespace {

void value_cleanup(void* data) {
  delete static_cast<Value*>(data);
}

bool update_wrapper_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "update_wrapper() expected wrapper and wrapped";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool wraps_apply(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "wraps decorator expected one function";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool wraps_entry(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "wraps() expected wrapped function";
    return false;
  }
  auto* wrapped = new Value(args[0]);
  out = runtime.make_native_function("functools.wraps.<decorator>", wraps_apply, wrapped, value_cleanup);
  return true;
}

bool wraps_entry_positional(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return wraps_entry(runtime, args, argc, nullptr, 0, out, error, nullptr);
}

bool identity_decorator_factory(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 1 && (value_as_function(args[0]) != nullptr || value_as_native_function(args[0]) != nullptr)) {
    value_assign_fast(out, args[0]);
    return true;
  }
  if (argc > 1) {
    error = "decorator expected optional function";
    return false;
  }
  out = runtime.make_native_function("functools.identity_decorator.<decorator>", wraps_apply);
  return true;
}

} // namespace

void register_functools_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "functools");
  builder.function("update_wrapper", update_wrapper_entry)
      .value(
          "wraps",
          runtime.make_native_function(
              "functools.wraps",
              wraps_entry_positional,
              nullptr,
              nullptr,
              nullptr,
              false,
              wraps_entry))
      .function("lru_cache", identity_decorator_factory)
      .function("cache", identity_decorator_factory);
  runtime.register_module("functools", builder.finish());
}

} // namespace xlang3
