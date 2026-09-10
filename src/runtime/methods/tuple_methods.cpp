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
#include "xlang3/builtin_methods.h"

#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/value_hash.h"

#include <algorithm>

namespace xlang3 {

namespace {

bool tuple_getitem_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "tuple.__getitem__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!sequence_get_item(args[0], args[1], out, error)) {
    runtime.raise_class_error(error.find("range") != std::string::npos ? "IndexError" : "TypeError", error);
    return false;
  }
  return true;
}

bool tuple_iter_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "tuple.__iter__", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!sequence_get_iter(args[0], out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool normalize_bound(const Value& value, size_t size, size_t& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = "tuple index bounds must be int";
    return false;
  }
  int64_t index = value.as.i64;
  if (index < 0) {
    index += static_cast<int64_t>(size);
  }
  if (index < 0) {
    index = 0;
  }
  if (index > static_cast<int64_t>(size)) {
    index = static_cast<int64_t>(size);
  }
  out = static_cast<size_t>(index);
  return true;
}

bool tuple_count_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "tuple.count", error)) {
    return false;
  }
  auto* tuple = value_as_tuple(args[0]);
  if (tuple == nullptr) {
    error = "tuple.count target is not a tuple";
    return false;
  }
  int64_t count = 0;
  for (const auto& item : tuple->items) {
    if (value_key_equal(item, args[1])) {
      ++count;
    }
  }
  value_set_int64(out, count);
  return true;
}

bool tuple_index_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "tuple.index expected 2 to 4 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* tuple = value_as_tuple(args[0]);
  if (tuple == nullptr) {
    error = "tuple.index target is not a tuple";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  size_t start = 0;
  size_t stop = tuple->items.size();
  if (argc >= 3 && !normalize_bound(args[2], tuple->items.size(), start, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc >= 4 && !normalize_bound(args[3], tuple->items.size(), stop, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (start > stop) {
    start = stop;
  }
  for (size_t i = start; i < stop; ++i) {
    if (value_key_equal(tuple->items[i], args[1])) {
      value_set_int64(out, static_cast<int64_t>(i));
      return true;
    }
  }
  error = "tuple.index(x): x not in tuple";
  runtime.raise_class_error("ValueError", error);
  return false;
}

const TupleObject* tuple_protocol_storage(const Value& value, Value& scratch) {
  if (auto* tuple = value_as_tuple(value)) {
    return tuple;
  }
  if (value_as_instance(value) == nullptr) {
    return nullptr;
  }
  std::string ignored;
  if (!object_get_attr(value, "_tuple", scratch, ignored)) {
    return nullptr;
  }
  return value_as_tuple(scratch);
}

bool tuple_compare_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    const char* op,
    const char* method_name) {
  if (!method_check_argc(argc, 2, method_name, error)) {
    return false;
  }
  Value left_scratch;
  Value right_scratch;
  const auto* left = tuple_protocol_storage(args[0], left_scratch);
  const auto* right = tuple_protocol_storage(args[1], right_scratch);
  if (left == nullptr || right == nullptr) {
    if (const Value* not_implemented = runtime.find_builtin("NotImplemented")) {
      value_assign_fast(out, *not_implemented);
    } else {
      value_set_bool(out, std::string_view(op) == "!=");
    }
    return true;
  }
  const size_t common = std::min(left->items.size(), right->items.size());
  for (size_t i = 0; i < common; ++i) {
    Value equal;
    if (!runtime_value_compare(runtime, "==", left->items[i], right->items[i], equal, error)) {
      return false;
    }
    if (value_truthy(equal)) {
      continue;
    }
    if (std::string_view(op) == "==" || std::string_view(op) == "!=") {
      value_set_bool(out, std::string_view(op) == "!=");
      return true;
    }
    return runtime_value_compare(runtime, op, left->items[i], right->items[i], out, error);
  }
  const bool equal_size = left->items.size() == right->items.size();
  bool result = false;
  if (std::string_view(op) == "==") result = equal_size;
  else if (std::string_view(op) == "!=") result = !equal_size;
  else if (std::string_view(op) == "<") result = left->items.size() < right->items.size();
  else if (std::string_view(op) == "<=") result = left->items.size() <= right->items.size();
  else if (std::string_view(op) == ">") result = left->items.size() > right->items.size();
  else result = left->items.size() >= right->items.size();
  value_set_bool(out, result);
  return true;
}

#define XLANG3_TUPLE_COMPARE_METHOD(function_name, op_text, method_text) \
  bool function_name(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) { \
    return tuple_compare_impl(runtime, args, argc, out, error, op_text, method_text); \
  }

XLANG3_TUPLE_COMPARE_METHOD(tuple_eq_method, "==", "tuple.__eq__")
XLANG3_TUPLE_COMPARE_METHOD(tuple_ne_method, "!=", "tuple.__ne__")
XLANG3_TUPLE_COMPARE_METHOD(tuple_lt_method, "<", "tuple.__lt__")
XLANG3_TUPLE_COMPARE_METHOD(tuple_le_method, "<=", "tuple.__le__")
XLANG3_TUPLE_COMPARE_METHOD(tuple_gt_method, ">", "tuple.__gt__")
XLANG3_TUPLE_COMPARE_METHOD(tuple_ge_method, ">=", "tuple.__ge__")

#undef XLANG3_TUPLE_COMPARE_METHOD

static constexpr BuiltinMethodSpec kTupleMethods[] = {
    {"__getitem__", "tuple.__getitem__", tuple_getitem_method},
    {"__iter__", "tuple.__iter__", tuple_iter_method},
    {"__eq__", "tuple.__eq__", tuple_eq_method},
    {"__ne__", "tuple.__ne__", tuple_ne_method},
    {"__lt__", "tuple.__lt__", tuple_lt_method},
    {"__le__", "tuple.__le__", tuple_le_method},
    {"__gt__", "tuple.__gt__", tuple_gt_method},
    {"__ge__", "tuple.__ge__", tuple_ge_method},
    {"count", "tuple.count", tuple_count_method},
    {"index", "tuple.index", tuple_index_method},
};

} // namespace

bool tuple_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_tuple(object) == nullptr) {
    return false;
  }
  return bind_builtin_method_from_table(object, name, kTupleMethods, std::size(kTupleMethods), out);
}

bool tuple_install_class_methods(Runtime& runtime, ClassObject& tuple_class) {
  for (const auto& method : kTupleMethods) {
    tuple_class.attrs[method.name] = runtime.make_native_function(method.full_name, method.callback);
  }
  ++tuple_class.version;
  return true;
}

} // namespace xlang3
