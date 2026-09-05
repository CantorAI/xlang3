/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "value_graph_internal.h"

namespace xlang3::serialize::graph {
template<class Visit> void edges(Value& value, Visit visit) {
  if (auto* obj = value_as_list(value)) { for (auto& v : obj->items) visit(v); }
  else if (auto* obj = value_as_tuple(value)) { for (auto& v : obj->items) visit(v); }
  else if (auto* obj = value_as_dict(value)) { for (auto& v : obj->entries) { visit(v.first); visit(v.second); } }
  else if (auto* obj = value_as_cell(value)) visit(obj->value);
  else if (auto* obj = value_as_module(value)) { for (auto& v : obj->slots) visit(v); }
  else if (auto* obj = value_as_function(value)) {
    visit(obj->globals_module); visit(obj->annotations); visit(obj->doc); visit(obj->attrs_dict);
    for (auto& v : obj->closure) visit(v);
    for (auto& v : obj->defaults) visit(v);
    for (auto& v : obj->positional_defaults) visit(v);
    for (auto& v : obj->kwdefaults) visit(v.second);
  } else if (auto* obj = value_as_class(value)) {
    visit(obj->base); visit(obj->metaclass);
    for (auto& v : obj->bases) visit(v);
    for (auto& v : obj->attrs) visit(v.second);
    for (auto& v : obj->mro_cache) visit(v);
  } else if (auto* obj = value_as_instance(value)) {
    visit(obj->klass); visit(obj->mapping_storage); visit(obj->sequence_storage);
    for (uint32_t i = 0; i < obj->slot_count; ++i) visit(instance_slot_at(obj, i));
    for (auto& v : obj->attrs) visit(v.second);
  } else if (auto* obj = value_as_bound_method(value)) { visit(obj->self); visit(obj->function); }
  else if (auto* obj = value_as_static_method(value)) { visit(obj->function); visit(obj->attrs_dict); }
  else if (auto* obj = value_as_class_method(value)) { visit(obj->function); visit(obj->attrs_dict); }
  else if (auto* obj = value_as_slot_descriptor(value)) visit(obj->owner_class);
  else if (auto* obj = value_as_property(value)) {
    visit(obj->fget); visit(obj->fset); visit(obj->fdel); visit(obj->doc); visit(obj->name);
  }
}
void clear_edges(Value& value) {
  if (auto* instance = value_as_instance(value); instance && instance->native_data_cleanup) {
    auto cleanup = instance->native_data_cleanup;
    auto data = instance->native_owner;
    instance->native_data_cleanup = nullptr;
    instance->native_data = nullptr;
    instance->native_owner = nullptr;
    cleanup(data);
  }
  edges(value, [](Value& edge) { value_set_invalid(edge); });
}
}

namespace xlang3 {
void Runtime::retain_serialized_objects(std::vector<Value> objects) {
  serialized_objects_.reserve(serialized_objects_.size() + objects.size());
  for (auto& value : objects) serialized_objects_.push_back(std::move(value));
}
uint64_t Runtime::collect_serialized_objects(bool force) {
  const size_t count = serialized_objects_.size();
  if (!count) return 0;
  std::unordered_map<Object*, size_t> indices;
  for (size_t i = 0; i < count; ++i) indices.emplace(serialized_objects_[i].as.obj, i);
  std::vector<uint64_t> internal_refs(count, 1);
  std::vector<bool> live(count, false);
  std::vector<size_t> pending;
  if (!force) {
    for (auto& value : serialized_objects_) {
      serialize::graph::edges(value, [&](Value& edge) {
        if (edge.tag != ValueTag::Object || (edge.flags & kXlangValueBorrowedRefFlag)) return;
        auto it = indices.find(edge.as.obj);
        if (it != indices.end()) ++internal_refs[it->second];
      });
    }
    for (size_t i = 0; i < count; ++i) {
      if (serialized_objects_[i].as.obj->refcnt.load() > internal_refs[i]) {
        live[i] = true; pending.push_back(i);
      }
    }
    for (size_t i = 0; i < pending.size(); ++i) {
      serialize::graph::edges(serialized_objects_[pending[i]], [&](Value& edge) {
        if (edge.tag != ValueTag::Object) return;
        auto it = indices.find(edge.as.obj);
        if (it != indices.end() && !live[it->second]) { live[it->second] = true; pending.push_back(it->second); }
      });
    }
  }
  for (size_t i = 0; i < count; ++i) if (!live[i]) serialize::graph::clear_edges(serialized_objects_[i]);
  size_t keep = 0;
  for (size_t i = 0; i < count; ++i) {
    if (live[i]) {
      if (keep != i) serialized_objects_[keep] = std::move(serialized_objects_[i]);
      ++keep;
    }
  }
  serialized_objects_.resize(keep);
  return count - keep;
}
}
