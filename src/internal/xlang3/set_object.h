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

#include <string>
#include <vector>

namespace xlang3 {

struct SetObject {
  Object header;
  bool frozen = false;
  std::vector<Value> items;
};

struct SetIteratorObject {
  Object header;
  Value source;
  uint64_t index = 0;
};

XLANG3_HOT_INLINE SetObject* value_as_set(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Set) {
    return nullptr;
  }
  return reinterpret_cast<SetObject*>(value.as.obj);
}

XLANG3_HOT_INLINE SetIteratorObject* value_as_set_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::SetIterator) {
    return nullptr;
  }
  return reinterpret_cast<SetIteratorObject*>(value.as.obj);
}

void set_release_object(Object* object);
std::string set_to_string(const Value& value);
bool set_truthy(const Value& value);

bool set_get_iter(const Value& object, Value& out, std::string& error);
bool set_iter_next(Value& iterator, bool& done, Value& out, std::string& error);
bool set_len(const Value& value, Value& out, std::string& error);
bool set_add(Value& set, const Value& item, std::string& error);

} // namespace xlang3
