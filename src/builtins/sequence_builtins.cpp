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

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cstring>

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

bool value_is_callable_for_iter(const Value& value) {
  if (value_as_native_function(value) != nullptr || value_as_function(value) != nullptr ||
      value_as_bound_method(value) != nullptr || value_as_class(value) != nullptr) {
    return true;
  }
  if (value_as_instance(value) != nullptr) {
    Value call_attr;
    std::string ignored;
    return object_get_class_attr_for_instance(value, "__call__", call_attr, ignored);
  }
  return false;
}

bool pending_exception_is(Runtime& runtime, const Value& exception, const char* class_name) {
  auto* klass = value_as_class(runtime.exception_type(exception));
  return klass != nullptr && klass->name == class_name;
}

void raise_stop_iteration_with_value(Runtime& runtime, const Value& return_value) {
  Value exception = runtime.make_exception("StopIteration", "");
  std::string ignored;
  object_set_attr(exception, "value", return_value, ignored);
  if (return_value.tag == ValueTag::None) {
    object_set_attr(exception, "args", Value::tuple({}), ignored);
  } else {
    object_set_attr(exception, "args", Value::tuple({return_value}), ignored);
    object_set_attr(exception, "message", return_value, ignored);
  }
  runtime.set_pending_exception(std::move(exception));
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

Value noninterned_string_value(const std::string& text) {
  Value out = Value::string_uninitialized(text.size());
  auto* string = value_as_string(out);
  if (string != nullptr && !text.empty()) {
    std::memcpy(string_object_mutable_data(*string), text.data(), text.size());
  }
  return out;
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
    Value next_method;
    std::string attr_error;
    if (!attribute_get(iterator, "__next__", next_method, attr_error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    std::string call_error;
    if (!runtime_call_callable(runtime, next_method, nullptr, 0, out, call_error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        if (pending_exception_is(runtime, pending, "StopIteration") && argc == 2) {
          value_assign_fast(out, args[1]);
          return true;
        }
        runtime.set_pending_exception(std::move(pending));
        return false;
      }
      error = call_error;
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    return true;
  }
  if (done) {
    if (argc == 2) {
      value_assign_fast(out, args[1]);
      return true;
    }
    error = "";
    raise_stop_iteration_with_value(runtime, out);
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
  if (argc == 2) {
    if (!value_is_callable_for_iter(args[0])) {
      error = "iter(v, w): v must be callable";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    out = functional_callable_iterator(&runtime, args[0], args[1]);
    return true;
  }
  if (argc != 1) {
    error = "iter() expected 1 or 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!runtime_get_iter(runtime, args[0], out, error)) {
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

} // namespace

bool builtin_str_from_value(Runtime& runtime, const Value& value, Value& out, std::string& error) {
  if (auto* instance = value_as_instance(value)) {
    auto* klass = value_as_class(instance->klass);
    if (klass != nullptr && klass->attrs.find("__str__") == klass->attrs.end() &&
        class_has_builtin_base_name(klass, "int")) {
      out = noninterned_string_value(value_to_string(value));
      return true;
    }
    Value str_method;
    std::string attr_error;
    if (attribute_get(value, "__str__", str_method, attr_error)) {
      Value result;
      if (!runtime_call_callable(runtime, str_method, nullptr, 0, result, error)) {
        return false;
      }
      if (value_as_string(result) == nullptr) {
        error = "__str__ returned non-string";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      out = result;
      return true;
    }
  }
  out = noninterned_string_value(value_to_string(value));
  return true;
}

namespace {

bool builtin_str(
    Runtime& runtime,
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
  return builtin_str_from_value(runtime, args[0], out, error);
}

} // namespace

void register_sequence_builtins(Runtime& runtime) {
  runtime.register_native_builtin("len", builtin_len, builtin_len_fast);
  runtime.register_native_builtin("iter", builtin_iter);
  runtime.register_native_builtin("next", builtin_next);
  runtime.register_native_builtin("ord", builtin_ord);
}

} // namespace xlang3
