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
#pragma once

#include "xlang3/compiler.h"
#include "xlang3/value.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

enum class DictIterationKind : uint8_t {
  Keys,
  Values,
  Items,
};

struct DictObject {
  Object header;
  std::vector<std::pair<Value, Value>> entries;
};

struct MappingProxyObject {
  Object header;
  Value source;
};

struct DictViewObject {
  Object header;
  Value source;
  DictIterationKind kind = DictIterationKind::Keys;
};

struct DictIteratorObject {
  Object header;
  Value source;
  uint64_t index = 0;
  DictIterationKind kind = DictIterationKind::Keys;
};

XLANG3_HOT_INLINE DictObject* value_as_dict(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Dict) {
    return nullptr;
  }
  return reinterpret_cast<DictObject*>(value.as.obj);
}

XLANG3_HOT_INLINE MappingProxyObject* value_as_mapping_proxy(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::MappingProxy) {
    return nullptr;
  }
  return reinterpret_cast<MappingProxyObject*>(value.as.obj);
}

XLANG3_HOT_INLINE DictIteratorObject* value_as_dict_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::DictIterator) {
    return nullptr;
  }
  return reinterpret_cast<DictIteratorObject*>(value.as.obj);
}

XLANG3_HOT_INLINE DictViewObject* value_as_dict_view(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
    return nullptr;
  }
  switch (value.as.obj->kind) {
    case ObjectKind::DictKeysView:
    case ObjectKind::DictValuesView:
    case ObjectKind::DictItemsView:
      return reinterpret_cast<DictViewObject*>(value.as.obj);
    default:
      return nullptr;
  }
}

Value mapping_keys_view(Value source);
Value mapping_values_view(Value source);
Value mapping_items_view(Value source);
Value mapping_proxy(Value source);

void mapping_release_object(Object* object);
std::string mapping_to_string(const Value& value);
bool mapping_truthy(const Value& value);
bool mapping_is_mapping(const Value& value);

bool mapping_get_item(const Value& object, const Value& key, Value& out, std::string& error);
bool mapping_set_item(Value& object, const Value& key, const Value& item, std::string& error);
bool mapping_delete_item(Value& object, const Value& key, std::string& error);
bool mapping_get_iter(const Value& object, Value& out, std::string& error);
bool mapping_iter_next(Value& iterator, bool& done, Value& out, std::string& error);
bool mapping_len(const Value& value, Value& out, std::string& error);
bool mapping_contains(const Value& container, const Value& item, bool& out, std::string& error);
bool mapping_clear(Value& value, std::string& error);
bool mapping_popitem(Value& value, Value& out, std::string& error);
Value mapping_copy(const Value& value);

} // namespace xlang3
