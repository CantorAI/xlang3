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
#include "xlang3/object_model.h"

namespace xlang3 {

namespace {

thread_local Value g_current_event_loop;

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

bool event_loop_run_until_complete(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "EventLoop.run_until_complete() expected awaitable";
    return false;
  }
  return xlang_task_await_value(runtime, args[1], out, error);
}

bool event_loop_create_task(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "EventLoop.create_task() expected awaitable";
    return false;
  }
  if (value_as_function(args[1]) != nullptr || value_as_native_function(args[1]) != nullptr) {
    return xlang_task_spawn(runtime, args + 1, 1, out, error);
  }
  return xlang_task_completed(runtime, args[1], out, error);
}

bool event_loop_close(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "EventLoop.close() expected no arguments";
    return false;
  }
  std::string ignored;
  object_set_attr(const_cast<Value&>(args[0]), "_closed", Value::boolean(true), ignored);
  value_set_none(out);
  return true;
}

bool event_loop_is_closed(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "EventLoop.is_closed() expected no arguments";
    return false;
  }
  Value closed;
  std::string ignored;
  if (object_get_attr(args[0], "_closed", closed, ignored)) {
    value_set_bool(out, value_truthy(closed));
  } else {
    value_set_bool(out, false);
  }
  return true;
}

Value make_event_loop_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("asyncio")});
  attrs.push_back({"run_until_complete", runtime.make_native_function("asyncio.EventLoop.run_until_complete", event_loop_run_until_complete)});
  attrs.push_back({"create_task", runtime.make_native_function("asyncio.EventLoop.create_task", event_loop_create_task)});
  attrs.push_back({"close", runtime.make_native_function("asyncio.EventLoop.close", event_loop_close)});
  attrs.push_back({"is_closed", runtime.make_native_function("asyncio.EventLoop.is_closed", event_loop_is_closed)});
  return Value::class_object("EventLoop", std::move(attrs));
}

Value make_event_loop(Runtime& runtime) {
  Value loop = Value::instance(make_event_loop_class(runtime));
  std::string ignored;
  object_set_attr(loop, "_closed", Value::boolean(false), ignored);
  return loop;
}

bool asyncio_new_event_loop(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    error = "asyncio.new_event_loop() expected no arguments";
    return false;
  }
  out = make_event_loop(runtime);
  return true;
}

bool asyncio_get_event_loop(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    error = "asyncio.get_event_loop() expected no arguments";
    return false;
  }
  if (g_current_event_loop.tag == ValueTag::Invalid || g_current_event_loop.tag == ValueTag::None) {
    g_current_event_loop = make_event_loop(runtime);
  }
  value_assign_fast(out, g_current_event_loop);
  return true;
}

bool asyncio_set_event_loop(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "asyncio.set_event_loop() expected one argument";
    return false;
  }
  value_assign_fast(g_current_event_loop, args[0]);
  value_set_none(out);
  return true;
}

bool asyncio_get_running_loop(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  return asyncio_get_event_loop(runtime, args, argc, out, error, user_data);
}

bool asyncio_sleep(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc > 2) {
    error = "asyncio.sleep() expected delay and optional result";
    return false;
  }
  Value result = argc == 2 ? args[1] : Value::none();
  return xlang_task_completed(runtime, result, out, error);
}

} // namespace

void register_asyncio_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "asyncio");
  builder.value("EventLoop", make_event_loop_class(runtime))
      .function("create_task", asyncio_create_task)
      .function("run", asyncio_run)
      .function("gather", asyncio_gather)
      .function("new_event_loop", asyncio_new_event_loop)
      .function("get_event_loop", asyncio_get_event_loop)
      .function("set_event_loop", asyncio_set_event_loop)
      .function("get_running_loop", asyncio_get_running_loop)
      .function("sleep", asyncio_sleep);
  runtime.register_module("asyncio", builder.finish());
}

} // namespace xlang3
