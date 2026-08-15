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
#include "xlang3/sequence.h"

#include <cstdint>
#include <string>

namespace xlang3 {

namespace {

template <typename T>
T* allocate_sequence_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  return obj;
}

std::string repr_items(const std::vector<Value>& items, const char* open, const char* close) {
  std::string text = open;
  for (size_t i = 0; i < items.size(); ++i) {
    if (i != 0) {
      text += ", ";
    }
    text += value_to_string(items[i]);
  }
  text += close;
  return text;
}

} // namespace

Value Value::list(std::vector<Value> items) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_sequence_object<ListObject>(ObjectKind::List);
  obj->items = std::move(items);
  v.as.obj = &obj->header;
  return v;
}

Value Value::range(int64_t start, int64_t stop, int64_t step) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_sequence_object<RangeObject>(ObjectKind::Range);
  obj->start = start;
  obj->stop = stop;
  obj->step = step;
  v.as.obj = &obj->header;
  return v;
}

Value Value::range_iterator(int64_t current, int64_t stop, int64_t step) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_sequence_object<RangeIteratorObject>(ObjectKind::RangeIterator);
  obj->current = current;
  obj->stop = stop;
  obj->step = step;
  v.as.obj = &obj->header;
  return v;
}

ListObject* value_as_list(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::List) {
    return nullptr;
  }
  return reinterpret_cast<ListObject*>(value.as.obj);
}

RangeObject* value_as_range(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Range) {
    return nullptr;
  }
  return reinterpret_cast<RangeObject*>(value.as.obj);
}

RangeIteratorObject* value_as_range_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::RangeIterator) {
    return nullptr;
  }
  return reinterpret_cast<RangeIteratorObject*>(value.as.obj);
}

void sequence_release_object(Object* object) {
  switch (object->kind) {
    case ObjectKind::List:
      delete reinterpret_cast<ListObject*>(object);
      break;
    case ObjectKind::Range:
      delete reinterpret_cast<RangeObject*>(object);
      break;
    case ObjectKind::RangeIterator:
      delete reinterpret_cast<RangeIteratorObject*>(object);
      break;
    default:
      break;
  }
}

std::string sequence_to_string(const Value& value) {
  if (auto* list = value_as_list(value)) {
    return repr_items(list->items, "[", "]");
  }
  if (auto* range = value_as_range(value)) {
    if (range->start == 0 && range->step == 1) {
      return "range(" + std::to_string(range->stop) + ")";
    }
    return "range(" + std::to_string(range->start) + ", " +
           std::to_string(range->stop) + ", " + std::to_string(range->step) + ")";
  }
  if (value_as_range_iterator(value) != nullptr) {
    return "<range_iterator>";
  }
  return "<sequence>";
}

bool sequence_truthy(const Value& value) {
  if (auto* list = value_as_list(value)) {
    return !list->items.empty();
  }
  if (auto* range = value_as_range(value)) {
    return range->step > 0 ? range->start < range->stop : range->start > range->stop;
  }
  if (value_as_range_iterator(value) != nullptr) {
    return true;
  }
  return true;
}

bool sequence_get_iter(const Value& iterable, Value& out, std::string& error) {
  if (auto* range = value_as_range(iterable)) {
    out = Value::range_iterator(range->start, range->stop, range->step);
    return true;
  }
  error = "object is not iterable";
  return false;
}

bool sequence_iter_next(Value& iterator, bool& done, Value& out, std::string& error) {
  auto* range = value_as_range_iterator(iterator);
  if (range == nullptr) {
    error = "invalid iterator";
    return false;
  }
  done = range->step > 0 ? range->current >= range->stop : range->current <= range->stop;
  if (done) {
    out = Value::none();
    return true;
  }
  out = Value::int64(range->current);
  range->current += range->step;
  return true;
}

bool sequence_list_append(Value& list, const Value& item, std::string& error) {
  auto* obj = value_as_list(list);
  if (obj == nullptr) {
    error = "list append target is not a list";
    return false;
  }
  obj->items.push_back(item);
  return true;
}

bool sequence_len(const Value& value, Value& out, std::string& error) {
  if (auto* list = value_as_list(value)) {
    out = Value::int64(static_cast<int64_t>(list->items.size()));
    return true;
  }
  if (value.tag == ValueTag::Object && value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Tuple) {
    auto* tuple = reinterpret_cast<TupleObject*>(value.as.obj);
    out = Value::int64(static_cast<int64_t>(tuple->items.size()));
    return true;
  }
  if (value.tag == ValueTag::Object && value.as.obj != nullptr && value.as.obj->kind == ObjectKind::String) {
    auto* string = reinterpret_cast<StringObject*>(value.as.obj);
    out = Value::int64(static_cast<int64_t>(string->value.size()));
    return true;
  }
  error = "object has no len()";
  return false;
}

} // namespace xlang3
