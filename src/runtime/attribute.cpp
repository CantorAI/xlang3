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
#include "xlang3/attribute.h"

#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <cctype>

namespace xlang3 {

namespace {

bool check_argc(uint32_t argc, uint32_t expected, const char* name, std::string& error) {
  if (argc == expected) {
    return true;
  }
  error = std::string(name) + " expected " + std::to_string(expected) + " arguments, got " + std::to_string(argc);
  return false;
}

bool list_append_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!check_argc(argc, 2, "list.append", error)) {
    return false;
  }
  Value list = args[0];
  if (!sequence_list_append(list, args[1], error)) {
    return false;
  }
  out = Value::none();
  return true;
}

bool list_pop_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!check_argc(argc, 1, "list.pop", error)) {
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
  out = list->items.back();
  list->items.pop_back();
  return true;
}

bool dict_get_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (argc != 2 && argc != 3) {
    error = "dict.get expected 2 or 3 arguments, got " + std::to_string(argc);
    return false;
  }
  if (mapping_get_item(args[0], args[1], out, error)) {
    return true;
  }
  error.clear();
  out = argc == 3 ? args[2] : Value::none();
  return true;
}

bool dict_keys_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!check_argc(argc, 1, "dict.keys", error)) {
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.keys target is not a dict";
    return false;
  }
  std::vector<Value> keys;
  keys.reserve(dict->entries.size());
  for (const auto& entry : dict->entries) {
    keys.push_back(entry.first);
  }
  out = Value::list(std::move(keys));
  return true;
}

bool dict_values_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!check_argc(argc, 1, "dict.values", error)) {
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.values target is not a dict";
    return false;
  }
  std::vector<Value> values;
  values.reserve(dict->entries.size());
  for (const auto& entry : dict->entries) {
    values.push_back(entry.second);
  }
  out = Value::list(std::move(values));
  return true;
}

bool set_add_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!check_argc(argc, 2, "set.add", error)) {
    return false;
  }
  Value set = args[0];
  if (!set_add(set, args[1], error)) {
    return false;
  }
  out = Value::none();
  return true;
}

bool string_upper_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!check_argc(argc, 1, "str.upper", error)) {
    return false;
  }
  if (args[0].tag != ValueTag::Object || args[0].as.obj == nullptr || args[0].as.obj->kind != ObjectKind::String) {
    error = "str.upper target is not a string";
    return false;
  }
  auto* string = reinterpret_cast<StringObject*>(args[0].as.obj);
  std::string text = string->value;
  for (auto& ch : text) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  out = Value::string(std::move(text));
  return true;
}

bool string_lower_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!check_argc(argc, 1, "str.lower", error)) {
    return false;
  }
  if (args[0].tag != ValueTag::Object || args[0].as.obj == nullptr || args[0].as.obj->kind != ObjectKind::String) {
    error = "str.lower target is not a string";
    return false;
  }
  auto* string = reinterpret_cast<StringObject*>(args[0].as.obj);
  std::string text = string->value;
  for (auto& ch : text) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  out = Value::string(std::move(text));
  return true;
}

bool bind_builtin_method(
    const Value& object,
    const std::string& method_name,
    const std::string& full_name,
    NativeFunctionCallback callback,
    Value& out) {
  (void)method_name;
  out = Value::bound_method(object, Value::native_function(0, full_name, callback));
  return true;
}

bool get_builtin_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_list(object) != nullptr) {
    if (name == "append") return bind_builtin_method(object, name, "list.append", list_append_method, out);
    if (name == "pop") return bind_builtin_method(object, name, "list.pop", list_pop_method, out);
  }
  if (value_as_dict(object) != nullptr) {
    if (name == "get") return bind_builtin_method(object, name, "dict.get", dict_get_method, out);
    if (name == "keys") return bind_builtin_method(object, name, "dict.keys", dict_keys_method, out);
    if (name == "values") return bind_builtin_method(object, name, "dict.values", dict_values_method, out);
  }
  if (value_as_set(object) != nullptr) {
    if (name == "add") return bind_builtin_method(object, name, "set.add", set_add_method, out);
  }
  if (object.tag == ValueTag::Object && object.as.obj != nullptr && object.as.obj->kind == ObjectKind::String) {
    if (name == "upper") return bind_builtin_method(object, name, "str.upper", string_upper_method, out);
    if (name == "lower") return bind_builtin_method(object, name, "str.lower", string_lower_method, out);
  }
  return false;
}

} // namespace

bool attribute_get(const Value& object, const std::string& name, Value& out, std::string& error) {
  if (value_as_module(object) != nullptr) {
    return module_get_attr(object, name, out, error);
  }
  if (value_as_class(object) != nullptr || value_as_instance(object) != nullptr) {
    return object_get_attr(object, name, out, error);
  }
  if (get_builtin_method(object, name, out)) {
    return true;
  }
  error = "object has no attribute '" + name + "'";
  return false;
}

bool attribute_set(Value& object, const std::string& name, const Value& value, std::string& error) {
  if (value_as_module(object) != nullptr) {
    return module_set_attr(object, name, value, error);
  }
  return object_set_attr(object, name, value, error);
}

} // namespace xlang3
