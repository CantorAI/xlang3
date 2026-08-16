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

#include "xlang3/sequence.h"

namespace xlang3 {

namespace {

bool require_int_arg(const Value& value, const char* name, int64_t& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = std::string(name) + "() arguments must be int";
    return false;
  }
  out = value.as.i64;
  return true;
}

bool builtin_range(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc < 1 || argc > 3) {
    error = "range() expected 1 to 3 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t start = 0;
  int64_t stop = 0;
  int64_t step = 1;
  if (argc == 1) {
    if (!require_int_arg(args[0], "range", stop, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  } else {
    if (!require_int_arg(args[0], "range", start, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (!require_int_arg(args[1], "range", stop, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (argc == 3 && !require_int_arg(args[2], "range", step, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (step == 0) {
    error = "range() step must not be zero";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::range(start, stop, step);
  return true;
}

bool builtin_len(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc != 1) {
    error = "len() expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!sequence_len(args[0], out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

} // namespace

void register_sequence_builtins(Runtime& runtime) {
  runtime.register_native_builtin("range", builtin_range);
  runtime.register_native_builtin("len", builtin_len);
}

} // namespace xlang3
