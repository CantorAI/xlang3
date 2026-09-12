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
#include "runtime/memory/object_cache_lifetime.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"
#include "xlang3/value_hash.h"

#include <algorithm>
#include <vector>

namespace xlang3 {

namespace {

struct DictObjectFreeList {
  ~DictObjectFreeList() {
    memory::object_caches_alive = false;
    for (auto* object : items) {
      delete object;
    }
  }

  std::vector<DictObject*> items;
};

struct DictIteratorObjectFreeList {
  ~DictIteratorObjectFreeList() {
    memory::object_caches_alive = false;
    for (auto* object : items) {
      delete object;
    }
  }

  std::vector<DictIteratorObject*> items;
};

struct DictViewObjectFreeList {
  ~DictViewObjectFreeList() {
    memory::object_caches_alive = false;
    for (auto* object : items) {
      delete object;
    }
  }

  std::vector<DictViewObject*> items;
};

struct MappingProxyObjectFreeList {
  ~MappingProxyObjectFreeList() {
    memory::object_caches_alive = false;
    for (auto* object : items) {
      delete object;
    }
  }

  std::vector<MappingProxyObject*> items;
};

thread_local DictObjectFreeList dict_object_free_list;
thread_local DictIteratorObjectFreeList dict_iterator_object_free_list;
thread_local DictViewObjectFreeList dict_view_object_free_list;
thread_local MappingProxyObjectFreeList mapping_proxy_object_free_list;

DictObject* allocate_dict_object() {
  xlang_perf_count_object_alloc(ObjectKind::Dict);
  if (memory::object_caches_alive && !dict_object_free_list.items.empty()) {
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
  if (memory::object_caches_alive && !dict_iterator_object_free_list.items.empty()) {
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
  if (memory::object_caches_alive && !dict_view_object_free_list.items.empty()) {
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

MappingProxyObject* allocate_mapping_proxy_object() {
  xlang_perf_count_object_alloc(ObjectKind::MappingProxy);
  if (memory::object_caches_alive && !mapping_proxy_object_free_list.items.empty()) {
    auto* obj = mapping_proxy_object_free_list.items.back();
    mapping_proxy_object_free_list.items.pop_back();
    obj->header.kind = ObjectKind::MappingProxy;
    obj->header.refcnt = 1;
    return obj;
  }
  auto* obj = new MappingProxyObject();
  obj->header.kind = ObjectKind::MappingProxy;
  obj->header.refcnt = 1;
  return obj;
}

void recycle_dict_object(DictObject* object) {
  for (auto& entry : object->entries) {
    value_set_invalid(entry.first);
    value_set_invalid(entry.second);
  }
  object->entries.clear();
  if (memory::object_caches_alive && dict_object_free_list.items.size() < 4096) {
    dict_object_free_list.items.push_back(object);
    return;
  }
  delete object;
}

void recycle_dict_iterator_object(DictIteratorObject* object) {
  value_set_invalid(object->source);
  object->kind = DictIterationKind::Keys;
  if (memory::object_caches_alive && dict_iterator_object_free_list.items.size() < 4096) {
    dict_iterator_object_free_list.items.push_back(object);
    return;
  }
  delete object;
}

void recycle_dict_view_object(DictViewObject* object) {
  value_set_invalid(object->source);
  object->kind = DictIterationKind::Keys;
  if (memory::object_caches_alive && dict_view_object_free_list.items.size() < 4096) {
    dict_view_object_free_list.items.push_back(object);
    return;
  }
  delete object;
}

void recycle_mapping_proxy_object(MappingProxyObject* object) {
  value_set_invalid(object->source);
  if (memory::object_caches_alive && mapping_proxy_object_free_list.items.size() < 4096) {
    mapping_proxy_object_free_list.items.push_back(object);
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

const Value* mapping_proxy_source(const Value& value) {
  auto* proxy = value_as_mapping_proxy(value);
  return proxy == nullptr ? nullptr : &proxy->source;
}

DictObject* dict_source_from_view_or_dict(const Value& value, DictIterationKind& kind) {
  if (const Value* source = mapping_proxy_source(value)) {
    return dict_source_from_view_or_dict(*source, kind);
  }
  if (auto* dict = dict_storage_from_value(value)) {
    kind = DictIterationKind::Keys;
    return dict;
  }
  if (auto* view = value_as_dict_view(value)) {
    kind = view->kind;
    if (const Value* source = mapping_proxy_source(view->source)) {
      return dict_storage_from_value(*source);
    }
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
  entries.reserve(module.name_to_slot.size() + module.extra_globals.size() + 1);
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
  entries.insert(entries.end(), module.extra_globals.begin(), module.extra_globals.end());
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
  const uint64_t extra_index = index - visible;
  if (index >= visible && extra_index < module.extra_globals.size()) {
    out = module.extra_globals[static_cast<size_t>(extra_index)];
    return true;
  }
  return false;
}

bool class_visible_name(const std::string& name) {
  return !name.empty() && name[0] != '#' && name != "__qualname__" &&
      name.rfind("__xlang3_abc_", 0) != 0;
}

std::vector<std::pair<Value, Value>> class_entries(const ClassObject& klass) {
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(klass.attrs.size());
  for (const auto& name : klass.definition_attr_order) {
    auto item = klass.attrs.find(name);
    if (item != klass.attrs.end() && class_visible_name(name)) {
      entries.push_back({Value::string(name), item->second});
    }
  }
  for (const auto& item : klass.attrs) {
    if (class_visible_name(item.first) &&
        std::find(klass.definition_attr_order.begin(), klass.definition_attr_order.end(), item.first) == klass.definition_attr_order.end()) {
      entries.push_back({Value::string(item.first), item.second});
    }
  }
  return entries;
}

bool class_entry_at(const ClassObject& klass, uint64_t index, std::pair<Value, Value>& out) {
  uint64_t visible = 0;
  for (const auto& name : klass.definition_attr_order) {
    auto item = klass.attrs.find(name);
    if (item == klass.attrs.end() || !class_visible_name(name)) continue;
    if (visible++ == index) {
      out = {Value::string(name), item->second};
      return true;
    }
  }
  for (const auto& item : klass.attrs) {
    if (!class_visible_name(item.first) ||
        std::find(klass.definition_attr_order.begin(), klass.definition_attr_order.end(), item.first) != klass.definition_attr_order.end()) {
      continue;
    }
    if (visible == index) {
      out = {Value::string(item.first), item.second};
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

Value mapping_proxy(Value source) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_mapping_proxy_object();
  obj->source = std::move(source);
  v.as.obj = &obj->header;
  return v;
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
    case ObjectKind::MappingProxy:
      recycle_mapping_proxy_object(reinterpret_cast<MappingProxyObject*>(object));
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
  if (auto* proxy = value_as_mapping_proxy(value)) {
    return "mappingproxy(" + value_to_repr(proxy->source) + ")";
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
  if (const Value* source = mapping_proxy_source(value)) {
    return mapping_truthy(*source);
  }
  return true;
}

bool mapping_is_mapping(const Value& value) {
  return dict_storage_from_value(value) != nullptr || value_as_module(value) != nullptr || value_as_mapping_proxy(value) != nullptr;
}

bool dict_integer_key(const Value& key, int64_t& out) {
  return value_int_like_to_i64(key, out);
}

void ensure_integer_index(const DictObject& dict) {
  if (dict.indexed_entry_count == dict.entries.size()) return;
  dict.integer_index.clear();
  dict.index_has_other_keys = false;
  for (size_t i = 0; i < dict.entries.size(); ++i) {
    int64_t numeric_key = 0;
    if (dict_integer_key(dict.entries[i].first, numeric_key)) {
      dict.integer_index.emplace(numeric_key, i);
    } else {
      dict.index_has_other_keys = true;
    }
  }
  dict.indexed_entry_count = dict.entries.size();
}

bool mapping_get_item(const Value& object, const Value& key, Value& out, std::string& error) {
  if (const Value* source = mapping_proxy_source(object)) {
    return mapping_get_item(*source, key, out, error);
  }
  auto* dict = dict_storage_from_value(object);
  if (!ensure_hashable(key, error)) {
    return false;
  }
  if (dict != nullptr) {
    int64_t numeric_key = 0;
    if (dict_integer_key(key, numeric_key)) {
      ensure_integer_index(*dict);
      if (const auto found = dict->integer_index.find(numeric_key); found != dict->integer_index.end()) {
        value_assign_fast(out, dict->entries[found->second].second);
        return true;
      }
      if (!dict->index_has_other_keys) {
        error = "key not found";
        return false;
      }
    }
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
    for (const auto& entry : module->extra_globals) {
      if (value_key_equal(entry.first, key)) {
        value_assign_fast(out, entry.second);
        return true;
      }
    }
    error = "key not found";
    return false;
  }
  if (auto* klass = value_as_class(object)) {
    if (auto* string = value_as_string(key)) {
      const auto name = string_object_to_string(*string);
      if (!class_visible_name(name)) {
        error = "key not found";
        return false;
      }
      auto it = klass->attrs.find(name);
      if (it != klass->attrs.end()) {
        value_assign_fast(out, it->second);
        return true;
      }
      error = "key not found";
      return false;
    }
    error = "class dictionary keys must be strings";
    return false;
  }
  error = "object is not a dict: " + value_to_repr(object);
  return false;
}

bool mapping_get_item_runtime(
    Runtime& runtime,
    const Value& object,
    const Value& key,
    Value& out,
    std::string& error) {
  auto* dict = dict_storage_from_value(object);
  if (dict == nullptr) {
    Value getitem;
    std::string attr_error;
    if (object_get_attr(object, "__getitem__", getitem, attr_error)) {
      return runtime_call_callable(runtime, getitem, &key, 1, out, error);
    }
    return mapping_get_item(object, key, out, error);
  }
  if (!ensure_hashable(key, error)) return false;
  const auto runtime_hash = [&](const Value& value, int64_t& hash) -> bool {
    if (value_as_instance(value) != nullptr) {
      Value method;
      std::string ignored;
      if (object_get_attr(value, "__hash__", method, ignored)) {
        if (method.tag == ValueTag::None) {
          error = "unhashable type";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
        Value result;
        if (!runtime_call_callable(runtime, method, nullptr, 0, result, error)) return false;
        if (result.tag != ValueTag::Int64) {
          error = "__hash__ method should return an integer";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
        hash = result.as.i64;
        return true;
      }
    }
    size_t raw = 0;
    if (!value_hash_key(value, raw, error)) return false;
    hash = static_cast<int64_t>(raw);
    return true;
  };
  int64_t key_hash = 0;
  if (!runtime_hash(key, key_hash)) return false;
  const auto is_weakref_key = [](const Value& value) {
    auto* instance = value_as_instance(value);
    auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
    return klass != nullptr && (klass->name == "ReferenceType" ||
        class_has_builtin_base_name(klass, "ReferenceType"));
  };
  for (size_t index = 0; index < dict->entries.size(); ++index) {
    Value candidate_key = dict->entries[index].first;
    Value candidate_value = dict->entries[index].second;
    if (value_is(candidate_key, key)) {
      value_assign_fast(out, candidate_value);
      return true;
    }
    int64_t candidate_hash = 0;
    if (!runtime_hash(candidate_key, candidate_hash)) return false;
    if (is_weakref_key(candidate_key) && is_weakref_key(key) &&
        candidate_hash != key_hash) continue;
    Value equal;
    if (!runtime_value_compare(runtime, "==", candidate_key, key, equal, error)) return false;
    bool is_equal = false;
    if (!runtime_truthy(runtime, equal, is_equal, error)) return false;
    if (is_equal) {
      value_assign_fast(out, candidate_value);
      return true;
    }
  }
  if (value_as_instance(object) != nullptr) {
    Value missing;
    std::string attr_error;
    if (object_get_attr(object, "__missing__", missing, attr_error)) {
      return runtime_call_callable(runtime, missing, &key, 1, out, error);
    }
  }
  error = "key not found";
  return false;
}

bool mapping_delete_item_runtime(Runtime& runtime, Value& object, const Value& key, std::string& error) {
  auto* dict = dict_storage_from_value(object);
  if (dict == nullptr) return mapping_delete_item(object, key, error);
  if (!ensure_hashable(key, error)) return false;
  const auto runtime_hash = [&](const Value& value, int64_t& hash) -> bool {
    if (value_as_instance(value) != nullptr) {
      Value method;
      std::string ignored;
      if (object_get_attr(value, "__hash__", method, ignored)) {
        if (method.tag == ValueTag::None) {
          error = "unhashable type";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
        Value result;
        if (!runtime_call_callable(runtime, method, nullptr, 0, result, error)) return false;
        if (result.tag != ValueTag::Int64) {
          error = "__hash__ method should return an integer";
          runtime.raise_class_error("TypeError", error);
          return false;
        }
        hash = result.as.i64;
        return true;
      }
    }
    size_t raw = 0;
    if (!value_hash_key(value, raw, error)) return false;
    hash = static_cast<int64_t>(raw);
    return true;
  };
  int64_t key_hash = 0;
  if (!runtime_hash(key, key_hash)) return false;
  const auto is_weakref_key = [](const Value& value) {
    auto* instance = value_as_instance(value);
    auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
    return klass != nullptr && (klass->name == "ReferenceType" ||
        class_has_builtin_base_name(klass, "ReferenceType"));
  };
  for (size_t index = 0; index < dict->entries.size(); ++index) {
    Value candidate_key = dict->entries[index].first;
    bool matches = value_is(candidate_key, key);
    if (!matches) {
      int64_t candidate_hash = 0;
      if (!runtime_hash(candidate_key, candidate_hash)) return false;
      if (is_weakref_key(candidate_key) && is_weakref_key(key) &&
          candidate_hash != key_hash) continue;
      Value equal;
      if (!runtime_value_compare(runtime, "==", candidate_key, key, equal, error)) return false;
      if (!runtime_truthy(runtime, equal, matches, error)) return false;
    }
    if (!matches) continue;
    for (auto current = dict->entries.begin(); current != dict->entries.end(); ++current) {
      if (value_is(current->first, candidate_key)) {
        dict->entries.erase(current);
        dict->indexed_entry_count = static_cast<size_t>(-1);
        return true;
      }
    }
    error = "key not found";
    return false;
  }
  error = "key not found";
  return false;
}

bool mapping_set_item(Value& object, const Value& key, const Value& item, std::string& error) {
  if (value_as_mapping_proxy(object) != nullptr) {
    error = "'mappingproxy' object does not support item assignment";
    return false;
  }
  auto* dict = dict_storage_from_value(object);
  if (!ensure_hashable(key, error)) {
    return false;
  }
  if (dict != nullptr) {
    int64_t numeric_key = 0;
    const bool indexed_numeric_key = dict_integer_key(key, numeric_key);
    if (indexed_numeric_key) {
      ensure_integer_index(*dict);
      if (const auto found = dict->integer_index.find(numeric_key); found != dict->integer_index.end()) {
        value_assign_fast(dict->entries[found->second].second, item);
        return true;
      }
      if (!dict->index_has_other_keys) {
        Value owned_key;
        Value owned_item;
        value_assign_fast(owned_key, key);
        value_assign_fast(owned_item, item);
        dict->entries.push_back(std::make_pair(std::move(owned_key), std::move(owned_item)));
        dict->integer_index.emplace(numeric_key, dict->entries.size() - 1);
        dict->indexed_entry_count = dict->entries.size();
        return true;
      }
    }
    for (auto& entry : dict->entries) {
      if (value_key_equal(entry.first, key)) {
        value_assign_fast(entry.second, item);
        return true;
      }
    }
    Value owned_key;
    Value owned_item;
    value_assign_fast(owned_key, key);
    value_assign_fast(owned_item, item);
    dict->entries.push_back(std::make_pair(std::move(owned_key), std::move(owned_item)));
    if (dict->indexed_entry_count + 1 == dict->entries.size()) {
      if (indexed_numeric_key) {
        dict->integer_index.emplace(numeric_key, dict->entries.size() - 1);
      } else {
        dict->index_has_other_keys = true;
      }
      dict->indexed_entry_count = dict->entries.size();
    }
    return true;
  }
  if (value_as_module(object) != nullptr) {
    auto* module = value_as_module(object);
    auto* string = value_as_string(key);
    if (string == nullptr) {
      for (auto& entry : module->extra_globals) {
        if (value_key_equal(entry.first, key)) {
          value_assign_fast(entry.second, item);
          ++module->version;
          return true;
        }
      }
      Value owned_key;
      Value owned_item;
      value_assign_fast(owned_key, key);
      value_assign_fast(owned_item, item);
      module->extra_globals.push_back({std::move(owned_key), std::move(owned_item)});
      ++module->version;
      return true;
    }
    return module_set_attr(object, string_object_to_string(*string), item, error);
  }
  error = "object does not support item assignment";
  return false;
}

bool mapping_delete_item(Value& object, const Value& key, std::string& error) {
  if (value_as_mapping_proxy(object) != nullptr) {
    error = "'mappingproxy' object does not support item deletion";
    return false;
  }
  auto* dict = dict_storage_from_value(object);
  if (!ensure_hashable(key, error)) {
    return false;
  }
  if (dict != nullptr) {
    for (auto it = dict->entries.begin(); it != dict->entries.end(); ++it) {
      if (value_key_equal(it->first, key)) {
        value_set_invalid(it->first);
        value_set_invalid(it->second);
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
      for (auto it = module->extra_globals.begin(); it != module->extra_globals.end(); ++it) {
        if (value_key_equal(it->first, key)) {
          module->extra_globals.erase(it);
          ++module->version;
          return true;
        }
      }
      error = "key not found";
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
    if (auto* property = value_as_property(module->slots[it->second]); property && property->native_module_runtime) {
      error = "native module property cannot be deleted: " + name;
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
  if (const Value* source = mapping_proxy_source(object)) {
    return mapping_get_iter(*source, out, error);
  }
  DictIterationKind kind = DictIterationKind::Keys;
  auto* view = value_as_dict_view(object);
  const Value* view_source = view == nullptr ? nullptr : &view->source;
  if (view_source != nullptr) {
    if (const Value* source = mapping_proxy_source(*view_source)) {
      view_source = source;
    }
  }
  if (dict_source_from_view_or_dict(object, kind) == nullptr &&
      value_as_module(object) == nullptr &&
      value_as_class(object) == nullptr &&
      (view_source == nullptr || (value_as_module(*view_source) == nullptr && value_as_class(*view_source) == nullptr))) {
    error = "object is not a dict: " + value_to_repr(object);
    return false;
  }
  if (view != nullptr) {
    if (view_source != nullptr) {
      out = make_dict_iterator(*view_source, 0, kind);
    } else {
      out = make_dict_iterator(view->source, 0, kind);
    }
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
  if (it->source.tag == ValueTag::Invalid) {
    done = true;
    value_set_none(out);
    return true;
  }
  if (const Value* source = mapping_proxy_source(it->source)) {
    value_assign_fast(it->source, *source);
  }
  auto* dict = dict_storage_from_value(it->source);
  auto* module = value_as_module(it->source);
  auto* klass = value_as_class(it->source);
  if (dict == nullptr && module == nullptr && klass == nullptr) {
    error = "dict iterator source is invalid";
    return false;
  }
  std::pair<Value, Value> entry;
  if (dict != nullptr) {
    if (it->index >= dict->entries.size()) {
      done = true;
      value_set_none(out);
      value_set_invalid(it->source);
      return true;
    }
    entry = dict->entries[static_cast<size_t>(it->index)];
  } else if (module != nullptr) {
    if (!module_entry_at(*module, it->index, entry)) {
      done = true;
      value_set_none(out);
      value_set_invalid(it->source);
      return true;
    }
  } else if (!class_entry_at(*klass, it->index, entry)) {
    done = true;
    value_set_none(out);
    value_set_invalid(it->source);
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
  if (const Value* source = mapping_proxy_source(value)) {
    return mapping_len(*source, out, error);
  }
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
  if (auto* klass = value_as_class(value)) {
    value_set_int64(out, static_cast<int64_t>(class_entries(*klass).size()));
    return true;
  }
  error = "object has no len()";
  return false;
}

bool mapping_contains(const Value& container, const Value& item, bool& out, std::string& error) {
  if (const Value* source = mapping_proxy_source(container)) {
    return mapping_contains(*source, item, out, error);
  }
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
    } else if (auto* klass = value_as_class(container)) {
      module_entries_storage = class_entries(*klass);
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
  if (value_as_mapping_proxy(value) != nullptr) {
    error = "'mappingproxy' object does not support clear";
    return false;
  }
  if (auto* dict = dict_storage_from_value(value)) {
    dict->entries.clear();
    return true;
  }
  if (auto* module = value_as_module(value)) {
    for (const auto& slot : module->slots) {
      auto* property = value_as_property(slot);
      if (property && property->native_module_runtime) {
        error = "cannot clear a module containing native properties";
        return false;
      }
    }
    for (auto& slot : module->slots) {
      value_set_invalid(slot);
    }
    module->name_to_slot.clear();
    module->extra_globals.clear();
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
  if (const Value* source = mapping_proxy_source(value)) {
    return mapping_copy(*source);
  }
  if (auto* dict = value_as_dict(value)) {
    return Value::dict(dict->entries);
  }
  if (auto* module = value_as_module(value)) {
    return Value::dict(module_entries(*module));
  }
  if (auto* klass = value_as_class(value)) {
    return Value::dict(class_entries(*klass));
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
