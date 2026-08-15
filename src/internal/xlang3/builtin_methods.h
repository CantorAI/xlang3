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
#pragma once

#include "xlang3/compiler.h"
#include "xlang3/value.h"

#include <cstddef>
#include <string>
#include <utility>

namespace xlang3 {

XLANG3_FORCE_INLINE bool method_check_argc(uint32_t argc, uint32_t expected, const char* name, std::string& error) {
  if (argc == expected) {
    return true;
  }
  error = std::string(name) + " expected " + std::to_string(expected) + " arguments, got " + std::to_string(argc);
  return false;
}

XLANG3_FORCE_INLINE bool bind_builtin_method(
    const Value& object,
    std::string full_name,
    NativeFunctionCallback callback,
    Value& out) {
  out = Value::bound_method(object, Value::native_function(0, std::move(full_name), callback));
  return true;
}

struct BuiltinMethodSpec {
  const char* name;
  const char* full_name;
  NativeFunctionCallback callback;
};

XLANG3_FORCE_INLINE bool bind_builtin_method_from_table(
    const Value& object,
    const std::string& name,
    const BuiltinMethodSpec* methods,
    size_t method_count,
    Value& out) {
  for (size_t i = 0; i < method_count; ++i) {
    if (name == methods[i].name) {
      return bind_builtin_method(object, methods[i].full_name, methods[i].callback, out);
    }
  }
  return false;
}

bool list_get_method(const Value& object, const std::string& name, Value& out);
bool dict_get_method(const Value& object, const std::string& name, Value& out);
bool set_get_method(const Value& object, const std::string& name, Value& out);
bool string_get_method(const Value& object, const std::string& name, Value& out);

} // namespace xlang3
