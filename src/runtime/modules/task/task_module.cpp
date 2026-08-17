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

bool task_spawn(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  return xlang_task_spawn(runtime, args, argc, out, error);
}

bool task_join(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "task.join() expected one Task argument";
    return false;
  }
  return xlang_task_join_value(args[0], out, error);
}

bool task_done(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)runtime;
  (void)user_data;
  if (argc != 1) {
    error = "task.done() expected one Task argument";
    return false;
  }
  bool done = false;
  if (!xlang_task_done_value(args[0], done, error)) {
    return false;
  }
  value_set_bool(out, done);
  return true;
}

bool task_await_all(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc != 1) {
    error = "task.await_all() expected one list argument";
    return false;
  }
  return xlang_task_await_all(runtime, args[0], out, error);
}

} // namespace

void register_task_modules(Runtime& runtime) {
  NativeModuleBuilder task_builder(runtime, "task");
  task_builder.value("Task", xlang_task_make_task_class(runtime))
      .function("spawn", task_spawn)
      .function("join", task_join)
      .function("done", task_done)
      .function("await_all", task_await_all);
  runtime.register_module("task", task_builder.finish());
}

} // namespace xlang3
