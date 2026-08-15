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

#include "xlang3/sequence.h"

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

bool list_append_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
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

bool list_pop_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
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
  out = list->items[index];
  list->items.erase(list->items.begin() + static_cast<std::ptrdiff_t>(index));
  return true;
}

bool list_extend_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
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

bool list_insert_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
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

bool list_clear_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
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

} // namespace

bool list_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_list(object) == nullptr) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"append", "list.append", list_append_method},
      {"clear", "list.clear", list_clear_method},
      {"extend", "list.extend", list_extend_method},
      {"insert", "list.insert", list_insert_method},
      {"pop", "list.pop", list_pop_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
