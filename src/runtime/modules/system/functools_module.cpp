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

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

namespace xlang3 {

namespace {

struct PartialState {
  Value callable;
  std::vector<Value> bound_args;
};

struct WrapsState {
  Value wrapped;
};

void wraps_cleanup(void* data) {
  delete static_cast<WrapsState*>(data);
}

void partial_cleanup(void* data) {
  delete static_cast<PartialState*>(data);
}

bool partial_call(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<PartialState*>(user_data);
  if (state == nullptr) {
    error = "invalid functools.partial object";
    return false;
  }
  std::vector<Value> call_args;
  call_args.reserve(state->bound_args.size() + argc);
  for (const auto& arg : state->bound_args) {
    call_args.push_back(arg);
  }
  for (uint32_t i = 0; i < argc; ++i) {
    call_args.push_back(args[i]);
  }
  return runtime_call_callable(
      runtime,
      state->callable,
      call_args.empty() ? nullptr : call_args.data(),
      static_cast<uint32_t>(call_args.size()),
      out,
      error);
}

bool partial_call_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void* user_data) {
  return partial_call(runtime, args, argc, out, error, user_data);
}

bool partial_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "functools.partial() expected callable";
    return false;
  }
  auto* state = new PartialState();
  state->callable = args[0];
  state->bound_args.reserve(argc - 1);
  for (uint32_t i = 1; i < argc; ++i) {
    state->bound_args.push_back(args[i]);
  }
  out = runtime.make_native_function(
      "functools.partial.<call>",
      partial_call,
      state,
      partial_cleanup,
      nullptr,
      false,
      partial_call_kw);
  return true;
}

bool copy_attr_if_present(Value& wrapper, const Value& wrapped, const char* name, std::string& error) {
  Value attr;
  std::string ignored;
  if (!object_get_attr(wrapped, name, attr, ignored)) {
    return true;
  }
  return object_set_attr(wrapper, name, attr, error);
}

bool update_wrapper_common(Value wrapper, const Value& wrapped, Value& out, std::string& error) {
  if (!copy_attr_if_present(wrapper, wrapped, "__module__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__name__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__qualname__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__doc__", error) ||
      !copy_attr_if_present(wrapper, wrapped, "__annotations__", error)) {
    return false;
  }
  if (!object_set_attr(wrapper, "__wrapped__", wrapped, error)) {
    return false;
  }
  value_assign_fast(out, wrapper);
  return true;
}

bool update_wrapper_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "update_wrapper() expected wrapper and wrapped";
    return false;
  }
  return update_wrapper_common(args[0], args[1], out, error);
}

bool wraps_apply(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "wraps decorator expected one function";
    return false;
  }
  auto* state = static_cast<WrapsState*>(user_data);
  if (state == nullptr) {
    error = "invalid functools.wraps decorator";
    return false;
  }
  return update_wrapper_common(args[0], state->wrapped, out, error);
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
  auto* state = new WrapsState();
  state->wrapped = args[0];
  out = runtime.make_native_function("functools.wraps.<decorator>", wraps_apply, state, wraps_cleanup);
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
      .function("cache", identity_decorator_factory)
      .function("partial", partial_entry);
  runtime.register_module("functools", builder.finish());
}

} // namespace xlang3
