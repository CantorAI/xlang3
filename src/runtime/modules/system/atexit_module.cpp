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

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <algorithm>

namespace xlang3 {

namespace {

bool is_callable_value(const Value& value) {
  return value_as_function(value) != nullptr ||
         value_as_native_function(value) != nullptr ||
         value_as_bound_method(value) != nullptr ||
         value_as_class(value) != nullptr ||
         [&]() {
           Value call_attr;
           std::string ignored;
           return value_as_instance(value) != nullptr && attribute_get(value, "__call__", call_attr, ignored);
         }();
}

bool call_exit_callable(
    Runtime& runtime,
    const Value& callable,
    const Value* args,
    uint32_t argc,
    const std::vector<std::pair<std::string, Value>>& kwargs,
    Value& out,
    std::string& error) {
  if (auto* native = value_as_native_function(callable)) {
    if (!kwargs.empty()) {
      if (native->keyword_callback == nullptr) {
        error = "native exit function does not accept keyword arguments";
        return false;
      }
      std::vector<NativeKeywordArg> native_kwargs;
      native_kwargs.reserve(kwargs.size());
      for (const auto& item : kwargs) {
        native_kwargs.push_back(NativeKeywordArg{item.first.c_str(), &item.second});
      }
      return native->keyword_callback(
          runtime,
          args,
          argc,
          native_kwargs.data(),
          static_cast<uint32_t>(native_kwargs.size()),
          out,
          error,
          native->user_data);
    }
    if (native->callback == nullptr) {
      error = "native exit function is not callable";
      return false;
    }
    return native->callback(runtime, args, argc, out, error, native->user_data);
  }

  if (auto* function = value_as_function(callable)) {
    std::vector<Value> keyword_registers;
    std::vector<uint32_t> keyword_register_args;
    std::vector<ir::CallKeywordArg> keyword_args;
    if (!kwargs.empty()) {
      keyword_registers.reserve(kwargs.size());
      keyword_args.reserve(kwargs.size());
      for (const auto& item : kwargs) {
        keyword_registers.push_back(item.second);
        keyword_args.push_back(ir::CallKeywordArg{item.first, static_cast<uint32_t>(keyword_registers.size() - 1)});
      }
    }
    CallArgsView call_args;
    call_args.leading = args;
    call_args.leading_count = argc;
    call_args.registers = keyword_registers.empty() ? nullptr : keyword_registers.data();
    call_args.register_args = &keyword_register_args;
    call_args.keyword_args = keyword_args.empty() ? nullptr : &keyword_args;
    Interpreter interpreter(runtime);
    RuntimeResult result = interpreter.run_function_value(function, call_args);
    if (!result.errors.empty()) {
      error = result.errors.front();
      return false;
    }
    value_assign_fast(out, result.value);
    return true;
  }

  if (auto* bound = value_as_bound_method(callable)) {
    std::vector<Value> bound_args;
    bound_args.reserve(static_cast<size_t>(argc) + 1);
    bound_args.push_back(bound->self);
    for (uint32_t i = 0; i < argc; ++i) {
      bound_args.push_back(args[i]);
    }
    return call_exit_callable(
        runtime,
        bound->function,
        bound_args.data(),
        static_cast<uint32_t>(bound_args.size()),
        kwargs,
        out,
        error);
  }

  if (value_as_instance(callable) != nullptr) {
    Value call_attr;
    std::string attr_error;
    if (attribute_get(callable, "__call__", call_attr, attr_error)) {
      return call_exit_callable(runtime, call_attr, args, argc, kwargs, out, error);
    }
  }

  if (value_as_class(callable) != nullptr && kwargs.empty()) {
    return runtime_call_callable(runtime, callable, args, argc, out, error);
  }

  if (value_as_class(callable) != nullptr) {
    error = "class exit function keyword arguments are not supported yet";
    return false;
  }

  error = "exit function is not callable";
  return false;
}

bool collect_atexit_kwargs(
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    std::vector<std::pair<std::string, Value>>& out,
    std::string& error) {
  out.reserve(kwargc);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "atexit.register() got invalid keyword argument";
      return false;
    }
    out.push_back({kwargs[i].name, *kwargs[i].value});
  }
  return true;
}

bool atexit_register(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "atexit.register() expected at least one argument";
    return false;
  }
  if (!is_callable_value(args[0])) {
    error = "atexit.register() first argument must be callable";
    return false;
  }
  std::vector<Value> callback_args;
  callback_args.reserve(argc - 1);
  for (uint32_t i = 1; i < argc; ++i) {
    callback_args.push_back(args[i]);
  }
  runtime.register_exit_function(args[0], std::move(callback_args));
  value_assign_fast(out, args[0]);
  return true;
}

bool atexit_register_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "atexit.register() expected at least one argument";
    return false;
  }
  if (!is_callable_value(args[0])) {
    error = "atexit.register() first argument must be callable";
    return false;
  }
  std::vector<Value> callback_args;
  callback_args.reserve(argc - 1);
  for (uint32_t i = 1; i < argc; ++i) {
    callback_args.push_back(args[i]);
  }
  std::vector<std::pair<std::string, Value>> callback_kwargs;
  if (!collect_atexit_kwargs(kwargs, kwargc, callback_kwargs, error)) {
    return false;
  }
  runtime.register_exit_function(args[0], std::move(callback_args), std::move(callback_kwargs));
  value_assign_fast(out, args[0]);
  return true;
}

bool atexit_unregister(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "atexit.unregister() expected one argument";
    return false;
  }
  runtime.unregister_exit_function(args[0]);
  value_set_none(out);
  return true;
}

bool atexit_run_exitfuncs(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "atexit._run_exitfuncs() expected no arguments";
    return false;
  }
  if (!runtime.run_exit_functions(error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

} // namespace

bool Runtime::run_exit_functions(std::string& error) {
  if (exit_functions_running_) {
    return true;
  }
  exit_functions_running_ = true;
  auto callbacks = std::move(exit_functions_);
  exit_functions_.clear();

  bool ok = true;
  for (auto it = callbacks.rbegin(); it != callbacks.rend(); ++it) {
    Value ignored;
    std::string call_error;
    if (!call_exit_callable(
            *this,
            it->callable,
            it->args.empty() ? nullptr : it->args.data(),
            static_cast<uint32_t>(it->args.size()),
            it->kwargs,
            ignored,
            call_error)) {
      ok = false;
      if (error.empty()) {
        error = std::move(call_error);
      }
    }
  }

  exit_functions_running_ = false;
  return ok;
}

void register_atexit_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "atexit");
  builder.function("register", atexit_register, nullptr, false, atexit_register_kw)
      .function("unregister", atexit_unregister)
      .function("_run_exitfuncs", atexit_run_exitfuncs);
  runtime.register_module("atexit", builder.finish());
}

} // namespace xlang3
