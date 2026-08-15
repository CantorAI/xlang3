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
#include "xlang3/set_object.h"

#include "xlang3/value_hash.h"

namespace xlang3 {

namespace {

template <typename T>
T* allocate_set_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  return obj;
}

bool append_unique(std::vector<Value>& items, const Value& value, std::string& error) {
  size_t ignored = 0;
  if (!value_hash_key(value, ignored, error)) {
    return false;
  }
  for (const auto& item : items) {
    if (value_key_equal(item, value)) {
      return true;
    }
  }
  items.push_back(value);
  return true;
}

Value make_set_iterator(Value source, uint64_t index) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_set_object<SetIteratorObject>(ObjectKind::SetIterator);
  obj->source = std::move(source);
  obj->index = index;
  v.as.obj = &obj->header;
  return v;
}

} // namespace

Value Value::set(std::vector<Value> items) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_set_object<SetObject>(ObjectKind::Set);
  for (const auto& item : items) {
    std::string error;
    append_unique(obj->items, item, error);
  }
  v.as.obj = &obj->header;
  return v;
}

void set_release_object(Object* object) {
  switch (object->kind) {
    case ObjectKind::Set:
      delete reinterpret_cast<SetObject*>(object);
      break;
    case ObjectKind::SetIterator:
      delete reinterpret_cast<SetIteratorObject*>(object);
      break;
    default:
      break;
  }
}

std::string set_to_string(const Value& value) {
  if (auto* set = value_as_set(value)) {
    std::string text = "{";
    for (size_t i = 0; i < set->items.size(); ++i) {
      if (i != 0) {
        text += ", ";
      }
      text += value_to_string(set->items[i]);
    }
    text += "}";
    return text;
  }
  if (value_as_set_iterator(value) != nullptr) {
    return "<set_iterator>";
  }
  return "<set>";
}

bool set_truthy(const Value& value) {
  if (auto* set = value_as_set(value)) {
    return !set->items.empty();
  }
  if (value_as_set_iterator(value) != nullptr) {
    return true;
  }
  return true;
}

bool set_get_iter(const Value& object, Value& out, std::string& error) {
  if (value_as_set(object) == nullptr) {
    error = "object is not a set";
    return false;
  }
  out = make_set_iterator(object, 0);
  return true;
}

bool set_iter_next(Value& iterator, bool& done, Value& out, std::string& error) {
  auto* it = value_as_set_iterator(iterator);
  if (it == nullptr) {
    error = "invalid set iterator";
    return false;
  }
  auto* set = value_as_set(it->source);
  if (set == nullptr) {
    error = "set iterator source is invalid";
    return false;
  }
  if (it->index >= set->items.size()) {
    done = true;
    out = Value::none();
    return true;
  }
  out = set->items[static_cast<size_t>(it->index)];
  ++it->index;
  done = false;
  return true;
}

bool set_len(const Value& value, Value& out, std::string& error) {
  auto* set = value_as_set(value);
  if (set == nullptr) {
    error = "object has no len()";
    return false;
  }
  out = Value::int64(static_cast<int64_t>(set->items.size()));
  return true;
}

bool set_add(Value& set, const Value& item, std::string& error) {
  auto* obj = value_as_set(set);
  if (obj == nullptr) {
    error = "set add target is not a set";
    return false;
  }
  return append_unique(obj->items, item, error);
}

} // namespace xlang3
