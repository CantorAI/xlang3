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

#include "xlang3/mapping.h"
#include "xlang3/runtime.h"
#include "xlang3/value_hash.h"

namespace xlang3 {

namespace {

bool dict_get_method_impl(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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

bool dict_keys_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "dict.keys", error)) {
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.keys target is not a dict";
    return false;
  }
  out = mapping_keys_view(args[0]);
  return true;
}

bool dict_values_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "dict.values", error)) {
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.values target is not a dict";
    return false;
  }
  out = mapping_values_view(args[0]);
  return true;
}

bool dict_items_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "dict.items", error)) {
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.items target is not a dict";
    return false;
  }
  out = mapping_items_view(args[0]);
  return true;
}

bool dict_pop_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 && argc != 3) {
    error = "dict.pop expected 2 or 3 arguments, got " + std::to_string(argc);
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.pop target is not a dict";
    return false;
  }
  size_t ignored = 0;
  if (!value_hash_key(args[1], ignored, error)) {
    return false;
  }
  for (auto it = dict->entries.begin(); it != dict->entries.end(); ++it) {
    if (value_key_equal(it->first, args[1])) {
      value_assign_fast(out, it->second);
      dict->entries.erase(it);
      return true;
    }
  }
  if (argc == 3) {
    out = args[2];
    return true;
  }
  error = "key not found";
  return false;
}

bool dict_clear_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "dict.clear", error)) {
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.clear target is not a dict";
    return false;
  }
  dict->entries.clear();
  value_set_none(out);
  return true;
}

bool dict_copy_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "dict.copy", error)) {
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.copy target is not a dict";
    return false;
  }
  out = Value::dict(dict->entries);
  return true;
}

bool dict_popitem_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "dict.popitem", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.popitem target is not a dict";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (dict->entries.empty()) {
    error = "popitem(): dictionary is empty";
    runtime.raise_class_error("KeyError", error);
    return false;
  }
  auto entry = dict->entries.back();
  dict->entries.pop_back();
  out = Value::tuple({entry.first, entry.second});
  return true;
}

bool dict_setdefault_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 && argc != 3) {
    error = "dict.setdefault expected 2 or 3 arguments, got " + std::to_string(argc);
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.setdefault target is not a dict";
    return false;
  }
  Value target = args[0];
  if (mapping_get_item(target, args[1], out, error)) {
    return true;
  }
  error.clear();
  const Value& default_value = argc == 3 ? args[2] : Value::none();
  if (!mapping_set_item(target, args[1], default_value, error)) {
    return false;
  }
  value_assign_fast(out, default_value);
  return true;
}

bool dict_update_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "dict.update expected 1 or 2 arguments, got " + std::to_string(argc);
    return false;
  }
  auto* dict = value_as_dict(args[0]);
  if (dict == nullptr) {
    error = "dict.update target is not a dict";
    return false;
  }
  if (argc == 2) {
    auto* source = value_as_dict(args[1]);
    if (source == nullptr) {
      error = "dict.update source must be a dict";
      return false;
    }
    Value target = args[0];
    for (const auto& entry : source->entries) {
      if (!mapping_set_item(target, entry.first, entry.second, error)) {
        return false;
      }
    }
  }
  value_set_none(out);
  return true;
}

} // namespace

bool dict_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_dict(object) == nullptr) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"clear", "dict.clear", dict_clear_method},
      {"copy", "dict.copy", dict_copy_method},
      {"get", "dict.get", dict_get_method_impl},
      {"items", "dict.items", dict_items_method},
      {"keys", "dict.keys", dict_keys_method},
      {"pop", "dict.pop", dict_pop_method},
      {"popitem", "dict.popitem", dict_popitem_method},
      {"setdefault", "dict.setdefault", dict_setdefault_method},
      {"update", "dict.update", dict_update_method},
      {"values", "dict.values", dict_values_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
