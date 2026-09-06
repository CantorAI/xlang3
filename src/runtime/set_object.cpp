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

#include "xlang3/perf_counters.h"
#include "xlang3/value_hash.h"

#include <array>

namespace xlang3 {

namespace {

// Cache only dead objects, as the list/dict caches do. A live set or iterator
// may escape its allocating thread; it must not reside in a thread-owned slab.
template<class T> struct SetFreeList {
  std::array<T*, 256> items{};
  size_t count = 0;
  ~SetFreeList() { while (count) delete items[--count]; }
  T* Allocate() { return count ? items[--count] : new T(); }
  void Release(T* value) {
    if (count < items.size()) items[count++] = value;
    else delete value;
  }
};
thread_local SetFreeList<SetObject> set_free_list;
thread_local SetFreeList<SetIteratorObject> set_iterator_free_list;

SetObject* allocate_set_object() {
  auto* obj = set_free_list.Allocate();
  obj->header.kind = ObjectKind::Set;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(ObjectKind::Set);
  return obj;
}

SetIteratorObject* allocate_set_iterator_object() {
  auto* obj = set_iterator_free_list.Allocate();
  obj->header.kind = ObjectKind::SetIterator;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(ObjectKind::SetIterator);
  return obj;
}

void recycle_set_object(SetObject* object) {
  object->items.clear();
  object->frozen = false;
  set_free_list.Release(object);
}

void recycle_set_iterator_object(SetIteratorObject* object) {
  object->source = Value();
  object->index = 0;
  set_iterator_free_list.Release(object);
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
  auto* obj = allocate_set_iterator_object();
  obj->source = std::move(source);
  obj->index = index;
  v.as.obj = &obj->header;
  return v;
}

} // namespace

namespace {

Value make_set_value(std::vector<Value> items, bool frozen) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_set_object();
  obj->frozen = frozen;
  obj->items.reserve(items.size());
  for (const auto& item : items) {
    std::string error;
    append_unique(obj->items, item, error);
  }
  v.as.obj = &obj->header;
  return v;
}

} // namespace

Value Value::set(std::vector<Value> items) {
  return make_set_value(std::move(items), false);
}

Value Value::frozenset(std::vector<Value> items) {
  return make_set_value(std::move(items), true);
}

void set_release_object(Object* object) {
  switch (object->kind) {
    case ObjectKind::Set:
      recycle_set_object(reinterpret_cast<SetObject*>(object));
      break;
    case ObjectKind::SetIterator:
      recycle_set_iterator_object(reinterpret_cast<SetIteratorObject*>(object));
      break;
    default:
      break;
  }
}

std::string set_to_string(const Value& value) {
  if (auto* set = value_as_set(value)) {
    if (set->frozen) {
      std::string text = "frozenset({";
      for (size_t i = 0; i < set->items.size(); ++i) {
        if (i != 0) {
          text += ", ";
        }
        text += value_to_repr(set->items[i]);
      }
      text += "})";
      return text;
    }
    std::string text = "{";
    for (size_t i = 0; i < set->items.size(); ++i) {
      if (i != 0) {
        text += ", ";
      }
      text += value_to_repr(set->items[i]);
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
    value_set_none(out);
    return true;
  }
  value_assign_fast(out, set->items[static_cast<size_t>(it->index)]);
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
  value_set_int64(out, static_cast<int64_t>(set->items.size()));
  return true;
}

bool set_add(Value& set, const Value& item, std::string& error) {
  auto* obj = value_as_set(set);
  if (obj == nullptr) {
    error = "set add target is not a set";
    return false;
  }
  if (obj->frozen) {
    error = "frozenset is immutable";
    return false;
  }
  return append_unique(obj->items, item, error);
}

} // namespace xlang3
