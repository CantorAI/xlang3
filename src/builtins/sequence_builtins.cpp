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
#include "xlang3/builtin_methods.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/generator.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <cstring>

namespace xlang3 {

namespace {

bool require_int_arg(const Value& value, const char* name, int64_t& out, std::string& error) {
  if (!value_int_like_to_i64(value, out)) {
    error = std::string(name) + "() arguments must be int";
    return false;
  }
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

bool decimal_default_str(const Value& value, Value& out) {
  auto* instance = value_as_instance(value);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (klass == nullptr || klass->name != "Decimal") return false;

  // pyperformance's telco prints one Decimal for each input record.  For the
  // unmodified stdlib Decimal class, its four immutable slots already contain
  // exactly the data __str__ walks in Python; format those slots directly and
  // keep custom classes/methods on the normal descriptor path below.
  auto* globals = value_as_module(klass->globals_module);
  if (globals == nullptr) return false;
  Value module_name;
  std::string ignored;
  if (!module_get_attr(klass->globals_module, "__name__", module_name, ignored) ||
      value_as_string(module_name) == nullptr ||
      string_object_view(*value_as_string(module_name)) != "decimal") {
    return false;
  }
  Value exported_class;
  if (!module_get_attr(klass->globals_module, "Decimal", exported_class, ignored) ||
      value_as_class(exported_class) != klass) {
    return false;
  }
  const auto str_method = klass->attrs.find("__str__");
  auto* function = str_method == klass->attrs.end()
      ? nullptr
      : value_as_function(str_method->second);
  if (function == nullptr || function->qualname != "Decimal.__str__" ||
      value_as_module(function->globals_module) != globals) {
    return false;
  }

  const auto exp_slot = klass->instance_slot_indices.find("_exp");
  const auto int_slot = klass->instance_slot_indices.find("_int");
  const auto sign_slot = klass->instance_slot_indices.find("_sign");
  const auto special_slot = klass->instance_slot_indices.find("_is_special");
  if (exp_slot == klass->instance_slot_indices.end() ||
      int_slot == klass->instance_slot_indices.end() ||
      sign_slot == klass->instance_slot_indices.end() ||
      special_slot == klass->instance_slot_indices.end() ||
      exp_slot->second >= instance_slot_count(instance) ||
      int_slot->second >= instance_slot_count(instance) ||
      sign_slot->second >= instance_slot_count(instance) ||
      special_slot->second >= instance_slot_count(instance)) {
    return false;
  }

  const Value& exponent = instance_slot_at(instance, exp_slot->second);
  const Value& digits_value = instance_slot_at(instance, int_slot->second);
  const Value& sign_value = instance_slot_at(instance, sign_slot->second);
  const Value& special_value = instance_slot_at(instance, special_slot->second);
  const auto* digits_object = value_as_string(digits_value);
  if (digits_object == nullptr || sign_value.tag != ValueTag::Int64 ||
      special_value.tag != ValueTag::Bool) {
    return false;
  }
  const bool negative = sign_value.as.i64 != 0;
  const std::string_view digits = string_object_view(*digits_object);
  std::string result;
  if (special_value.as.b) {
    if (value_as_string(exponent) == nullptr) return false;
    const std::string_view kind = string_object_view(*value_as_string(exponent));
    if (kind == "F") result = negative ? "-Infinity" : "Infinity";
    else if (kind == "n") result = (negative ? "-NaN" : "NaN") + std::string(digits);
    else if (kind == "N") result = (negative ? "-sNaN" : "sNaN") + std::string(digits);
    else return false;
    out = noninterned_string_value(std::move(result));
    return true;
  }
  if (exponent.tag != ValueTag::Int64 || digits.empty()) return false;
  const int64_t exp = exponent.as.i64;
  // Avoid speculative huge allocations for unusual values; the Python method
  // remains the compatibility fallback for large exponents.
  if (exp < -1000000 || exp > 1000000 || digits.size() > 1000000) return false;
  const int64_t leftdigits = exp + static_cast<int64_t>(digits.size());
  const int64_t dotplace = exp <= 0 && leftdigits > -6 ? leftdigits : 1;
  result.reserve(digits.size() + 32 +
      static_cast<size_t>(dotplace <= 0 ? -dotplace : 0) +
      static_cast<size_t>(dotplace > static_cast<int64_t>(digits.size())
          ? dotplace - static_cast<int64_t>(digits.size()) : 0));
  if (negative) result.push_back('-');
  if (dotplace <= 0) {
    result += "0.";
    result.append(static_cast<size_t>(-dotplace), '0');
    result.append(digits);
  } else if (dotplace >= static_cast<int64_t>(digits.size())) {
    result.append(digits);
    result.append(static_cast<size_t>(dotplace - static_cast<int64_t>(digits.size())), '0');
  } else {
    result.append(digits.substr(0, static_cast<size_t>(dotplace)));
    result.push_back('.');
    result.append(digits.substr(static_cast<size_t>(dotplace)));
  }
  if (leftdigits != dotplace) {
    const int64_t scientific_exp = leftdigits - dotplace;
    result.push_back('e');
    result.push_back(scientific_exp >= 0 ? '+' : '-');
    result += std::to_string(scientific_exp >= 0 ? scientific_exp : -scientific_exp);
  }
  out = noninterned_string_value(std::move(result));
  return true;
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
  Value start = Value::int64(0);
  Value stop;
  Value step = Value::int64(1);
  if (argc == 1) {
    int64_t ignored = 0;
    if (!value_int_like_to_i64(args[0], ignored) && value_as_bigint(args[0]) == nullptr) {
      error = "range() arguments must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_assign_fast(stop, args[0]);
  } else {
    int64_t ignored = 0;
    if ((!value_int_like_to_i64(args[0], ignored) && value_as_bigint(args[0]) == nullptr) ||
        (!value_int_like_to_i64(args[1], ignored) && value_as_bigint(args[1]) == nullptr)) {
      error = "range() arguments must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_assign_fast(start, args[0]);
    value_assign_fast(stop, args[1]);
    if (argc == 3) {
      if (!value_int_like_to_i64(args[2], ignored) && value_as_bigint(args[2]) == nullptr) {
        error = "range() arguments must be int";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(step, args[2]);
    }
  }
  Value zero_compare;
  if (!value_int_like_compare("==", step, Value::int64(0), zero_compare) ||
      zero_compare.tag != ValueTag::Bool) {
    error = "range() arguments must be int";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (zero_compare.as.b) {
    error = "range() step must not be zero";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::range_values(start, stop, step);
  return true;
}

Value noninterned_string_value(const std::string& text) {
  Value out = Value::string_uninitialized(text.size());
  auto* string = value_as_string(out);
  if (string != nullptr && !text.empty()) {
    std::memcpy(string_object_mutable_data(*string), text.data(), text.size());
  }
  if (string != nullptr) string_object_refresh_ascii(*string);
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
    Value method;
    std::string attr_error;
    if (attribute_get(args[0], "__len__", method, attr_error)) {
      error.clear();
      return runtime_call_callable(runtime, method, nullptr, 0, out, error);
    }
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
    Value method;
    std::string attr_error;
    if (attribute_get(registers[register_args[0]], "__len__", method, attr_error)) {
      error.clear();
      return runtime_call_callable(runtime, method, nullptr, 0, out, error);
    }
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
    // Sequence and functional iterators can report a real exception (for
    // example a deque changed during iteration).  Preserve it instead of
    // treating every failed fast-path attempt as an unsupported iterator and
    // replacing the exception with TypeError below.
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      runtime.set_pending_exception(std::move(pending));
      return false;
    }
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
  if (value_as_instance(args[0]) != nullptr ||
      value_as_class(args[0]) != nullptr) {
    Value iter_method;
    std::string method_error;
    if (object_get_special_method(runtime, args[0], "__iter__",
                                  iter_method, method_error)) {
      Value iterator;
      if (!runtime_call_callable(
              runtime, iter_method, nullptr, 0, iterator, error))
        return false;
      Value next_method;
      std::string next_error;
      Value native_iterator;
      std::string native_error;
      if (!sequence_get_iter(iterator, native_iterator, native_error) &&
          !object_get_special_method(runtime, iterator, "__next__",
                                     next_method, next_error)) {
        error = "iter() returned non-iterator";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(out, iterator);
      return true;
    }
  }
  if (!runtime_get_iter(runtime, args[0], out, error)) {
    if (error == "object is not iterable") {
      error = "'" + std::string(value_binary_type_name(args[0])) + "' object is not iterable";
    }
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool builtin_aiter(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "aiter() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value method;
  if (!attribute_get(args[0], "__aiter__", method, error)) {
    error = "aiter() argument must be an async iterable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!runtime_call_callable(runtime, method, nullptr, 0, out, error)) {
    return false;
  }
  Value next_method;
  std::string next_error;
  if (!attribute_get(out, "__anext__", next_method, next_error)) {
    error = "aiter() returned not an async iterator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool builtin_anext(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1 && argc != 2) {
    error = "anext() expected 1 or 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value method;
  if (!attribute_get(args[0], "__anext__", method, error)) {
    error = "anext() argument must be an async iterator";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!runtime_call_callable(runtime, method, nullptr, 0, out, error)) {
    return false;
  }
  // Native async generators use this internal awaitable.  Its unused ANext
  // argument vector carries the optional default until exhaustion.
  if (argc == 2) {
    if (auto* awaitable = value_as_async_generator_awaitable(out);
        awaitable != nullptr && awaitable->kind == AsyncGenAwaitableKind::ANext) {
      awaitable->args = {args[1]};
    }
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
  if (const auto* bytes = value_as_bytes(args[0])) {
    const auto view = bytes_object_view(*bytes);
    if (view.size() != 1) {
      error = "ord() expected a character";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    out = Value::int64(static_cast<unsigned char>(view[0]));
    return true;
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
  if (value_as_generic_alias(value) != nullptr) {
    if (const Value* repr_function = runtime.find_builtin("repr")) {
      return runtime_call_callable(runtime, *repr_function, &value, 1, out, error);
    }
  }
  if (value_as_dict(value) != nullptr || value_as_list(value) != nullptr ||
      value_as_tuple(value) != nullptr || value_as_set(value) != nullptr ||
      value_as_mapping_proxy(value) != nullptr ||
      value_as_dict_view(value) != nullptr) {
    if (const Value* repr_function = runtime.find_builtin("repr")) {
      return runtime_call_callable(runtime, *repr_function, &value, 1, out, error);
    }
  }
  if (decimal_default_str(value, out)) return true;
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
      bool result_is_string = value_as_string(result) != nullptr;
      if (!result_is_string) {
        auto* result_instance = value_as_instance(result);
        auto* result_class = result_instance == nullptr
            ? nullptr : value_as_class(result_instance->klass);
        if (result_class != nullptr && class_has_builtin_base_name(result_class, "str")) {
          // CPython accepts and preserves a str subclass returned by __str__.
          // Check the runtime's stored string payload directly so SafeString's
          // common `return self` implementation keeps its identity and avoids
          // allocating a replacement string.
          for (const auto& attr : result_instance->attrs) {
            if (attr.first == "__xlang3_string_value__" && value_as_string(attr.second) != nullptr) {
              result_is_string = true;
              break;
            }
          }
        }
      }
      if (!result_is_string) {
        error = "__str__ returned non-string";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      out = result;
      return true;
    }
    Value repr_method;
    attr_error.clear();
    if (attribute_get(value, "__repr__", repr_method, attr_error)) {
      Value result;
      if (!runtime_call_callable(runtime, repr_method, nullptr, 0, result, error)) {
        return false;
      }
      if (value_as_string(result) == nullptr) {
        error = "__repr__ returned non-string";
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
  runtime.register_native_builtin(
      "next", builtin_next, builtin_fast_adapter<builtin_next, 2>);
  runtime.register_native_builtin("aiter", builtin_aiter);
  runtime.register_native_builtin("anext", builtin_anext);
  runtime.register_native_builtin("ord", builtin_ord, builtin_fast_adapter<builtin_ord, 1>);
}

} // namespace xlang3
