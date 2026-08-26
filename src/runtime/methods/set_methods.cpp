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

#include "xlang3/functional_iterators.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value_hash.h"

namespace xlang3 {

namespace {

bool set_add_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "set.add", error)) {
    return false;
  }
  Value set = args[0];
  if (!set_add(set, args[1], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool add_iterable_items(Runtime& runtime, Value& set, const Value& iterable, std::string& error) {
  Value iterator;
  if (!runtime_get_iter(runtime, iterable, iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    if (!set_add(set, item, error)) {
      return false;
    }
  }
}

bool set_contains_value(const SetObject& set, const Value& value) {
  for (const auto& item : set.items) {
    if (value_key_equal(item, value)) {
      return true;
    }
  }
  return false;
}

bool iterable_all_in_set(Runtime& runtime, const Value& iterable, const SetObject& set, bool& out, std::string& error) {
  Value iterator;
  if (!runtime_get_iter(runtime, iterable, iterator, error)) {
    return false;
  }
  out = true;
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    if (!set_contains_value(set, item)) {
      out = false;
      return true;
    }
  }
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

bool set_clear_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "set.clear", error)) {
    return false;
  }
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.clear target is not a set";
    return false;
  }
  set->items.clear();
  value_set_none(out);
  return true;
}

bool set_copy_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "set.copy", error)) {
    return false;
  }
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.copy target is not a set";
    return false;
  }
  out = Value::set(set->items);
  return true;
}

bool set_discard_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "set.discard", error)) {
    return false;
  }
  Value set = args[0];
  if (!remove_set_item(set, args[1], false, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool set_pop_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "set.pop", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.pop target is not a set";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (set->items.empty()) {
    error = "pop from an empty set";
    runtime.raise_class_error("KeyError", error);
    return false;
  }
  value_assign_fast(out, set->items.back());
  set->items.pop_back();
  return true;
}

bool set_remove_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "set.remove", error)) {
    return false;
  }
  Value set = args[0];
  if (!remove_set_item(set, args[1], true, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool set_update_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "set.update expected at least set self";
    return false;
  }
  Value set = args[0];
  if (value_as_set(set) == nullptr) {
    error = "set.update target is not a set";
    return false;
  }
  for (uint32_t i = 1; i < argc; ++i) {
    if (!add_iterable_items(runtime, set, args[i], error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool set_union_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.union target is not a set";
    return false;
  }
  out = Value::set(set->items);
  for (uint32_t i = 1; i < argc; ++i) {
    if (!add_iterable_items(runtime, out, args[i], error)) {
      return false;
    }
  }
  return true;
}

bool set_intersection_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.intersection target is not a set";
    return false;
  }
  out = Value::set(set->items);
  auto* result = value_as_set(out);
  for (uint32_t i = 1; i < argc; ++i) {
    Value iterator;
    if (!runtime_get_iter(runtime, args[i], iterator, error)) {
      return false;
    }
    std::vector<Value> keep;
    while (true) {
      bool done = false;
      Value item;
      if (!sequence_iter_next(iterator, done, item, error)) {
        return false;
      }
      if (done) {
        break;
      }
      if (set_contains_value(*result, item)) {
        keep.push_back(item);
      }
    }
    result->items.clear();
    for (const auto& item : keep) {
      if (!set_add(out, item, error)) {
        return false;
      }
    }
  }
  return true;
}

bool set_difference_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.difference target is not a set";
    return false;
  }
  out = Value::set(set->items);
  auto* result = value_as_set(out);
  for (uint32_t i = 1; i < argc; ++i) {
    Value iterator;
    if (!runtime_get_iter(runtime, args[i], iterator, error)) {
      return false;
    }
    while (true) {
      bool done = false;
      Value item;
      if (!sequence_iter_next(iterator, done, item, error)) {
        return false;
      }
      if (done) {
        break;
      }
      for (auto it = result->items.begin(); it != result->items.end(); ++it) {
        if (value_key_equal(*it, item)) {
          result->items.erase(it);
          break;
        }
      }
    }
  }
  return true;
}

bool set_symmetric_difference_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "set.symmetric_difference", error)) {
    return false;
  }
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.symmetric_difference target is not a set";
    return false;
  }
  out = Value::set(set->items);
  auto* result = value_as_set(out);
  Value iterator;
  if (!runtime_get_iter(runtime, args[1], iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    bool removed = false;
    for (auto it = result->items.begin(); it != result->items.end(); ++it) {
      if (value_key_equal(*it, item)) {
        result->items.erase(it);
        removed = true;
        break;
      }
    }
    if (!removed && !set_add(out, item, error)) {
      return false;
    }
  }
}

bool set_intersection_update_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  Value updated;
  if (!set_intersection_method(runtime, args, argc, updated, error, user_data)) {
    return false;
  }
  auto* target = value_as_set(args[0]);
  auto* source = value_as_set(updated);
  if (target == nullptr || source == nullptr) {
    error = "set.intersection_update target is not a set";
    return false;
  }
  target->items = source->items;
  value_set_none(out);
  return true;
}

bool set_difference_update_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  Value updated;
  if (!set_difference_method(runtime, args, argc, updated, error, user_data)) {
    return false;
  }
  auto* target = value_as_set(args[0]);
  auto* source = value_as_set(updated);
  if (target == nullptr || source == nullptr) {
    error = "set.difference_update target is not a set";
    return false;
  }
  target->items = source->items;
  value_set_none(out);
  return true;
}

bool set_symmetric_difference_update_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  Value updated;
  if (!set_symmetric_difference_method(runtime, args, argc, updated, error, user_data)) {
    return false;
  }
  auto* target = value_as_set(args[0]);
  auto* source = value_as_set(updated);
  if (target == nullptr || source == nullptr) {
    error = "set.symmetric_difference_update target is not a set";
    return false;
  }
  target->items = source->items;
  value_set_none(out);
  return true;
}

bool set_isdisjoint_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "set.isdisjoint", error)) {
    return false;
  }
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.isdisjoint target is not a set";
    return false;
  }
  Value iterator;
  if (!runtime_get_iter(runtime, args[1], iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      value_set_bool(out, true);
      return true;
    }
    if (set_contains_value(*set, item)) {
      value_set_bool(out, false);
      return true;
    }
  }
}

bool set_issubset_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "set.issubset", error)) {
    return false;
  }
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.issubset target is not a set";
    return false;
  }
  Value other_value = Value::set({});
  if (!add_iterable_items(runtime, other_value, args[1], error)) {
    return false;
  }
  auto* other = value_as_set(other_value);
  bool ok = false;
  if (!iterable_all_in_set(runtime, args[0], *other, ok, error)) {
    return false;
  }
  value_set_bool(out, ok);
  return true;
}

bool set_issuperset_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "set.issuperset", error)) {
    return false;
  }
  auto* set = value_as_set(args[0]);
  if (set == nullptr) {
    error = "set.issuperset target is not a set";
    return false;
  }
  bool ok = false;
  if (!iterable_all_in_set(runtime, args[1], *set, ok, error)) {
    return false;
  }
  value_set_bool(out, ok);
  return true;
}

} // namespace

bool set_get_method(const Value& object, const std::string& name, Value& out) {
  auto* set = value_as_set(object);
  if (set == nullptr) {
    return false;
  }
  if (set->frozen &&
      (name == "add" || name == "clear" || name == "difference_update" || name == "discard" ||
       name == "intersection_update" || name == "pop" || name == "remove" ||
       name == "symmetric_difference_update" || name == "update")) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"add", "set.add", set_add_method},
      {"clear", "set.clear", set_clear_method},
      {"copy", "set.copy", set_copy_method},
      {"difference", "set.difference", set_difference_method},
      {"difference_update", "set.difference_update", set_difference_update_method},
      {"discard", "set.discard", set_discard_method},
      {"intersection", "set.intersection", set_intersection_method},
      {"intersection_update", "set.intersection_update", set_intersection_update_method},
      {"isdisjoint", "set.isdisjoint", set_isdisjoint_method},
      {"issubset", "set.issubset", set_issubset_method},
      {"issuperset", "set.issuperset", set_issuperset_method},
      {"pop", "set.pop", set_pop_method},
      {"remove", "set.remove", set_remove_method},
      {"symmetric_difference", "set.symmetric_difference", set_symmetric_difference_method},
      {"symmetric_difference_update", "set.symmetric_difference_update", set_symmetric_difference_update_method},
      {"union", "set.union", set_union_method},
      {"update", "set.update", set_update_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
