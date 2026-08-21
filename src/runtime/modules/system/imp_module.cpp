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

#include <atomic>

namespace xlang3 {

namespace {

std::atomic_uint32_t g_import_lock_depth{0};

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

bool no_args(uint32_t argc, const char* name, std::string& error) {
  if (argc == 0) {
    return true;
  }
  error = std::string(name) + "() expected no arguments";
  return false;
}

bool imp_acquire_lock(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "_imp.acquire_lock", error)) {
    return false;
  }
  g_import_lock_depth.fetch_add(1, std::memory_order_acq_rel);
  value_set_none(out);
  return true;
}

bool imp_release_lock(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "_imp.release_lock", error)) {
    return false;
  }
  uint32_t depth = g_import_lock_depth.load(std::memory_order_acquire);
  while (depth != 0) {
    if (g_import_lock_depth.compare_exchange_weak(depth, depth - 1, std::memory_order_acq_rel)) {
      value_set_none(out);
      return true;
    }
  }
  error = "not holding the import lock";
  return false;
}

bool imp_lock_held(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "_imp.lock_held", error)) {
    return false;
  }
  out = Value::boolean(g_import_lock_depth.load(std::memory_order_acquire) != 0);
  return true;
}

bool imp_is_builtin(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_imp.is_builtin() expected one argument";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "_imp.is_builtin name", name, error)) {
    return false;
  }
  value_set_int64(out, runtime.has_registered_module(name) ? -1 : 0);
  return true;
}

bool imp_is_frozen(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_imp.is_frozen() expected one argument";
    return false;
  }
  std::string ignored;
  if (!get_string_arg(args[0], "_imp.is_frozen name", ignored, error)) {
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool imp_get_magic(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "_imp.get_magic", error)) {
    return false;
  }
  out = Value::bytes("X3IR");
  return true;
}

bool imp_extension_suffixes(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "_imp.extension_suffixes", error)) {
    return false;
  }
#if defined(_WIN32)
  out = Value::list({Value::string(".x3pkg.dll"), Value::string(".pyd")});
#elif defined(__APPLE__)
  out = Value::list({Value::string(".x3pkg.dylib"), Value::string(".so")});
#else
  out = Value::list({Value::string(".x3pkg.so"), Value::string(".so")});
#endif
  return true;
}

} // namespace

void register_imp_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_imp");
  builder.function("acquire_lock", imp_acquire_lock)
      .function("release_lock", imp_release_lock)
      .function("lock_held", imp_lock_held)
      .function("is_builtin", imp_is_builtin)
      .function("is_frozen", imp_is_frozen)
      .function("get_magic", imp_get_magic)
      .function("extension_suffixes", imp_extension_suffixes);
  runtime.register_module("_imp", builder.finish());
}

} // namespace xlang3
