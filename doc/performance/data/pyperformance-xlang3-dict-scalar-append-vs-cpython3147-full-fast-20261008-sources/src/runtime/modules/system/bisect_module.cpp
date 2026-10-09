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

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cstdint>
#include <string>

namespace xlang3 {

namespace {

enum class BisectDirection { Left, Right };

bool bisect_index(Runtime& runtime, const Value& value, int64_t& out, std::string& error) {
  if (value_int_like_to_i64(value, out) || value_bigint_to_i64(value, out)) return true;
  Value method;
  std::string lookup_error;
  if (!object_get_attr(value, "__index__", method, lookup_error)) {
    error = "object cannot be interpreted as an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value converted;
  if (!runtime_call_callable(runtime, method, nullptr, 0, converted, error)) return false;
  if (value_int_like_to_i64(converted, out) || value_bigint_to_i64(converted, out)) return true;
  error = "__index__ returned non-int";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool bisect_length(Runtime& runtime, const Value& sequence, int64_t& out, std::string& error) {
  Value length;
  if (sequence_len(sequence, length, error) && value_int_like_to_i64(length, out)) return true;
  Value method;
  std::string lookup_error;
  if (!object_get_attr(sequence, "__len__", method, lookup_error) ||
      !runtime_call_callable(runtime, method, nullptr, 0, length, error) ||
      !bisect_index(runtime, length, out, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      runtime.set_pending_exception(std::move(pending));
    } else {
      error = "object has no len()";
      runtime.raise_class_error("TypeError", error);
    }
    return false;
  }
  if (out < 0) {
    error = "__len__() should return >= 0";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

bool bisect_item(Runtime& runtime, const Value& sequence, int64_t index, Value& out, std::string& error) {
  if (sequence_get_item(sequence, Value::int64(index), out, error)) return true;
  Value method;
  std::string lookup_error;
  if (!object_get_attr(sequence, "__getitem__", method, lookup_error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value argument = Value::int64(index);
  return runtime_call_callable(runtime, method, &argument, 1, out, error);
}

struct BisectArguments {
  const Value* sequence = nullptr;
  const Value* needle = nullptr;
  Value lo = Value::int64(0);
  Value hi = Value::none();
  Value key = Value::none();
};

bool parse_bisect_arguments(
    Runtime& runtime,
    const char* name,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    BisectArguments& parsed,
    std::string& error) {
  if (argc > 4) {
    error = std::string(name) + "() takes at most 4 positional arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool have_sequence = argc >= 1;
  bool have_needle = argc >= 2;
  if (have_sequence) parsed.sequence = &args[0];
  if (have_needle) parsed.needle = &args[1];
  if (argc >= 3) value_assign_fast(parsed.lo, args[2]);
  if (argc >= 4) value_assign_fast(parsed.hi, args[3]);
  bool have_lo = argc >= 3;
  bool have_hi = argc >= 4;
  bool have_key = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string keyword = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    const Value* value = kwargs[i].value;
    bool* present = nullptr;
    Value* target = nullptr;
    const Value** pointer_target = nullptr;
    if (keyword == "a") { present = &have_sequence; pointer_target = &parsed.sequence; }
    else if (keyword == "x") { present = &have_needle; pointer_target = &parsed.needle; }
    else if (keyword == "lo") { present = &have_lo; target = &parsed.lo; }
    else if (keyword == "hi") { present = &have_hi; target = &parsed.hi; }
    else if (keyword == "key") { present = &have_key; target = &parsed.key; }
    else {
      error = std::string(name) + "() got an unexpected keyword argument '" + keyword + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (*present) {
      error = std::string(name) + "() got multiple values for argument '" + keyword + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    *present = true;
    if (pointer_target != nullptr) {
      *pointer_target = value;
    } else if (value != nullptr) {
      value_assign_fast(*target, *value);
    }
  }
  if (!have_sequence || parsed.sequence == nullptr) {
    error = std::string(name) + "() missing required argument 'a' (pos 1)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!have_needle || parsed.needle == nullptr) {
    error = std::string(name) + "() missing required argument 'x' (pos 2)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool bisect_search(
    Runtime& runtime,
    const BisectArguments& args,
    BisectDirection direction,
    int64_t& insertion_point,
    std::string& error) {
  int64_t lo = 0;
  if (!bisect_index(runtime, args.lo, lo, error)) return false;
  if (lo < 0) {
    error = "lo must be non-negative";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  int64_t hi = 0;
  if (args.hi.tag == ValueTag::None) {
    if (!bisect_length(runtime, *args.sequence, hi, error)) return false;
  } else if (!bisect_index(runtime, args.hi, hi, error)) {
    return false;
  }
  while (lo < hi) {
    const int64_t mid = lo + ((hi - lo) / 2);
    Value item;
    if (!bisect_item(runtime, *args.sequence, mid, item, error)) return false;
    if (args.key.tag != ValueTag::None) {
      Value transformed;
      if (!runtime_call_callable(runtime, args.key, &item, 1, transformed, error)) return false;
      item = std::move(transformed);
    }
    Value comparison;
    const Value& left = direction == BisectDirection::Left ? item : *args.needle;
    const Value& right = direction == BisectDirection::Left ? *args.needle : item;
    if (!runtime_value_compare(runtime, "<", left, right, comparison, error)) return false;
    bool less = false;
    if (!runtime_truthy(runtime, comparison, less, error)) return false;
    if (direction == BisectDirection::Left) {
      if (less) lo = mid + 1;
      else hi = mid;
    } else {
      if (less) hi = mid;
      else lo = mid + 1;
    }
  }
  insertion_point = lo;
  return true;
}

bool bisect_impl_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  const auto direction = user_data == nullptr ? BisectDirection::Left : BisectDirection::Right;
  const char* name = direction == BisectDirection::Left ? "bisect_left" : "bisect_right";
  BisectArguments parsed;
  if (!parse_bisect_arguments(runtime, name, args, argc, kwargs, kwargc, parsed, error)) return false;
  int64_t result = 0;
  if (!bisect_search(runtime, parsed, direction, result, error)) return false;
  value_set_int64(out, result);
  return true;
}

bool bisect_impl(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return bisect_impl_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool bisect_insert(Runtime& runtime, const Value& sequence, int64_t index, const Value& item, Value& out, std::string& error) {
  if (auto* list = value_as_list(sequence)) {
    const size_t position = static_cast<size_t>(index < 0 ? 0 : index);
    list->items.insert(list->items.begin() + static_cast<std::ptrdiff_t>(position), item);
    value_set_none(out);
    return true;
  }
  Value insert;
  if (!object_get_attr(sequence, "insert", insert, error)) {
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  Value insert_args[] = {Value::int64(index), item};
  return runtime_call_callable(runtime, insert, insert_args, 2, out, error);
}

bool insort_impl_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  const auto direction = user_data == nullptr ? BisectDirection::Left : BisectDirection::Right;
  const char* name = direction == BisectDirection::Left ? "insort_left" : "insort_right";
  BisectArguments parsed;
  if (!parse_bisect_arguments(runtime, name, args, argc, kwargs, kwargc, parsed, error)) return false;
  Value keyed_needle;
  if (parsed.key.tag == ValueTag::None) value_assign_fast(keyed_needle, *parsed.needle);
  else if (!runtime_call_callable(runtime, parsed.key, parsed.needle, 1, keyed_needle, error)) return false;
  BisectArguments search = parsed;
  search.needle = &keyed_needle;
  int64_t position = 0;
  if (!bisect_search(runtime, search, direction, position, error)) return false;
  return bisect_insert(runtime, *parsed.sequence, position, *parsed.needle, out, error);
}

bool insort_impl(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return insort_impl_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

} // namespace

void register_bisect_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_bisect");
  builder.value("bisect_left", runtime.make_native_function(
              "_bisect.bisect_left", bisect_impl, nullptr, nullptr, nullptr, false, bisect_impl_kw, false))
      .value("bisect_right", runtime.make_native_function(
              "_bisect.bisect_right", bisect_impl, reinterpret_cast<void*>(1), nullptr, nullptr, false, bisect_impl_kw, false))
      .value("insort_left", runtime.make_native_function(
              "_bisect.insort_left", insort_impl, nullptr, nullptr, nullptr, false, insort_impl_kw, false))
      .value("insort_right", runtime.make_native_function(
              "_bisect.insort_right", insort_impl, reinterpret_cast<void*>(1), nullptr, nullptr, false, insort_impl_kw, false));
  runtime.register_module("_bisect", builder.finish());
}

} // namespace xlang3
