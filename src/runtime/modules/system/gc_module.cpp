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

#include <array>

namespace xlang3 {

void emit_pending_socket_resource_warnings(Runtime& runtime);

namespace {

struct GcState {
  bool enabled = true;
  std::array<int64_t, 3> thresholds{700, 10, 10};
};

bool gc_enable(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 0) {
    error = "gc.enable() takes no arguments";
    return false;
  }
  static_cast<GcState*>(data)->enabled = true;
  value_set_none(out);
  return true;
}

bool gc_disable(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 0) {
    error = "gc.disable() takes no arguments";
    return false;
  }
  static_cast<GcState*>(data)->enabled = false;
  value_set_none(out);
  return true;
}

bool gc_isenabled(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 0) {
    error = "gc.isenabled() takes no arguments";
    return false;
  }
  value_set_bool(out, static_cast<GcState*>(data)->enabled);
  return true;
}

bool gc_collect(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1 || (argc == 1 && (args[0].tag != ValueTag::Int64 || args[0].as.i64 < 0 || args[0].as.i64 > 2))) {
    error = "gc.collect() generation must be an integer between 0 and 2";
    return false;
  }
  runtime.synchronize_modules_from_registry();
  runtime.release_dead_frame_registers();
  emit_pending_socket_resource_warnings(runtime);
  value_set_int64(out, static_cast<int64_t>(weakref_collect_cycles()));
  return true;
}

bool gc_get_count(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "gc.get_count() takes no arguments";
    return false;
  }
  out = Value::tuple({Value::int64(0), Value::int64(0), Value::int64(0)});
  return true;
}

bool gc_is_tracked(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "gc.is_tracked() takes exactly one argument";
    return false;
  }
  value_set_bool(out, args[0].tag == ValueTag::Object && args[0].as.obj != nullptr);
  return true;
}

bool gc_get_threshold(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 0) {
    error = "gc.get_threshold() takes no arguments";
    return false;
  }
  const auto& values = static_cast<GcState*>(data)->thresholds;
  out = Value::tuple({Value::int64(values[0]), Value::int64(values[1]), Value::int64(values[2])});
  return true;
}

bool gc_set_threshold(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc < 1 || argc > 3) {
    error = "gc.set_threshold() expected 1 to 3 arguments";
    return false;
  }
  auto& values = static_cast<GcState*>(data)->thresholds;
  for (uint32_t i = 0; i < argc; ++i) {
    if (args[i].tag != ValueTag::Int64) {
      error = "gc.set_threshold() arguments must be integers";
      return false;
    }
    values[i] = args[i].as.i64;
  }
  value_set_none(out);
  return true;
}

} // namespace

void register_gc_module(Runtime& runtime) {
  auto* state = new GcState();
  runtime.register_native_package_cleanup(state, [](void* data) { delete static_cast<GcState*>(data); });
  NativeModuleBuilder builder(runtime, "gc");
  builder.value("enable", runtime.make_native_function("gc.enable", gc_enable, state))
      .value("disable", runtime.make_native_function("gc.disable", gc_disable, state))
      .value("isenabled", runtime.make_native_function("gc.isenabled", gc_isenabled, state))
      .function("collect", gc_collect)
      .function("is_tracked", gc_is_tracked)
      .function("get_count", gc_get_count)
      .value("get_threshold", runtime.make_native_function("gc.get_threshold", gc_get_threshold, state))
      .value("set_threshold", runtime.make_native_function("gc.set_threshold", gc_set_threshold, state))
      .value("garbage", Value::list({}))
      .value("callbacks", Value::list({}));
  runtime.register_module("gc", builder.finish());
}

} // namespace xlang3
