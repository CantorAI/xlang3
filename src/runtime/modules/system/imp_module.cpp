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

bool imp_is_frozen_package(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return imp_is_frozen(runtime, args, argc, out, error, user_data);
}

bool imp_create_builtin(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_imp.create_builtin() expected spec";
    return false;
  }
  Value name_value;
  if (!object_get_attr(args[0], "name", name_value, error)) {
    return false;
  }
  std::string name;
  if (!get_string_arg(name_value, "_imp.create_builtin spec.name", name, error)) {
    return false;
  }
  if (!runtime.has_registered_module(name)) {
    value_set_none(out);
    return true;
  }
  return runtime.import_module(name, out, error);
}

bool imp_exec_builtin(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_module(args[0]) == nullptr) {
    error = "_imp.exec_builtin() expected module";
    return false;
  }
  value_set_int64(out, 0);
  return true;
}

bool imp_find_frozen(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_imp.find_frozen() expected name";
    return false;
  }
  std::string ignored;
  if (!get_string_arg(args[0], "_imp.find_frozen name", ignored, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool imp_init_frozen(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return imp_find_frozen(runtime, args, argc, out, error, user_data);
}

bool imp_get_frozen_object(Runtime& runtime, const Value* args, uint32_t argc, Value&, std::string& error, void*) {
  if (argc != 1) {
    error = "_imp.get_frozen_object() expected name";
    runtime.raise_class_error("ImportError", error);
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "_imp.get_frozen_object name", name, error)) {
    return false;
  }
  error = "No such frozen object named '" + name + "'";
  runtime.raise_class_error("ImportError", error);
  return false;
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

uint64_t imp_siphash13(uint64_t key, std::string_view source) {
  uint64_t v0 = UINT64_C(0x736f6d6570736575) ^ key;
  uint64_t v1 = UINT64_C(0x646f72616e646f6d);
  uint64_t v2 = UINT64_C(0x6c7967656e657261) ^ key;
  uint64_t v3 = UINT64_C(0x7465646279746573);
  const auto rotate_left = [](uint64_t value, unsigned bits) {
    return (value << bits) | (value >> (64u - bits));
  };
  const auto round = [&]() {
    v0 += v1; v1 = rotate_left(v1, 13) ^ v0; v0 = rotate_left(v0, 32);
    v2 += v3; v3 = rotate_left(v3, 16) ^ v2;
    v0 += v3; v3 = rotate_left(v3, 21) ^ v0;
    v2 += v1; v1 = rotate_left(v1, 17) ^ v2; v2 = rotate_left(v2, 32);
  };
  size_t offset = 0;
  while (source.size() - offset >= 8) {
    uint64_t word = 0;
    for (size_t index = 0; index < 8; ++index) {
      word |= static_cast<uint64_t>(static_cast<unsigned char>(source[offset + index])) << (index * 8u);
    }
    v3 ^= word;
    round();
    v0 ^= word;
    offset += 8;
  }
  uint64_t tail = static_cast<uint64_t>(source.size()) << 56u;
  for (size_t index = 0; offset + index < source.size(); ++index) {
    tail |= static_cast<uint64_t>(static_cast<unsigned char>(source[offset + index])) << (index * 8u);
  }
  v3 ^= tail;
  round();
  v0 ^= tail;
  v2 ^= 0xffu;
  round(); round(); round();
  return v0 ^ v1 ^ v2 ^ v3;
}

bool imp_source_hash(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64) {
    error = "_imp.source_hash() expected magic and source bytes";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto* bytes = value_as_bytes(args[1]);
  if (bytes == nullptr) {
    error = "_imp.source_hash() source must be bytes";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const uint64_t hash = imp_siphash13(
      static_cast<uint64_t>(args[0].as.i64), bytes_object_view(*bytes));
  std::string encoded(8, '\0');
  for (size_t index = 0; index < 8; ++index) {
    encoded[index] = static_cast<char>((hash >> (index * 8u)) & 0xffu);
  }
  out = Value::bytes(std::move(encoded));
  return true;
}

bool imp_fix_co_filename(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "_imp._fix_co_filename() expected code and filename";
    return false;
  }
  value_set_none(out);
  return true;
}

bool imp_override_frozen_modules_for_tests(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "_imp._override_frozen_modules_for_tests() expected one integer argument";
    return false;
  }
  value_set_none(out);
  return true;
}

bool imp_dynamic_not_available(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "dynamic extension loading is not available through _imp";
  runtime.raise_class_error("ImportError", error);
  return false;
}

} // namespace

void register_imp_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_imp");
  builder.function("acquire_lock", imp_acquire_lock)
      .function("release_lock", imp_release_lock)
      .function("lock_held", imp_lock_held)
      .function("is_builtin", imp_is_builtin)
      .function("is_frozen", imp_is_frozen)
      .function("is_frozen_package", imp_is_frozen_package)
      .function("create_builtin", imp_create_builtin)
      .function("exec_builtin", imp_exec_builtin)
      .function("find_frozen", imp_find_frozen)
      .function("init_frozen", imp_init_frozen)
      .function("get_frozen_object", imp_get_frozen_object)
      .function("get_magic", imp_get_magic)
      .function("extension_suffixes", imp_extension_suffixes)
      .function("source_hash", imp_source_hash)
      .function("_fix_co_filename", imp_fix_co_filename)
      .function("_override_frozen_modules_for_tests", imp_override_frozen_modules_for_tests)
      .function("create_dynamic", imp_dynamic_not_available)
      .function("exec_dynamic", imp_dynamic_not_available)
      .value("pyc_magic_number_token", Value::int64(0x0a0d5833))
      .value("check_hash_based_pycs", Value::string("default"));
  runtime.register_module("_imp", builder.finish());
}

} // namespace xlang3
