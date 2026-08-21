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

#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"

#include <algorithm>

namespace xlang3 {

namespace {

bool is_callable_value(const Value& value) {
  return value_as_function(value) != nullptr ||
         value_as_native_function(value) != nullptr;
}

bool call_exit_callable(Runtime& runtime, const Value& callable, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (auto* native = value_as_native_function(callable)) {
    if (native->callback == nullptr) {
      error = "native exit function is not callable";
      return false;
    }
    return native->callback(runtime, args, argc, out, error, native->user_data);
  }

  if (auto* function = value_as_function(callable)) {
    CallArgsView call_args;
    call_args.leading = args;
    call_args.leading_count = argc;
    Interpreter interpreter(runtime);
    RuntimeResult result = interpreter.run_function_value(function, call_args);
    if (!result.errors.empty()) {
      error = result.errors.front();
      return false;
    }
    value_assign_fast(out, result.value);
    return true;
  }

  error = "exit function is not callable";
  return false;
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
  builder.function("register", atexit_register)
      .function("unregister", atexit_unregister)
      .function("_run_exitfuncs", atexit_run_exitfuncs);
  runtime.register_module("atexit", builder.finish());
}

} // namespace xlang3
