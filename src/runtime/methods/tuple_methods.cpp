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

#include "xlang3/runtime.h"
#include "xlang3/value_hash.h"

#include <algorithm>

namespace xlang3 {

namespace {

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

static constexpr BuiltinMethodSpec kTupleMethods[] = {
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

} // namespace xlang3
