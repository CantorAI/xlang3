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
#include "xlang3/object_model.h"

#include <chrono>
#include <ctime>
#include <thread>

namespace xlang3 {

namespace {

bool no_args(uint32_t argc, const char* name, std::string& error) {
  if (argc == 0) {
    return true;
  }
  error = std::string(name) + "() expected no arguments";
  return false;
}

int64_t duration_to_ns(std::chrono::nanoseconds value) {
  return static_cast<int64_t>(value.count());
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  auto* string = value_as_string(value);
  if (string == nullptr) {
    error = std::string(name) + " must be str";
    return false;
  }
  out = string_object_to_string(*string);
  return true;
}

Value make_clock_info(Runtime& runtime, bool adjustable, bool monotonic, double resolution, const std::string& implementation) {
  Value info = Value::instance(Value::class_object("SimpleNamespace", {}));
  std::string ignored;
  object_set_attr(info, "adjustable", Value::boolean(adjustable), ignored);
  object_set_attr(info, "monotonic", Value::boolean(monotonic), ignored);
  object_set_attr(info, "resolution", Value::number(resolution), ignored);
  object_set_attr(info, "implementation", Value::string(implementation), ignored);
  return info;
}

bool time_time(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.time", error)) {
    return false;
  }
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  out = Value::number(std::chrono::duration<double>(now).count());
  return true;
}

bool time_time_ns(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.time_ns", error)) {
    return false;
  }
  value_set_int64(out, duration_to_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch())));
  return true;
}

bool time_monotonic(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.monotonic", error)) {
    return false;
  }
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  out = Value::number(std::chrono::duration<double>(now).count());
  return true;
}

bool time_monotonic_ns(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.monotonic_ns", error)) {
    return false;
  }
  value_set_int64(out, duration_to_ns(std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())));
  return true;
}

bool time_sleep(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "time.sleep() expected one argument";
    return false;
  }
  double seconds = 0.0;
  if (args[0].tag == ValueTag::Int64) {
    seconds = static_cast<double>(args[0].as.i64);
  } else if (args[0].tag == ValueTag::Double) {
    seconds = args[0].as.f64;
  } else {
    error = "time.sleep() argument must be int or float";
    return false;
  }
  if (seconds < 0.0) {
    error = "sleep length must be non-negative";
    return false;
  }
  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  value_set_none(out);
  return true;
}

bool time_process_time(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.process_time", error)) {
    return false;
  }
  out = Value::number(static_cast<double>(std::clock()) / static_cast<double>(CLOCKS_PER_SEC));
  return true;
}

bool time_process_time_ns(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "time.process_time_ns", error)) {
    return false;
  }
  const auto ticks = static_cast<int64_t>(std::clock());
  value_set_int64(out, static_cast<int64_t>((static_cast<long double>(ticks) * 1000000000.0L) / CLOCKS_PER_SEC));
  return true;
}

bool time_thread_time(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return time_process_time(runtime, args, argc, out, error, user_data);
}

bool time_thread_time_ns(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return time_process_time_ns(runtime, args, argc, out, error, user_data);
}

bool time_get_clock_info(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "time.get_clock_info() expected clock name";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "time.get_clock_info name", name, error)) {
    return false;
  }
  if (name == "time") {
    out = make_clock_info(runtime, true, false, 1e-9, "std::chrono::system_clock");
    return true;
  }
  if (name == "monotonic" || name == "perf_counter") {
    out = make_clock_info(runtime, false, true, 1e-9, "std::chrono::steady_clock");
    return true;
  }
  if (name == "process_time" || name == "thread_time") {
    out = make_clock_info(runtime, false, true, 1.0 / static_cast<double>(CLOCKS_PER_SEC), "std::clock");
    return true;
  }
  error = "unknown clock";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool time_mktime(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "time.mktime() expected one time tuple";
    return false;
  }
  out = Value::number(0.0);
  return true;
}

} // namespace

void register_time_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "time");
  builder.function("time", time_time)
      .function("time_ns", time_time_ns)
      .function("monotonic", time_monotonic)
      .function("monotonic_ns", time_monotonic_ns)
      .function("perf_counter", time_monotonic)
      .function("perf_counter_ns", time_monotonic_ns)
      .function("process_time", time_process_time)
      .function("process_time_ns", time_process_time_ns)
      .function("thread_time", time_thread_time)
      .function("thread_time_ns", time_thread_time_ns)
      .function("get_clock_info", time_get_clock_info)
      .function("sleep", time_sleep)
      .function("mktime", time_mktime);
  runtime.register_module("time", builder.finish());
}

} // namespace xlang3
