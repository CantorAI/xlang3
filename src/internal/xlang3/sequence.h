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

struct ListObject {
  Object header;
  std::vector<Value> items;
};

struct RangeObject {
  Object header;
  int64_t start = 0;
  int64_t stop = 0;
  int64_t step = 1;
  bool int64_backed = true;
  Value start_value;
  Value stop_value;
  Value step_value;
};

struct RangeIteratorObject {
  Object header;
  int64_t current = 0;
  int64_t stop = 0;
  int64_t step = 1;
  bool int64_backed = true;
  Value current_value;
  Value stop_value;
  Value step_value;
};

struct SequenceIteratorObject {
  Object header;
  Value source;
  uint64_t index = 0;
};

XLANG3_HOT_INLINE ListObject* value_as_list(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::List) {
    return nullptr;
  }
  return reinterpret_cast<ListObject*>(value.as.obj);
}

XLANG3_HOT_INLINE RangeObject* value_as_range(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Range) {
    return nullptr;
  }
  return reinterpret_cast<RangeObject*>(value.as.obj);
}

XLANG3_HOT_INLINE RangeIteratorObject* value_as_range_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::RangeIterator) {
    return nullptr;
  }
  return reinterpret_cast<RangeIteratorObject*>(value.as.obj);
}

XLANG3_HOT_INLINE SequenceIteratorObject* value_as_sequence_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::SequenceIterator) {
    return nullptr;
  }
  return reinterpret_cast<SequenceIteratorObject*>(value.as.obj);
}

void sequence_release_object(Object* object);
std::string sequence_to_string(const Value& value);
bool sequence_truthy(const Value& value);

bool sequence_get_iter(const Value& iterable, Value& out, std::string& error);
bool sequence_iter_next(Value& iterator, bool& done, Value& out, std::string& error);
bool sequence_list_append(Value& list, const Value& item, std::string& error);
bool sequence_get_item(const Value& object, const Value& index, Value& out, std::string& error);
bool sequence_set_item(Value& object, const Value& index, const Value& item, std::string& error);
bool sequence_delete_item(Value& object, const Value& index, std::string& error);
bool sequence_len(const Value& value, Value& out, std::string& error);

} // namespace xlang3
