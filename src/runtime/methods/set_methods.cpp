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

#include "xlang3/set_object.h"
#include "xlang3/value_hash.h"

namespace xlang3 {

namespace {

bool set_add_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!method_check_argc(argc, 2, "set.add", error)) {
    return false;
  }
  Value set = args[0];
  if (!set_add(set, args[1], error)) {
    return false;
  }
  out = Value::none();
  return true;
}

bool remove_set_item(Value& set_value, const Value& item, bool require_present, std::string& error) {
  auto* set = value_as_set(set_value);
  if (set == nullptr) {
    error = "set method target is not a set";
    return false;
  }
  size_t ignored = 0;
  if (!value_hash_key(item, ignored, error)) {
    return false;
  }
  for (auto it = set->items.begin(); it != set->items.end(); ++it) {
    if (value_key_equal(*it, item)) {
      set->items.erase(it);
      return true;
    }
  }
  if (require_present) {
    error = "set item not found";
    return false;
  }
  return true;
}

bool set_discard_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!method_check_argc(argc, 2, "set.discard", error)) {
    return false;
  }
  Value set = args[0];
  if (!remove_set_item(set, args[1], false, error)) {
    return false;
  }
  out = Value::none();
  return true;
}

bool set_remove_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!method_check_argc(argc, 2, "set.remove", error)) {
    return false;
  }
  Value set = args[0];
  if (!remove_set_item(set, args[1], true, error)) {
    return false;
  }
  out = Value::none();
  return true;
}

} // namespace

bool set_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_set(object) == nullptr) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"add", "set.add", set_add_method},
      {"discard", "set.discard", set_discard_method},
      {"remove", "set.remove", set_remove_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
