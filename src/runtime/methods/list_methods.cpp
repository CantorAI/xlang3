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
#include "xlang3/sequence.h"
#include "xlang3/value_hash.h"

#include <algorithm>

namespace xlang3 {

namespace {

bool normalize_insert_index(const Value& value, size_t size, size_t& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = "list index must be int";
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

bool normalize_existing_index(const Value& value, size_t size, size_t& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = "list index must be int";
    return false;
  }
  int64_t index = value.as.i64;
  if (index < 0) {
    index += static_cast<int64_t>(size);
  }
  if (index < 0 || index >= static_cast<int64_t>(size)) {
    error = "pop index out of range";
    return false;
  }
  out = static_cast<size_t>(index);
  return true;
}

bool normalize_bound(const Value& value, size_t size, size_t& out, std::string& error) {
  if (value.tag != ValueTag::Int64) {
    error = "list index bounds must be int";
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

bool list_append_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "list.append", error)) {
    return false;
  }
  Value list = args[0];
  if (!sequence_list_append(list, args[1], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool list_append_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count != 1 || leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "list.append expected 1 argument";
    return false;
  }
  Value list = leading[0];
  if (!sequence_list_append(list, registers[register_args[0]], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool list_pop_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "list.pop expected 1 or 2 arguments, got " + std::to_string(argc);
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.pop target is not a list";
    return false;
  }
  if (list->items.empty()) {
    error = "pop from empty list";
    return false;
  }
  size_t index = list->items.size() - 1;
  if (argc == 2 && !normalize_existing_index(args[1], list->items.size(), index, error)) {
    return false;
  }
  value_assign_fast(out, list->items[index]);
  list->items.erase(list->items.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

bool list_extend_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "list.extend", error)) {
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    return false;
  }
  std::vector<Value> items;
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    items.push_back(std::move(item));
  }
  Value list = args[0];
  for (const auto& item : items) {
    if (!sequence_list_append(list, item, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool list_insert_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 3, "list.insert", error)) {
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.insert target is not a list";
    return false;
  }
  size_t index = 0;
  if (!normalize_insert_index(args[1], list->items.size(), index, error)) {
    return false;
  }
  list->items.insert(list->items.begin() + static_cast<std::ptrdiff_t>(index), args[2]);
  value_set_none(out);
  return true;
}

bool list_clear_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "list.clear", error)) {
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.clear target is not a list";
    return false;
  }
  list->items.clear();
  value_set_none(out);
  return true;
}

bool list_copy_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "list.copy", error)) {
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.copy target is not a list";
    return false;
  }
  out = Value::list(list->items);
  return true;
}

bool list_count_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "list.count", error)) {
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.count target is not a list";
    return false;
  }
  int64_t count = 0;
  for (const auto& item : list->items) {
    if (value_key_equal(item, args[1])) {
      ++count;
    }
  }
  value_set_int64(out, count);
  return true;
}

bool list_index_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "list.index expected 2 to 4 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.index target is not a list";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  size_t start = 0;
  size_t stop = list->items.size();
  if (argc >= 3 && !normalize_bound(args[2], list->items.size(), start, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc >= 4 && !normalize_bound(args[3], list->items.size(), stop, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (start > stop) {
    start = stop;
  }
  for (size_t i = start; i < stop; ++i) {
    if (value_key_equal(list->items[i], args[1])) {
      value_set_int64(out, static_cast<int64_t>(i));
      return true;
    }
  }
  error = "list.index(x): x not in list";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool list_remove_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "list.remove", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.remove target is not a list";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (auto it = list->items.begin(); it != list->items.end(); ++it) {
    if (value_key_equal(*it, args[1])) {
      list->items.erase(it);
      value_set_none(out);
      return true;
    }
  }
  error = "list.remove(x): x not in list";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool list_reverse_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "list.reverse", error)) {
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.reverse target is not a list";
    return false;
  }
  std::reverse(list->items.begin(), list->items.end());
  value_set_none(out);
  return true;
}

bool list_sort_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "list.sort expected no positional arguments in this XLang3 compatibility subset";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "list.sort target is not a list";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::sort(list->items.begin(), list->items.end(), [&](const Value& lhs, const Value& rhs) {
    Value result;
    std::string compare_error;
    if (!value_compare("<", lhs, rhs, result, compare_error) || result.tag != ValueTag::Bool) {
      return false;
    }
    return result.as.b;
  });
  value_set_none(out);
  return true;
}

static constexpr BuiltinMethodSpec kListMethods[] = {
      {"append", "list.append", list_append_method, list_append_fast_method},
      {"clear", "list.clear", list_clear_method},
      {"copy", "list.copy", list_copy_method},
      {"count", "list.count", list_count_method},
      {"extend", "list.extend", list_extend_method},
      {"index", "list.index", list_index_method},
      {"insert", "list.insert", list_insert_method},
      {"pop", "list.pop", list_pop_method},
      {"remove", "list.remove", list_remove_method},
      {"reverse", "list.reverse", list_reverse_method},
      {"sort", "list.sort", list_sort_method},
};

} // namespace

const BuiltinMethodSpec* list_find_method_spec(const Value& object, const std::string& name) {
  if (value_as_list(object) == nullptr) {
    return nullptr;
  }
  for (const auto& method : kListMethods) {
    if (name == method.name) {
      return &method;
    }
  }
  return nullptr;
}

bool list_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_list(object) == nullptr) {
    return false;
  }
  return bind_builtin_method_from_table(object, name, kListMethods, std::size(kListMethods), out);
}

} // namespace xlang3
