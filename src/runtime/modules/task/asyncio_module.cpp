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
#include "task_objects.h"

#include "xlang3/builtins.h"
#include "xlang3/module_object.h"

namespace xlang3 {

namespace {

bool asyncio_create_task(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc == 1 && value_as_function(args[0]) == nullptr && value_as_native_function(args[0]) == nullptr) {
    return xlang_task_completed(runtime, args[0], out, error);
  }
  return xlang_task_spawn(runtime, args, argc, out, error);
}

bool asyncio_run(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc < 1 || argc > 2) {
    error = "asyncio.run() expected Task or callable with optional args tuple";
    return false;
  }

  bool done = false;
  std::string task_error;
  if (xlang_task_done_value(args[0], done, task_error)) {
    return xlang_task_await_value(runtime, args[0], out, error);
  }

  if (argc == 1 && value_as_function(args[0]) == nullptr && value_as_native_function(args[0]) == nullptr) {
    value_assign_fast(out, args[0]);
    return true;
  }

  Value task;
  if (!xlang_task_spawn(runtime, args, argc, task, error)) {
    return false;
  }
  return xlang_task_join_value(task, out, error);
}

bool asyncio_gather(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc != 1) {
    error = "asyncio.gather() expected one list argument";
    return false;
  }
  return xlang_task_await_all(runtime, args[0], out, error);
}

} // namespace

void register_asyncio_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "asyncio");
  builder.function("create_task", asyncio_create_task)
      .function("run", asyncio_run)
      .function("gather", asyncio_gather);
  runtime.register_module("asyncio", builder.finish());
}

} // namespace xlang3
