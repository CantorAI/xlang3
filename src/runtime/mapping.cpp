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

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/value_hash.h"

#include <algorithm>
#include <vector>

namespace xlang3 {

namespace {

struct DictObjectFreeList {
  ~DictObjectFreeList() {
    for (auto* object : items) {
      delete object;
    }
  }

  std::vector<DictObject*> items;
};

struct DictIteratorObjectFreeList {
  ~DictIteratorObjectFreeList() {
    for (auto* object : items) {
      delete object;
    }
  }

  std::vector<DictIteratorObject*> items;
};

struct DictViewObjectFreeList {
  ~DictViewObjectFreeList() {
    for (auto* object : items) {
      delete object;
    }
  }

  std::vector<DictViewObject*> items;
};

thread_local DictObjectFreeList dict_object_free_list;
thread_local DictIteratorObjectFreeList dict_iterator_object_free_list;
thread_local DictViewObjectFreeList dict_view_object_free_list;

DictObject* allocate_dict_object() {
  xlang_perf_count_object_alloc(ObjectKind::Dict);
  if (!dict_object_free_list.items.empty()) {
    auto* obj = dict_object_free_list.items.back();
    dict_object_free_list.items.pop_back();
    obj->header.kind = ObjectKind::Dict;
    obj->header.refcnt = 1;
    return obj;
  }
  auto* obj = new DictObject();
  obj->header.kind = ObjectKind::Dict;
  obj->header.refcnt = 1;
  return obj;
}

DictIteratorObject* allocate_dict_iterator_object() {
  xlang_perf_count_object_alloc(ObjectKind::DictIterator);
  if (!dict_iterator_object_free_list.items.empty()) {
    auto* obj = dict_iterator_object_free_list.items.back();
    dict_iterator_object_free_list.items.pop_back();
    obj->header.kind = ObjectKind::DictIterator;
    obj->header.refcnt = 1;
    return obj;
  }
  auto* obj = new DictIteratorObject();
  obj->header.kind = ObjectKind::DictIterator;
  obj->header.refcnt = 1;
  return obj;
}

DictViewObject* allocate_dict_view_object(ObjectKind kind) {
  xlang_perf_count_object_alloc(kind);
  if (!dict_view_object_free_list.items.empty()) {
    auto* obj = dict_view_object_free_list.items.back();
    dict_view_object_free_list.items.pop_back();
    obj->header.kind = kind;
    obj->header.refcnt = 1;
    return obj;
  }
  auto* obj = new DictViewObject();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  return obj;
}

void recycle_dict_object(DictObject* object) {
  object->entries.clear();
  if (dict_object_free_list.items.size() < 4096) {
    dict_object_free_list.items.push_back(object);
    return;
  }
  delete object;
}

void recycle_dict_iterator_object(DictIteratorObject* object) {
  value_set_invalid(object->source);
  object->kind = DictIterationKind::Keys;
  if (dict_iterator_object_free_list.items.size() < 4096) {
    dict_iterator_object_free_list.items.push_back(object);
    return;
  }
  delete object;
}

void recycle_dict_view_object(DictViewObject* object) {
  value_set_invalid(object->source);
  object->kind = DictIterationKind::Keys;
  if (dict_view_object_free_list.items.size() < 4096) {
    dict_view_object_free_list.items.push_back(object);
    return;
  }
  delete object;
}

bool ensure_hashable(const Value& key, std::string& error) {
  size_t ignored = 0;
  return value_hash_key(key, ignored, error);
}

ObjectKind dict_view_kind(DictIterationKind kind) {
  switch (kind) {
    case DictIterationKind::Keys:
      return ObjectKind::DictKeysView;
    case DictIterationKind::Values:
      return ObjectKind::DictValuesView;
    case DictIterationKind::Items:
      return ObjectKind::DictItemsView;
  }
  return ObjectKind::DictKeysView;
}

const char* dict_view_name(DictIterationKind kind) {
  switch (kind) {
    case DictIterationKind::Keys:
      return "dict_keys";
    case DictIterationKind::Values:
      return "dict_values";
    case DictIterationKind::Items:
      return "dict_items";
  }
  return "dict_keys";
}

DictObject* dict_storage_from_value(const Value& value) {
  if (auto* dict = value_as_dict(value)) {
    return dict;
  }
  if (auto* instance = value_as_instance(value)) {
    return value_as_dict(instance->mapping_storage);
  }
  return nullptr;
}

DictObject* dict_source_from_view_or_dict(const Value& value, DictIterationKind& kind) {
  if (auto* dict = dict_storage_from_value(value)) {
    kind = DictIterationKind::Keys;
    return dict;
  }
  if (auto* view = value_as_dict_view(value)) {
    kind = view->kind;
    return dict_storage_from_value(view->source);
  }
  return nullptr;
}

bool module_visible_name(const std::string& name) {
  return !name.empty() && name[0] != '#';
}

bool module_slot_visible(const ModuleObject& module, const std::string& name, uint32_t slot) {
  return module_visible_name(name) && slot < module.slots.size() && module.slots[slot].tag != ValueTag::Invalid;
}

std::vector<std::pair<Value, Value>> module_entries(const ModuleObject& module) {
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(module.name_to_slot.size() + 1);
  entries.push_back({Value::string("__name__"), Value::string(module.name)});
  std::vector<std::pair<std::string, uint32_t>> names;
  names.reserve(module.name_to_slot.size());
  for (const auto& item : module.name_to_slot) {
    if (item.first == "__name__" || !module_slot_visible(module, item.first, item.second)) {
      continue;
    }
    names.push_back(item);
  }
  std::sort(names.begin(), names.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.second < rhs.second;
  });
  for (const auto& item : names) {
    entries.push_back({Value::string(item.first), module.slots[item.second]});
  }
  return entries;
}

bool module_entry_at(const ModuleObject& module, uint64_t index, std::pair<Value, Value>& out) {
  if (index == 0) {
    out = {Value::string("__name__"), Value::string(module.name)};
    return true;
  }
  uint64_t visible = 1;
  std::vector<std::pair<std::string, uint32_t>> names;
  names.reserve(module.name_to_slot.size());
  for (const auto& item : module.name_to_slot) {
    if (item.first == "__name__" || !module_slot_visible(module, item.first, item.second)) {
      continue;
    }
    names.push_back(item);
  }
  std::sort(names.begin(), names.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.second < rhs.second;
  });
  for (const auto& item : names) {
    if (visible == index) {
      out = {Value::string(item.first), module.slots[item.second]};
      return true;
    }
    ++visible;
  }
  return false;
}

} // namespace

Value Value::dict(std::vector<std::pair<Value, Value>> entries) {
  Value v = Value::dict_reserved(entries.size());
  auto* obj = value_as_dict(v);
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

Value Value::dict_reserved(size_t capacity) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_dict_object();
  obj->entries.reserve(capacity);
  v.as.obj = &obj->header;
  return v;
}

static Value make_dict_view(Value source, DictIterationKind kind) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_dict_view_object(dict_view_kind(kind));
  obj->source = std::move(source);
  obj->kind = kind;
  v.as.obj = &obj->header;
  return v;
}

static Value make_dict_iterator(Value source, uint64_t index, DictIterationKind kind) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_dict_iterator_object();
  obj->source = std::move(source);
  obj->index = index;
  obj->kind = kind;
  v.as.obj = &obj->header;
  return v;
}

Value mapping_keys_view(Value source) {
  return make_dict_view(std::move(source), DictIterationKind::Keys);
}

Value mapping_values_view(Value source) {
  return make_dict_view(std::move(source), DictIterationKind::Values);
}

Value mapping_items_view(Value source) {
  return make_dict_view(std::move(source), DictIterationKind::Items);
}

void mapping_release_object(Object* object) {
  switch (object->kind) {
    case ObjectKind::Dict:
      recycle_dict_object(reinterpret_cast<DictObject*>(object));
      break;
    case ObjectKind::DictKeysView:
    case ObjectKind::DictValuesView:
    case ObjectKind::DictItemsView:
      recycle_dict_view_object(reinterpret_cast<DictViewObject*>(object));
      break;
    case ObjectKind::DictIterator:
      recycle_dict_iterator_object(reinterpret_cast<DictIteratorObject*>(object));
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
      text += value_to_repr(dict->entries[i].first);
      text += ": ";
      text += value_to_repr(dict->entries[i].second);
    }
    text += "}";
    return text;
  }
  if (value_as_dict_iterator(value) != nullptr) {
    return "<dict_keyiterator>";
  }
  if (auto* view = value_as_dict_view(value)) {
    auto* dict = value_as_dict(view->source);
    if (dict == nullptr) {
      return std::string(dict_view_name(view->kind)) + "([])";
    }
    std::string text = dict_view_name(view->kind);
    text += "([";
    for (size_t i = 0; i < dict->entries.size(); ++i) {
      if (i != 0) {
        text += ", ";
      }
      switch (view->kind) {
        case DictIterationKind::Keys:
          text += value_to_repr(dict->entries[i].first);
          break;
        case DictIterationKind::Values:
          text += value_to_repr(dict->entries[i].second);
          break;
        case DictIterationKind::Items:
          text += "(";
          text += value_to_repr(dict->entries[i].first);
          text += ", ";
          text += value_to_repr(dict->entries[i].second);
          text += ")";
          break;
      }
    }
    text += "])";
    return text;
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
  if (auto* view = value_as_dict_view(value)) {
    if (auto* dict = value_as_dict(view->source)) {
      return !dict->entries.empty();
    }
    if (auto* module = value_as_module(view->source)) {
      return !module_entries(*module).empty();
    }
    return false;
  }
  if (auto* module = value_as_module(value)) {
    return !module_entries(*module).empty();
  }
  return true;
}

bool mapping_is_mapping(const Value& value) {
  return dict_storage_from_value(value) != nullptr || value_as_dict_view(value) != nullptr || value_as_module(value) != nullptr;
}

bool mapping_get_item(const Value& object, const Value& key, Value& out, std::string& error) {
  auto* dict = value_as_dict(object);
  if (!ensure_hashable(key, error)) {
    return false;
  }
  if (dict != nullptr) {
    for (const auto& entry : dict->entries) {
      if (value_key_equal(entry.first, key)) {
        value_assign_fast(out, entry.second);
        return true;
      }
    }
    error = "key not found";
    return false;
  }
  if (auto* module = value_as_module(object)) {
    if (auto* string = value_as_string(key)) {
      const auto name = string_object_to_string(*string);
      Value module_value = object;
      if (module_get_attr(module_value, name, out, error) && out.tag != ValueTag::Invalid) {
        return true;
      }
      error = "key not found";
      return false;
    }
    error = "module globals keys must be strings";
    return false;
  }
  error = "object is not a dict";
  return false;
}

bool mapping_set_item(Value& object, const Value& key, const Value& item, std::string& error) {
  auto* dict = value_as_dict(object);
  if (!ensure_hashable(key, error)) {
    return false;
  }
  if (dict != nullptr) {
    for (auto& entry : dict->entries) {
      if (value_key_equal(entry.first, key)) {
        entry.second = item;
        return true;
      }
    }
    dict->entries.push_back(std::make_pair(key, item));
    return true;
  }
  if (value_as_module(object) != nullptr) {
    auto* string = value_as_string(key);
    if (string == nullptr) {
      error = "module globals keys must be strings";
      return false;
    }
    return module_set_attr(object, string_object_to_string(*string), item, error);
  }
  error = "object does not support item assignment";
  return false;
}

bool mapping_delete_item(Value& object, const Value& key, std::string& error) {
  auto* dict = value_as_dict(object);
  if (!ensure_hashable(key, error)) {
    return false;
  }
  if (dict != nullptr) {
    for (auto it = dict->entries.begin(); it != dict->entries.end(); ++it) {
      if (value_key_equal(it->first, key)) {
        dict->entries.erase(it);
        return true;
      }
    }
    error = "key not found";
    return false;
  }
  if (auto* module = value_as_module(object)) {
    auto* string = value_as_string(key);
    if (string == nullptr) {
      error = "module globals keys must be strings";
      return false;
    }
    const auto name = string_object_to_string(*string);
    auto it = module->name_to_slot.find(name);
    if (name == "__name__") {
      module->name.clear();
      if (it != module->name_to_slot.end() && it->second < module->slots.size()) {
        value_set_invalid(module->slots[it->second]);
      }
      module->name_to_slot.erase(name);
      ++module->version;
      return true;
    }
    if (it == module->name_to_slot.end() || it->second >= module->slots.size() ||
        module->slots[it->second].tag == ValueTag::Invalid) {
      error = "key not found";
      return false;
    }
    value_set_invalid(module->slots[it->second]);
    module->name_to_slot.erase(it);
    ++module->version;
    return true;
  }
  error = "object does not support item deletion";
  return false;
}

bool mapping_get_iter(const Value& object, Value& out, std::string& error) {
  DictIterationKind kind = DictIterationKind::Keys;
  auto* view = value_as_dict_view(object);
  if (dict_source_from_view_or_dict(object, kind) == nullptr &&
      value_as_module(object) == nullptr &&
      (view == nullptr || value_as_module(view->source) == nullptr)) {
    error = "object is not a dict";
    return false;
  }
  if (view != nullptr) {
    out = make_dict_iterator(view->source, 0, kind);
  } else {
    out = make_dict_iterator(object, 0, kind);
  }
  return true;
}

bool mapping_iter_next(Value& iterator, bool& done, Value& out, std::string& error) {
  auto* it = value_as_dict_iterator(iterator);
  if (it == nullptr) {
    error = "invalid dict iterator";
    return false;
  }
  auto* dict = dict_storage_from_value(it->source);
  auto* module = value_as_module(it->source);
  if (dict == nullptr && module == nullptr) {
    error = "dict iterator source is invalid";
    return false;
  }
  std::pair<Value, Value> entry;
  if (dict != nullptr) {
    if (it->index >= dict->entries.size()) {
      done = true;
      value_set_none(out);
      return true;
    }
    entry = dict->entries[static_cast<size_t>(it->index)];
  } else if (!module_entry_at(*module, it->index, entry)) {
    done = true;
    value_set_none(out);
    return true;
  }
  switch (it->kind) {
    case DictIterationKind::Keys:
      value_assign_fast(out, entry.first);
      break;
    case DictIterationKind::Values:
      value_assign_fast(out, entry.second);
      break;
    case DictIterationKind::Items:
      out = Value::tuple({entry.first, entry.second});
      break;
  }
  ++it->index;
  done = false;
  return true;
}

bool mapping_len(const Value& value, Value& out, std::string& error) {
  DictIterationKind kind = DictIterationKind::Keys;
  auto* dict = dict_source_from_view_or_dict(value, kind);
  if (dict != nullptr) {
    value_set_int64(out, static_cast<int64_t>(dict->entries.size()));
    return true;
  }
  if (auto* view = value_as_dict_view(value)) {
    if (auto* module = value_as_module(view->source)) {
      value_set_int64(out, static_cast<int64_t>(module_entries(*module).size()));
      return true;
    }
  }
  if (auto* module = value_as_module(value)) {
    value_set_int64(out, static_cast<int64_t>(module_entries(*module).size()));
    return true;
  }
  error = "object has no len()";
  return false;
}

bool mapping_contains(const Value& container, const Value& item, bool& out, std::string& error) {
  out = false;
  DictIterationKind kind = DictIterationKind::Keys;
  auto* dict = dict_source_from_view_or_dict(container, kind);
  std::vector<std::pair<Value, Value>> module_entries_storage;
  if (dict == nullptr) {
    ModuleObject* module = nullptr;
    if (auto* view = value_as_dict_view(container)) {
      module = value_as_module(view->source);
    } else {
      module = value_as_module(container);
    }
    if (module != nullptr) {
      module_entries_storage = module_entries(*module);
    } else {
      error = "object is not a dict view";
      return false;
    }
  }
  const auto entry_count = dict != nullptr ? dict->entries.size() : module_entries_storage.size();
  auto entry_at = [&](size_t index) -> const std::pair<Value, Value>& {
    return dict != nullptr ? dict->entries[index] : module_entries_storage[index];
  };
  if (dict == nullptr && module_entries_storage.empty()) {
    error = "object is not a dict view";
    return false;
  }
  if (kind == DictIterationKind::Items) {
    if (item.tag != ValueTag::Object || item.as.obj == nullptr || item.as.obj->kind != ObjectKind::Tuple) {
      return true;
    }
    auto* tuple = reinterpret_cast<TupleObject*>(item.as.obj);
    if (tuple->items.size() != 2) {
      return true;
    }
    for (size_t i = 0; i < entry_count; ++i) {
      const auto& entry = entry_at(i);
      if (value_key_equal(entry.first, tuple->items[0]) && value_key_equal(entry.second, tuple->items[1])) {
        out = true;
        return true;
      }
    }
    return true;
  }
  for (size_t i = 0; i < entry_count; ++i) {
    const auto& entry = entry_at(i);
    const Value& candidate = kind == DictIterationKind::Keys ? entry.first : entry.second;
    if (value_key_equal(candidate, item)) {
      out = true;
      return true;
    }
  }
  return true;
}

bool mapping_clear(Value& value, std::string& error) {
  if (auto* dict = value_as_dict(value)) {
    dict->entries.clear();
    return true;
  }
  if (auto* module = value_as_module(value)) {
    for (auto& slot : module->slots) {
      value_set_invalid(slot);
    }
    module->name_to_slot.clear();
    module->name.clear();
    ++module->version;
    return true;
  }
  error = "object does not support clear";
  return false;
}

bool mapping_popitem(Value& value, Value& out, std::string& error) {
  if (auto* dict = value_as_dict(value)) {
    if (dict->entries.empty()) {
      error = "popitem(): dictionary is empty";
      return false;
    }
    auto entry = dict->entries.back();
    dict->entries.pop_back();
    out = Value::tuple({entry.first, entry.second});
    return true;
  }
  if (auto* module = value_as_module(value)) {
    auto entries = module_entries(*module);
    if (entries.empty()) {
      error = "popitem(): dictionary is empty";
      return false;
    }
    auto entry = entries.back();
    if (!mapping_delete_item(value, entry.first, error)) {
      return false;
    }
    out = Value::tuple({entry.first, entry.second});
    return true;
  }
  error = "object does not support popitem";
  return false;
}

Value mapping_copy(const Value& value) {
  if (auto* dict = value_as_dict(value)) {
    return Value::dict(dict->entries);
  }
  if (auto* module = value_as_module(value)) {
    return Value::dict(module_entries(*module));
  }
  if (auto* view = value_as_dict_view(value)) {
    if (auto* dict = value_as_dict(view->source)) {
      return Value::dict(dict->entries);
    }
    if (auto* module = value_as_module(view->source)) {
      return Value::dict(module_entries(*module));
    }
  }
  return Value::dict({});
}

} // namespace xlang3
