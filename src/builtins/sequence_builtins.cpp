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
#include "xlang3/set_object.h"

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

bool builtin_len_fast(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 0 || register_arg_count != 1 || registers == nullptr || register_args == nullptr) {
    error = "len() expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!sequence_len(registers[register_args[0]], out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool builtin_next(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1 && argc != 2) {
    error = "next() expected 1 or 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value iterator = args[0];
  bool done = false;
  if (!sequence_iter_next(iterator, done, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (done) {
    if (argc == 2) {
      value_assign_fast(out, args[1]);
      return true;
    }
    error = "";
    runtime.raise_class_error("StopIteration", error);
    return false;
  }
  return true;
}

bool builtin_iter(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "iter() expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!sequence_get_iter(args[0], out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool builtin_ord(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc != 1) {
    error = "ord() expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].tag != ValueTag::Object || args[0].as.obj == nullptr || args[0].as.obj->kind != ObjectKind::String) {
    error = "ord() expected a character";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* string = reinterpret_cast<StringObject*>(args[0].as.obj);
  auto text = string_object_view(*string);
  if (utf8_codepoint_count(text) != 1) {
    error = "ord() expected a character";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto width = utf8_codepoint_width(static_cast<unsigned char>(text[0]));
  uint32_t codepoint = 0;
  if (width == 1) {
    codepoint = static_cast<unsigned char>(text[0]);
  } else if (width == 2 && text.size() >= 2) {
    codepoint = ((static_cast<unsigned char>(text[0]) & 0x1Fu) << 6) |
                (static_cast<unsigned char>(text[1]) & 0x3Fu);
  } else if (width == 3 && text.size() >= 3) {
    codepoint = ((static_cast<unsigned char>(text[0]) & 0x0Fu) << 12) |
                ((static_cast<unsigned char>(text[1]) & 0x3Fu) << 6) |
                (static_cast<unsigned char>(text[2]) & 0x3Fu);
  } else if (width == 4 && text.size() >= 4) {
    codepoint = ((static_cast<unsigned char>(text[0]) & 0x07u) << 18) |
                ((static_cast<unsigned char>(text[1]) & 0x3Fu) << 12) |
                ((static_cast<unsigned char>(text[2]) & 0x3Fu) << 6) |
                (static_cast<unsigned char>(text[3]) & 0x3Fu);
  }
  out = Value::int64(static_cast<int64_t>(codepoint));
  return true;
}

bool builtin_str(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  if (argc != 1) {
    error = "str() expected 1 argument";
    return false;
  }
  out = Value::string(value_to_string(args[0]));
  return true;
}

bool builtin_frozenset(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc > 1) {
    error = "frozenset() expected at most 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 0) {
    out = Value::set({});
    return true;
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> items;
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (done) {
      out = Value::set(std::move(items));
      return true;
    }
    items.push_back(std::move(item));
  }
}

} // namespace

void register_sequence_builtins(Runtime& runtime) {
  runtime.register_native_builtin("len", builtin_len, builtin_len_fast);
  runtime.register_native_builtin("iter", builtin_iter);
  runtime.register_native_builtin("next", builtin_next);
  runtime.register_native_builtin("ord", builtin_ord);
  runtime.register_native_builtin("frozenset", builtin_frozenset);
}

} // namespace xlang3
