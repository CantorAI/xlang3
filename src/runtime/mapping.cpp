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
#include "xlang3/mapping.h"

#include "xlang3/value_hash.h"

namespace xlang3 {

namespace {

template <typename T>
T* allocate_mapping_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  return obj;
}

bool ensure_hashable(const Value& key, std::string& error) {
  size_t ignored = 0;
  return value_hash_key(key, ignored, error);
}

} // namespace

Value Value::dict(std::vector<std::pair<Value, Value>> entries) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_mapping_object<DictObject>(ObjectKind::Dict);
  for (auto& entry : entries) {
    std::string error;
    if (!ensure_hashable(entry.first, error)) {
      continue;
    }
    bool replaced = false;
    for (auto& existing : obj->entries) {
      if (value_key_equal(existing.first, entry.first)) {
        existing.second = std::move(entry.second);
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      obj->entries.push_back(std::move(entry));
    }
  }
  v.as.obj = &obj->header;
  return v;
}

static Value make_dict_iterator(Value source, uint64_t index) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_mapping_object<DictIteratorObject>(ObjectKind::DictIterator);
  obj->source = std::move(source);
  obj->index = index;
  v.as.obj = &obj->header;
  return v;
}

DictObject* value_as_dict(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Dict) {
    return nullptr;
  }
  return reinterpret_cast<DictObject*>(value.as.obj);
}

DictIteratorObject* value_as_dict_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::DictIterator) {
    return nullptr;
  }
  return reinterpret_cast<DictIteratorObject*>(value.as.obj);
}

void mapping_release_object(Object* object) {
  switch (object->kind) {
    case ObjectKind::Dict:
      delete reinterpret_cast<DictObject*>(object);
      break;
    case ObjectKind::DictIterator:
      delete reinterpret_cast<DictIteratorObject*>(object);
      break;
    default:
      break;
  }
}

std::string mapping_to_string(const Value& value) {
  if (auto* dict = value_as_dict(value)) {
    std::string text = "{";
    for (size_t i = 0; i < dict->entries.size(); ++i) {
      if (i != 0) {
        text += ", ";
      }
      text += value_to_string(dict->entries[i].first);
      text += ": ";
      text += value_to_string(dict->entries[i].second);
    }
    text += "}";
    return text;
  }
  if (value_as_dict_iterator(value) != nullptr) {
    return "<dict_keyiterator>";
  }
  return "<dict>";
}

bool mapping_truthy(const Value& value) {
  if (auto* dict = value_as_dict(value)) {
    return !dict->entries.empty();
  }
  if (value_as_dict_iterator(value) != nullptr) {
    return true;
  }
  return true;
}

bool mapping_get_item(const Value& object, const Value& key, Value& out, std::string& error) {
  auto* dict = value_as_dict(object);
  if (dict == nullptr) {
    error = "object is not a dict";
    return false;
  }
  if (!ensure_hashable(key, error)) {
    return false;
  }
  for (const auto& entry : dict->entries) {
    if (value_key_equal(entry.first, key)) {
      out = entry.second;
      return true;
    }
  }
  error = "key not found";
  return false;
}

bool mapping_set_item(Value& object, const Value& key, const Value& item, std::string& error) {
  auto* dict = value_as_dict(object);
  if (dict == nullptr) {
    error = "object does not support item assignment";
    return false;
  }
  if (!ensure_hashable(key, error)) {
    return false;
  }
  for (auto& entry : dict->entries) {
    if (value_key_equal(entry.first, key)) {
      entry.second = item;
      return true;
    }
  }
  dict->entries.push_back(std::make_pair(key, item));
  return true;
}

bool mapping_get_iter(const Value& object, Value& out, std::string& error) {
  if (value_as_dict(object) == nullptr) {
    error = "object is not a dict";
    return false;
  }
  out = make_dict_iterator(object, 0);
  return true;
}

bool mapping_iter_next(Value& iterator, bool& done, Value& out, std::string& error) {
  auto* it = value_as_dict_iterator(iterator);
  if (it == nullptr) {
    error = "invalid dict iterator";
    return false;
  }
  auto* dict = value_as_dict(it->source);
  if (dict == nullptr) {
    error = "dict iterator source is invalid";
    return false;
  }
  if (it->index >= dict->entries.size()) {
    done = true;
    out = Value::none();
    return true;
  }
  out = dict->entries[static_cast<size_t>(it->index)].first;
  ++it->index;
  done = false;
  return true;
}

bool mapping_len(const Value& value, Value& out, std::string& error) {
  auto* dict = value_as_dict(value);
  if (dict == nullptr) {
    error = "object has no len()";
    return false;
  }
  out = Value::int64(static_cast<int64_t>(dict->entries.size()));
  return true;
}

} // namespace xlang3
