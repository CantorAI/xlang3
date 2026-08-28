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
#include "xlang3/object_model.h"

#include "xlang3/builtin_methods.h"
#include "xlang3/exceptions.h"
#include "xlang3/ir.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "xlang3/value.h"
#include "xlang3/value_hash.h"

#include <algorithm>
#include <string_view>

namespace xlang3 {

namespace {

template <typename T>
T* allocate_object_model(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(kind);
  return obj;
}

struct InstanceFreeList {
  ~InstanceFreeList() {
    for (auto* instance : items) {
      delete instance;
    }
  }

  std::vector<InstanceObject*> items;
};

thread_local InstanceFreeList instance_free_list;

InstanceObject* allocate_instance_object() {
  if (!instance_free_list.items.empty()) {
    auto* obj = instance_free_list.items.back();
    instance_free_list.items.pop_back();
    obj->header.kind = ObjectKind::Instance;
    obj->header.refcnt = 1;
    xlang_perf_count_object_alloc(ObjectKind::Instance);
    return obj;
  }
  return allocate_object_model<InstanceObject>(ObjectKind::Instance);
}

void recycle_instance_object(InstanceObject* instance) {
  if (instance->native_data_cleanup != nullptr && instance->native_data != nullptr) {
    instance->native_data_cleanup(instance->native_data);
  }
  instance->klass = Value::invalid();
  instance->mapping_storage = Value::invalid();
  instance->native_type.clear();
  instance->native_data = nullptr;
  instance->native_data_cleanup = nullptr;
  for (uint32_t i = 0; i < instance->slot_count && i < 8; ++i) {
    value_set_invalid(instance->inline_slots[i]);
  }
  instance->overflow_slots.clear();
  instance->attrs.clear();
  instance->slot_count = 0;
  if (instance_free_list.items.size() < 1024) {
    instance_free_list.items.push_back(instance);
    return;
  }
  delete instance;
}

Value class_value(const ClassObject* klass) {
  Value value;
  value.tag = ValueTag::Object;
  value.as.obj = const_cast<Object*>(&klass->header);
  retain(value);
  return value;
}

bool contains_class(const std::vector<const ClassObject*>& classes, const ClassObject* klass) {
  return std::find(classes.begin(), classes.end(), klass) != classes.end();
}

bool build_class_mro_classes(const ClassObject* klass, std::vector<const ClassObject*>& out, std::string& error);

bool class_mro_values(ClassObject* klass, const std::vector<Value>*& out, std::string& error);
bool class_lookup_attr(ClassObject* klass, const std::string& name, Value& out, std::string& error);

bool attr_truthy_marker(ClassObject* klass, const std::string& name) {
  Value marker;
  std::string error;
  return class_lookup_attr(klass, name, marker, error) && value_truthy(marker);
}

bool value_is_enum_auto_sentinel(const Value& value) {
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return false;
  }
  for (const auto& attr : instance->attrs) {
    if (attr.first == "__xlang3_enum_auto__" && value_truthy(attr.second)) {
      return true;
    }
  }
  return false;
}

bool enum_value_equal(const Value& left, const Value& right) {
  return value_key_equal(left, right);
}

bool enum_member_candidate(const std::string& name, const Value& value) {
  if (name.empty() || name[0] == '_' || name == "name" || name == "value") {
    return false;
  }
  if (value_as_function(value) != nullptr || value_as_native_function(value) != nullptr ||
      value_as_class(value) != nullptr || value_as_static_method(value) != nullptr ||
      value_as_class_method(value) != nullptr || value_as_property(value) != nullptr) {
    return false;
  }
  return true;
}

bool enum_value_map_lookup(const std::vector<std::pair<Value, Value>>& map, const Value& value, Value& out) {
  for (const auto& entry : map) {
    if (enum_value_equal(entry.first, value)) {
      value_assign_fast(out, entry.second);
      return true;
    }
  }
  return false;
}

bool finalize_enum_class(ClassObject& klass) {
  if (klass.attrs.find("__xlang3_enum_finalized__") != klass.attrs.end()) {
    return true;
  }
  if (klass.attrs.find("__xlang3_enum_marker__") != klass.attrs.end()) {
    return true;
  }
  if (!attr_truthy_marker(&klass, "__xlang3_enum_marker__")) {
    return true;
  }

  Value klass_value = class_value(&klass);
  std::vector<std::pair<Value, Value>> members;
  std::vector<std::pair<Value, Value>> value_members;
  std::vector<Value> member_names;
  std::vector<Value> member_values;
  int64_t next_auto_value = 1;

  std::vector<std::pair<std::string, Value>> candidates;
  candidates.reserve(klass.attrs.size());
  for (const auto& name : klass.definition_attr_order) {
    auto attr_it = klass.attrs.find(name);
    if (attr_it != klass.attrs.end() && enum_member_candidate(attr_it->first, attr_it->second)) {
      candidates.push_back(*attr_it);
    }
  }
  if (candidates.empty()) {
    for (const auto& attr : klass.attrs) {
      if (enum_member_candidate(attr.first, attr.second)) {
        candidates.push_back(attr);
      }
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
      return left.first < right.first;
    });
  }

  for (auto& candidate : candidates) {
    Value raw_value = candidate.second;
    if (value_is_enum_auto_sentinel(raw_value)) {
      raw_value = Value::int64(next_auto_value);
    }
    if (raw_value.tag == ValueTag::Int64 && raw_value.as.i64 >= next_auto_value) {
      next_auto_value = raw_value.as.i64 + 1;
    }

    Value existing;
    if (enum_value_map_lookup(value_members, raw_value, existing)) {
      klass.attrs[candidate.first] = existing;
      members.push_back({Value::string(candidate.first), existing});
      continue;
    }

    Value member = Value::instance(klass_value);
    auto* instance = value_as_instance(member);
    if (instance == nullptr) {
      return false;
    }
    instance->attrs.push_back({"name", Value::string(candidate.first)});
    instance->attrs.push_back({"value", raw_value});
    instance->attrs.push_back({"__xlang3_string_value__", Value::string(klass.name + "." + candidate.first)});
    klass.attrs[candidate.first] = member;
    members.push_back({Value::string(candidate.first), member});
    value_members.push_back({raw_value, member});
    member_names.push_back(Value::string(candidate.first));
    member_values.push_back(member);
  }

  klass.attrs["_member_map_"] = Value::dict(members);
  klass.attrs["__members__"] = klass.attrs["_member_map_"];
  klass.attrs["_value2member_map_"] = Value::dict(value_members);
  klass.attrs["_member_names_"] = Value::list(member_names);
  klass.attrs["_member_list_"] = Value::list(member_values);
  klass.attrs["__xlang3_enum_finalized__"] = Value::boolean(true);
  ++klass.version;
  return true;
}

bool class_mro_classes(ClassObject* klass, std::vector<const ClassObject*>& out, std::string& error) {
  const std::vector<Value>* values = nullptr;
  if (!class_mro_values(klass, values, error)) {
    return false;
  }
  for (const auto& value : *values) {
    auto* item = value_as_class(value);
    if (item == nullptr) {
      error = "invalid class in method resolution order";
      return false;
    }
    out.push_back(item);
  }
  return true;
}

bool build_class_mro_classes(const ClassObject* klass, std::vector<const ClassObject*>& out, std::string& error) {
  std::vector<std::vector<const ClassObject*>> sequences;
  sequences.reserve(klass->bases.size() + 1);

  std::vector<const ClassObject*> direct_bases;
  direct_bases.reserve(klass->bases.size());
  for (const auto& base : klass->bases) {
    auto* base_class = value_as_class(base);
    if (base_class == nullptr) {
      error = "base object is not a class";
      return false;
    }
    std::vector<const ClassObject*> base_mro;
    if (!class_mro_classes(base_class, base_mro, error)) {
      return false;
    }
    sequences.push_back(std::move(base_mro));
    direct_bases.push_back(base_class);
  }
  if (!direct_bases.empty()) {
    sequences.push_back(std::move(direct_bases));
  }

  out.push_back(klass);
  while (!sequences.empty()) {
    const ClassObject* candidate = nullptr;
    for (const auto& sequence : sequences) {
      if (sequence.empty()) {
        continue;
      }
      const ClassObject* head = sequence.front();
      bool in_tail = false;
      for (const auto& other : sequences) {
        for (size_t i = 1; i < other.size(); ++i) {
          if (other[i] == head) {
            in_tail = true;
            break;
          }
        }
        if (in_tail) break;
      }
      if (!in_tail) {
        candidate = head;
        break;
      }
    }
    if (candidate == nullptr) {
      error = "cannot create a consistent method resolution order";
      return false;
    }
    if (!contains_class(out, candidate)) {
      out.push_back(candidate);
    }
    for (auto& sequence : sequences) {
      if (!sequence.empty() && sequence.front() == candidate) {
        sequence.erase(sequence.begin());
      }
    }
    sequences.erase(
        std::remove_if(sequences.begin(), sequences.end(), [](const auto& sequence) { return sequence.empty(); }),
        sequences.end());
  }
  return true;
}

bool class_mro_values(ClassObject* klass, const std::vector<Value>*& out, std::string& error) {
  if (klass->mro_cache_version == klass->version && !klass->mro_cache.empty()) {
    out = &klass->mro_cache;
    return true;
  }

  std::vector<const ClassObject*> classes;
  if (!build_class_mro_classes(klass, classes, error)) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(classes.size());
  for (auto* item : classes) {
    values.push_back(class_value(item));
  }
  klass->mro_cache = std::move(values);
  klass->mro_cache_version = klass->version;
  out = &klass->mro_cache;
  return true;
}

bool choose_compatible_metaclass(Value& current, const Value& candidate, std::string& error) {
  auto* candidate_class = value_as_class(candidate);
  if (candidate_class == nullptr) {
    return true;
  }
  auto* current_class = value_as_class(current);
  if (current_class == nullptr || current_class->name == "type") {
    value_assign_fast(current, candidate);
    return true;
  }
  if (class_is_subclass(candidate_class, current_class)) {
    value_assign_fast(current, candidate);
    return true;
  }
  if (class_is_subclass(current_class, candidate_class)) {
    return true;
  }
  std::vector<const ClassObject*> current_mro;
  std::vector<const ClassObject*> candidate_mro;
  std::string ignored;
  if (class_mro_classes(current_class, current_mro, ignored) &&
      class_mro_classes(candidate_class, candidate_mro, ignored)) {
    for (const auto* current_base : current_mro) {
      if (current_base == nullptr || current_base->name == "type" || current_base->name == "object") {
        continue;
      }
      for (const auto* candidate_base : candidate_mro) {
        if (current_base == candidate_base) {
          current = class_value(current_base);
          return true;
        }
      }
    }
  }
  error = "metaclass conflict: the metaclass of a derived class must be a non-strict subclass of the metaclasses of all its bases";
  return false;
}

bool class_has_builtin_base_name_impl(ClassObject* klass, std::string_view name) {
  std::vector<const ClassObject*> mro;
  std::string ignored;
  if (!class_mro_classes(klass, mro, ignored)) {
    return false;
  }
  for (const auto* item : mro) {
    if (item != nullptr && item->name == name) {
      return true;
    }
  }
  return false;
}

bool class_lookup_attr(ClassObject* klass, const std::string& name, Value& out, std::string& error) {
  const std::vector<Value>* mro = nullptr;
  if (!class_mro_values(klass, mro, error)) {
    return false;
  }
  for (const auto& class_value : *mro) {
    auto* candidate = value_as_class(class_value);
    if (candidate == nullptr) {
      error = "invalid class in method resolution order";
      return false;
    }
    auto it = candidate->attrs.find(name);
    if (it != candidate->attrs.end()) {
      value_assign_fast(out, it->second);
      return true;
    }
  }
  return false;
}

bool bind_metaclass_attr_for_class_access(const Value& class_value, Value attr, Value& out) {
  if (auto* method = value_as_static_method(attr)) {
    value_assign_fast(out, method->function);
    return true;
  }
  if (auto* method = value_as_class_method(attr)) {
    Value function;
    value_assign_fast(function, method->function);
    out = Value::bound_method(class_value, std::move(function));
    return true;
  }
  if (value_as_function(attr) != nullptr || value_as_native_function(attr) != nullptr) {
    out = Value::bound_method(class_value, std::move(attr));
    return true;
  }
  value_assign_fast(out, attr);
  return true;
}

bool class_or_bases_have_descriptors(const ClassObject* klass) {
  if (klass->has_descriptors) {
    return true;
  }
  for (const auto& base : klass->bases) {
    auto* base_class = value_as_class(base);
    if (base_class != nullptr && class_or_bases_have_descriptors(base_class)) {
      return true;
    }
  }
  return false;
}

bool object_model_value_has_abstract_marker(const Value& value) {
  Value marker;
  std::string ignored;
  return object_get_attr(value, "__isabstractmethod__", marker, ignored) && value_truthy(marker);
}

void object_model_add_abstract_name(std::vector<Value>& names, const std::string& name) {
  for (const auto& item : names) {
    auto* string = value_as_string(item);
    if (string != nullptr && string_object_to_string(*string) == name) {
      return;
    }
  }
  names.push_back(Value::string(name));
}

void object_model_collect_abstract_names(const Value& value, std::vector<std::string>& names) {
  auto add_name = [&names](const Value& item) {
    auto* string = value_as_string(item);
    if (string == nullptr) {
      return;
    }
    const auto name = string_object_to_string(*string);
    if (std::find(names.begin(), names.end(), name) == names.end()) {
      names.push_back(name);
    }
  };
  if (auto* set = value_as_set(value)) {
    for (const auto& item : set->items) {
      add_name(item);
    }
  } else if (auto* tuple = value_as_tuple(value)) {
    for (const auto& item : tuple->items) {
      add_name(item);
    }
  } else if (auto* list = value_as_list(value)) {
    for (const auto& item : list->items) {
      add_name(item);
    }
  }
}

bool class_or_bases_use_abc_meta(ClassObject& klass) {
  auto* metaclass = value_as_class(klass.metaclass);
  if (metaclass != nullptr &&
      (metaclass->name == "ABCMeta" || class_has_builtin_base_name_impl(metaclass, "ABCMeta"))) {
    return true;
  }
  for (const auto& base : klass.bases) {
    Value ignored_value;
    std::string ignored_error;
    if (object_get_attr(base, "__abstractmethods__", ignored_value, ignored_error)) {
      return true;
    }
  }
  return false;
}

void update_abc_abstract_methods_for_class(ClassObject& klass) {
  if (!class_or_bases_use_abc_meta(klass)) {
    return;
  }
  std::vector<Value> abstracts;
  std::vector<std::string> inherited_names;
  for (const auto& base : klass.bases) {
    Value base_abstracts;
    std::string ignored;
    if (object_get_attr(base, "__abstractmethods__", base_abstracts, ignored)) {
      object_model_collect_abstract_names(base_abstracts, inherited_names);
    }
  }
  for (const auto& name : inherited_names) {
    auto override_it = klass.attrs.find(name);
    if (override_it == klass.attrs.end() || object_model_value_has_abstract_marker(override_it->second)) {
      object_model_add_abstract_name(abstracts, name);
    }
  }
  for (const auto& attr : klass.attrs) {
    if (object_model_value_has_abstract_marker(attr.second)) {
      object_model_add_abstract_name(abstracts, attr.first);
    }
  }
  klass.attrs["__abstractmethods__"] = Value::frozenset(std::move(abstracts));
}

void add_unique_slot_name(std::vector<std::string>& slots, const std::string& name) {
  if (name == "__weakref__") {
    return;
  }
  if (std::find(slots.begin(), slots.end(), name) == slots.end()) {
    slots.push_back(name);
  }
}

bool collect_slot_names_from_value(
    const Value& value,
    std::vector<std::string>& slots,
    bool& allow_instance_dict,
    bool& allow_weakref) {
  if (auto* string = value_as_string(value)) {
    const auto name = string_object_to_string(*string);
    if (name == "__dict__") {
      allow_instance_dict = true;
    } else if (name == "__weakref__") {
      allow_weakref = true;
    } else {
      add_unique_slot_name(slots, name);
    }
    return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    for (const auto& item : tuple->items) {
      if (!collect_slot_names_from_value(item, slots, allow_instance_dict, allow_weakref)) {
        return false;
      }
    }
    return true;
  }
  if (auto* list = value_as_list(value)) {
    for (const auto& item : list->items) {
      if (!collect_slot_names_from_value(item, slots, allow_instance_dict, allow_weakref)) {
        return false;
      }
    }
    return true;
  }
  if (auto* set = value_as_set(value)) {
    for (const auto& item : set->items) {
      if (!collect_slot_names_from_value(item, slots, allow_instance_dict, allow_weakref)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

void inherit_special_attr_flags(ClassObject& klass, const ClassObject& base) {
  klass.has_getattribute_hook = klass.has_getattribute_hook || base.has_getattribute_hook;
  klass.has_getattr_hook = klass.has_getattr_hook || base.has_getattr_hook;
  klass.has_setattr_hook = klass.has_setattr_hook || base.has_setattr_hook;
  klass.has_delattr_hook = klass.has_delattr_hook || base.has_delattr_hook;
}

void update_special_attr_flags(ClassObject& klass, const std::string& attr_name) {
  if (klass.name == "object") {
    return;
  }
  if (attr_name == "__getattribute__") {
    klass.has_getattribute_hook = true;
  } else if (attr_name == "__getattr__") {
    klass.has_getattr_hook = true;
  } else if (attr_name == "__setattr__") {
    klass.has_setattr_hook = true;
  } else if (attr_name == "__delattr__") {
    klass.has_delattr_hook = true;
  }
}

bool descriptor_lookup_method(const Value& value, const std::string& name) {
  if (value_as_static_method(value) != nullptr || value_as_class_method(value) != nullptr) {
    return name == "__get__";
  }
  if (value_as_property(value) != nullptr) {
    return name == "__get__" || name == "__set__" || name == "__delete__";
  }
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  if (klass == nullptr) {
    return false;
  }
  Value ignored;
  std::string error;
  return class_lookup_attr(klass, name, ignored, error);
}

Value module_globals_snapshot(const Value& module_value) {
  auto* module = value_as_module(module_value);
  if (module == nullptr) {
    return Value::dict({});
  }
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(module->name_to_slot.size() + 1);
  entries.push_back({Value::string("__name__"), Value::string(module->name)});
  for (const auto& item : module->name_to_slot) {
    if (item.first.empty() || item.first[0] == '#') {
      continue;
    }
    if (item.first == "__name__") {
      continue;
    }
    if (item.second >= module->slots.size()) {
      continue;
    }
    entries.push_back({Value::string(item.first), module->slots[item.second]});
  }
  return Value::dict(std::move(entries));
}

int64_t frame_source_line(const FrameObject& frame) {
  if (frame.module == nullptr || frame.function_id >= frame.module->functions.size()) {
    return static_cast<int64_t>(frame.instruction_index);
  }
  const auto& fn = frame.module->functions[frame.function_id];
  if (frame.instruction_index < fn.source_lines.size() && fn.source_lines[frame.instruction_index] != 0) {
    return static_cast<int64_t>(fn.source_lines[frame.instruction_index]);
  }
  return static_cast<int64_t>(frame.instruction_index);
}

bool slot_descriptor_get_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3) {
    error = "member_descriptor.__get__ expected object and optional owner";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* slot = value_as_slot_descriptor(args[0]);
  if (slot == nullptr) {
    error = "member_descriptor.__get__ expected descriptor self";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[1].tag == ValueTag::None) {
    value_assign_fast(out, args[0]);
    return true;
  }
  auto* instance = value_as_instance(args[1]);
  if (instance == nullptr || slot->index >= instance_slot_count(instance)) {
    if (instance != nullptr) {
      for (const auto& attr : instance->attrs) {
        if (attr.first == slot->name) {
          value_assign_fast(out, attr.second);
          return true;
        }
      }
    }
    Value tuple_value;
    std::string tuple_error;
    if (object_get_attr(args[1], "_tuple", tuple_value, tuple_error)) {
      if (auto* tuple = value_as_tuple(tuple_value); tuple != nullptr && slot->index < tuple->items.size()) {
        value_assign_fast(out, tuple->items[slot->index]);
        return true;
      }
    }
    error = "descriptor does not apply to this object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto& slot_value = instance_slot_at(instance, slot->index);
  if (slot_value.tag == ValueTag::Invalid) {
    error = "object has no attribute '" + slot->name + "'";
    runtime.raise_class_error("AttributeError", error);
    return false;
  }
  value_assign_fast(out, slot_value);
  return true;
}

bool slot_descriptor_set_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 3) {
    error = "member_descriptor.__set__ expected object and value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* slot = value_as_slot_descriptor(args[0]);
  auto* instance = value_as_instance(args[1]);
  if (slot == nullptr || instance == nullptr || slot->index >= instance_slot_count(instance)) {
    error = "descriptor does not apply to this object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(instance_slot_at(instance, slot->index), args[2]);
  value_set_none(out);
  return true;
}

bool slot_descriptor_delete_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "member_descriptor.__delete__ expected object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* slot = value_as_slot_descriptor(args[0]);
  auto* instance = value_as_instance(args[1]);
  if (slot == nullptr || instance == nullptr || slot->index >= instance_slot_count(instance)) {
    error = "descriptor does not apply to this object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_invalid(instance_slot_at(instance, slot->index));
  value_set_none(out);
  return true;
}

bool code_lines_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "code.co_lines expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* code = value_as_code(args[0]);
  if (code == nullptr || code->module == nullptr || code->function_id >= code->module->functions.size()) {
    error = "invalid code object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  const auto& fn = code->module->functions[code->function_id];
  std::vector<Value> ranges;
  ranges.reserve(fn.source_lines.size());
  for (size_t i = 0; i < fn.source_lines.size(); ++i) {
    const int64_t line = fn.source_lines[i] == 0 ? -1 : static_cast<int64_t>(fn.source_lines[i]);
    ranges.push_back(Value::tuple({
        Value::int64(static_cast<int64_t>(i)),
        Value::int64(static_cast<int64_t>(i + 1)),
        line < 0 ? Value::none() : Value::int64(line),
    }));
  }
  out = Value::tuple(std::move(ranges));
  return true;
}

bool code_positions_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "code.co_positions expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* code = value_as_code(args[0]);
  if (code == nullptr || code->module == nullptr || code->function_id >= code->module->functions.size()) {
    error = "invalid code object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  const auto& fn = code->module->functions[code->function_id];
  std::vector<Value> positions;
  positions.reserve(fn.source_lines.size());
  for (const auto line_value : fn.source_lines) {
    Value line = line_value == 0 ? Value::none() : Value::int64(static_cast<int64_t>(line_value));
    positions.push_back(Value::tuple({line, line, Value::none(), Value::none()}));
  }
  out = Value::tuple(std::move(positions));
  return true;
}

bool code_replace_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "code.replace expected keyword-only arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* code = value_as_code(args[0]);
  if (code == nullptr) {
    error = "code.replace expected code object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::code(code->module, code->function_id, code->mode);
  auto* replaced = value_as_code(out);
  if (replaced != nullptr) {
    replaced->filename_override = code->filename_override;
    replaced->first_line_override = code->first_line_override;
  }
  return true;
}

bool code_replace_method_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (!code_replace_method(runtime, args, argc, out, error, nullptr)) {
    return false;
  }
  auto* replaced = value_as_code(out);
  if (replaced == nullptr) {
    error = "code.replace failed";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    const Value& value = *kwargs[i].value;
    if (name == "co_filename") {
      auto* filename = value_as_string(value);
      if (filename == nullptr) {
        error = "co_filename must be str";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      replaced->filename_override = string_object_to_string(*filename);
    } else if (name == "co_firstlineno") {
      if (value.tag != ValueTag::Int64) {
        error = "co_firstlineno must be int";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      replaced->first_line_override = value.as.i64;
    } else if (
        name == "co_argcount" || name == "co_posonlyargcount" || name == "co_kwonlyargcount" ||
        name == "co_nlocals" || name == "co_stacksize" || name == "co_flags" ||
        name == "co_code" || name == "co_consts" || name == "co_names" ||
        name == "co_varnames" || name == "co_freevars" || name == "co_cellvars" ||
        name == "co_name" || name == "co_qualname" || name == "co_linetable" ||
        name == "co_exceptiontable") {
      continue;
    } else {
      error = "code.replace got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return true;
}

bool callable_metadata_attr(const Value& callable, const std::string& name, Value& out) {
  std::string ignored;
  if (object_get_attr(callable, name, out, ignored)) {
    return true;
  }
  if (name == "__annotations__") {
    out = Value::dict({});
    return true;
  }
  if (name == "__doc__" || name == "__module__") {
    value_set_none(out);
    return true;
  }
  return false;
}

bool callable_name_attr(const Value& callable, Value& out) {
  if (callable_metadata_attr(callable, "__name__", out)) {
    return true;
  }
  out = Value::string(value_to_string(callable));
  return true;
}

bool callable_qualname_attr(const Value& callable, Value& out) {
  if (callable_metadata_attr(callable, "__qualname__", out)) {
    return true;
  }
  return callable_name_attr(callable, out);
}

} // namespace

bool class_has_builtin_base_name(ClassObject* klass, std::string_view name) {
  return class_has_builtin_base_name_impl(klass, name);
}

bool class_try_enum_value_lookup(const Value& klass, const Value& value, Value& out) {
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr || !attr_truthy_marker(klass_obj, "__xlang3_enum_marker__")) {
    return false;
  }
  auto it = klass_obj->attrs.find("_value2member_map_");
  if (it == klass_obj->attrs.end()) {
    return false;
  }
  auto* dict = value_as_dict(it->second);
  if (dict == nullptr) {
    return false;
  }
  for (const auto& entry : dict->entries) {
    if (enum_value_equal(entry.first, value)) {
      value_assign_fast(out, entry.second);
      return true;
    }
  }
  return false;
}

Value Value::class_object(
    std::string name,
    std::vector<std::pair<std::string, Value>> attrs,
    Value base,
    std::vector<std::string> instance_slots,
    Value metaclass) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<ClassObject>(ObjectKind::Class);
  obj->name = std::move(name);
  obj->base = std::move(base);
  obj->metaclass = std::move(metaclass);
  if (obj->base.tag != ValueTag::Invalid) {
    obj->bases.push_back(obj->base);
    if (auto* base_class = value_as_class(obj->base)) {
      std::string ignored;
      (void)choose_compatible_metaclass(obj->metaclass, base_class->metaclass, ignored);
      obj->has_descriptors = obj->has_descriptors || class_or_bases_have_descriptors(base_class);
      inherit_special_attr_flags(*obj, *base_class);
      obj->instance_slot_names = base_class->instance_slot_names;
      obj->allow_weakref = base_class->allow_weakref;
    }
  }
  const size_t inherited_slot_count = obj->instance_slot_names.size();
  for (auto& attr : attrs) {
    if (object_value_is_descriptor(attr.second)) {
      obj->has_descriptors = true;
    }
    if (attr.first == "__slots__") {
      obj->restrict_instance_attrs = true;
      obj->allow_instance_dict = false;
      obj->allow_weakref = false;
      collect_slot_names_from_value(attr.second, obj->instance_slot_names, obj->allow_instance_dict, obj->allow_weakref);
    }
    update_special_attr_flags(*obj, attr.first);
    obj->attrs[std::move(attr.first)] = std::move(attr.second);
  }
  for (auto& slot : instance_slots) {
    if (!obj->restrict_instance_attrs ||
        std::find(obj->instance_slot_names.begin(), obj->instance_slot_names.end(), slot) !=
            obj->instance_slot_names.end()) {
      add_unique_slot_name(obj->instance_slot_names, slot);
    }
  }
  for (size_t i = 0; i < obj->instance_slot_names.size(); ++i) {
    obj->instance_slot_indices[obj->instance_slot_names[i]] = static_cast<uint32_t>(i);
  }
  for (size_t i = inherited_slot_count; i < obj->instance_slot_names.size(); ++i) {
    const auto& slot_name = obj->instance_slot_names[i];
    if (slot_name == "__dict__" || slot_name == "__weakref__" || obj->attrs.find(slot_name) != obj->attrs.end()) {
      continue;
    }
    obj->attrs.emplace(slot_name, slot_descriptor(obj->name, slot_name, static_cast<uint32_t>(i)));
    obj->has_descriptors = true;
  }
  v.as.obj = &obj->header;
  for (auto& attr : obj->attrs) {
    slot_descriptor_set_owner_class(attr.second, v);
  }
  return v;
}

Value Value::instance(Value klass) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_instance_object();
  obj->klass = klass;
  if (auto* klass_obj = value_as_class(obj->klass)) {
    obj->slot_count = static_cast<uint32_t>(klass_obj->instance_slot_names.size());
    if (obj->slot_count > 8) {
      obj->overflow_slots.assign(obj->slot_count, Value::invalid());
    }
    if (class_has_builtin_base_name(klass_obj, "dict") ||
        class_has_builtin_base_name(klass_obj, "OrderedDict") ||
        class_has_builtin_base_name(klass_obj, "defaultdict")) {
      obj->mapping_storage = Value::dict({});
    }
  }
  if (obj->slot_count == 0) {
    obj->attrs.reserve(4);
  }
  v.as.obj = &obj->header;
  return v;
}

Value Value::bound_method(Value self, Value function) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<BoundMethodObject>(ObjectKind::BoundMethod);
  obj->self = std::move(self);
  obj->function = std::move(function);
  v.as.obj = &obj->header;
  return v;
}

Value Value::static_method(Value function) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<StaticMethodObject>(ObjectKind::StaticMethod);
  obj->function = std::move(function);
  v.as.obj = &obj->header;
  return v;
}

Value Value::class_method(Value function) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<ClassMethodObject>(ObjectKind::ClassMethod);
  obj->function = std::move(function);
  v.as.obj = &obj->header;
  return v;
}

Value Value::super_object(Value klass, Value self) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<SuperObject>(ObjectKind::Super);
  obj->klass = std::move(klass);
  obj->self = std::move(self);
  v.as.obj = &obj->header;
  return v;
}

Value slot_descriptor(std::string owner_name, std::string name, uint32_t index) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<SlotDescriptorObject>(ObjectKind::SlotDescriptor);
  value_set_invalid(obj->owner_class);
  obj->owner_name = std::move(owner_name);
  obj->name = std::move(name);
  obj->index = index;
  v.as.obj = &obj->header;
  return v;
}

void slot_descriptor_set_owner_class(Value& descriptor, const Value& owner_class) {
  if (auto* slot = value_as_slot_descriptor(descriptor)) {
    value_assign_fast(slot->owner_class, owner_class);
  }
}

void object_model_release_object(Object* object) {
  switch (object->kind) {
    case ObjectKind::Class:
      delete reinterpret_cast<ClassObject*>(object);
      break;
    case ObjectKind::Instance:
      recycle_instance_object(reinterpret_cast<InstanceObject*>(object));
      break;
    case ObjectKind::BoundMethod:
      delete reinterpret_cast<BoundMethodObject*>(object);
      break;
    case ObjectKind::StaticMethod:
      delete reinterpret_cast<StaticMethodObject*>(object);
      break;
    case ObjectKind::ClassMethod:
      delete reinterpret_cast<ClassMethodObject*>(object);
      break;
    case ObjectKind::Super:
      delete reinterpret_cast<SuperObject*>(object);
      break;
    case ObjectKind::SlotDescriptor:
      delete reinterpret_cast<SlotDescriptorObject*>(object);
      break;
    default:
      break;
  }
}

std::string object_model_to_string(const Value& value) {
  if (auto* klass = value_as_class(value)) {
    return "<class '" + klass->name + "'>";
  }
  if (auto* instance = value_as_instance(value)) {
    if (auto* klass = value_as_class(instance->klass)) {
      if (is_exception_class_name(klass->name)) {
        for (const auto& attr : instance->attrs) {
          if (attr.first == "message") {
            return value_to_string(attr.second);
          }
        }
      }
      for (const auto& attr : instance->attrs) {
        if (attr.first == "__xlang3_string_value__" && value_as_string(attr.second) != nullptr) {
          return string_object_to_string(*value_as_string(attr.second));
        }
      }
      return "<" + klass->name + " object>";
    }
    return "<object>";
  }
  if (value_as_bound_method(value) != nullptr) {
    return "<bound method>";
  }
  if (value_as_static_method(value) != nullptr) {
    return "<staticmethod object>";
  }
  if (value_as_class_method(value) != nullptr) {
    return "<classmethod object>";
  }
  if (value_as_super(value) != nullptr) {
    return "<super object>";
  }
  if (auto* slot = value_as_slot_descriptor(value)) {
    return "<member '" + slot->name + "' of '" + slot->owner_name + "' objects>";
  }
  return "<object>";
}

bool object_get_attr(const Value& object, const std::string& name, Value& out, std::string& error) {
  if (auto* slot = value_as_slot_descriptor(object)) {
    if (name == "__name__") {
      out = Value::string(slot->name);
      return true;
    }
    if (name == "__objclass__") {
      if (slot->owner_class.tag != ValueTag::Invalid) {
        value_assign_fast(out, slot->owner_class);
      } else {
        out = Value::string(slot->owner_name);
      }
      return true;
    }
    if (name == "__module__") {
      out = Value::string("builtins");
      return true;
    }
    if (name == "__doc__") {
      value_set_none(out);
      return true;
    }
    if (name == "__get__") {
      out = Value::bound_method(object, Value::native_function(0, "member_descriptor.__get__", slot_descriptor_get_method));
      return true;
    }
    if (name == "__set__") {
      out = Value::bound_method(object, Value::native_function(0, "member_descriptor.__set__", slot_descriptor_set_method));
      return true;
    }
    if (name == "__delete__") {
      out = Value::bound_method(object, Value::native_function(0, "member_descriptor.__delete__", slot_descriptor_delete_method));
      return true;
    }
    error = "member descriptor has no attribute '" + name + "'";
    return false;
  }

  if (auto* type_param = value_as_type_param(object)) {
    if (name == "__name__") {
      out = Value::string(type_param->name);
      return true;
    }
    if (name == "__bound__") {
      value_assign_fast(out, type_param->bound);
      return true;
    }
    if (name == "__default__") {
      value_assign_fast(out, type_param->default_value);
      return true;
    }
    error = "type parameter has no attribute '" + name + "'";
    return false;
  }

  if (auto* view = value_as_memoryview(object)) {
    if (view->released) {
      error = "operation forbidden on released memoryview object";
      return false;
    }
    if (name == "readonly") {
      value_set_bool(out, view->readonly);
      return true;
    }
    if (name == "nbytes") {
      value_set_int64(out, static_cast<int64_t>(view->size));
      return true;
    }
    if (name == "itemsize") {
      value_set_int64(out, 1);
      return true;
    }
    if (name == "format") {
      out = Value::string(view->format);
      return true;
    }
    if (name == "ndim") {
      value_set_int64(out, 1);
      return true;
    }
    if (name == "shape") {
      out = Value::tuple({Value::int64(static_cast<int64_t>(view->size))});
      return true;
    }
    if (name == "strides") {
      out = Value::tuple({Value::int64(1)});
      return true;
    }
    if (name == "suboffsets") {
      value_set_none(out);
      return true;
    }
    if (name == "obj") {
      value_assign_fast(out, view->owner);
      return true;
    }
    if (name == "c_contiguous" || name == "f_contiguous" || name == "contiguous") {
      value_set_bool(out, true);
      return true;
    }
    error = "memoryview has no attribute '" + name + "'";
    return false;
  }

  if (auto* function = value_as_function(object)) {
    if (function->attrs_dict.tag != ValueTag::Invalid) {
      std::string ignored;
      if (mapping_get_item(function->attrs_dict, Value::string(name), out, ignored)) {
        return true;
      }
    }
    if (name == "__name__") {
      if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        out = Value::string(function->module->functions[function->function_id].name);
      } else {
        out = Value::string("<function>");
      }
      return true;
    }
    if (name == "__qualname__") {
      if (!function->qualname.empty()) {
        out = Value::string(function->qualname);
      } else if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        const auto& fn = function->module->functions[function->function_id];
        out = Value::string(fn.qualname.empty() ? fn.name : fn.qualname);
      } else {
        out = Value::string("<function>");
      }
      return true;
    }
    if (name == "__module__") {
      if (auto* module = value_as_module(function->globals_module)) {
        out = Value::string(module->name);
      } else {
        value_set_none(out);
      }
      return true;
    }
    if (name == "__doc__") {
      if (function->doc.tag == ValueTag::Invalid) {
        value_set_none(out);
      } else {
        value_assign_fast(out, function->doc);
      }
      return true;
    }
    if (name == "__defaults__") {
      if (function->positional_defaults.empty()) {
        value_set_none(out);
      } else {
        out = Value::tuple(function->positional_defaults);
      }
      return true;
    }
    if (name == "__kwdefaults__") {
      if (function->kwdefaults.empty()) {
        value_set_none(out);
      } else {
        std::vector<std::pair<Value, Value>> entries;
        entries.reserve(function->kwdefaults.size());
        for (const auto& entry : function->kwdefaults) {
          entries.push_back({Value::string(entry.first), entry.second});
        }
        out = Value::dict(std::move(entries));
      }
      return true;
    }
    if (name == "__type_params__") {
      std::vector<Value> values;
      values.reserve(function->type_params.size());
      for (const auto& type_param : function->type_params) {
        values.push_back(Value::type_param(type_param));
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "__annotations__") {
      if (function->annotations.tag == ValueTag::Invalid) {
        out = Value::dict({});
      } else {
        value_assign_fast(out, function->annotations);
      }
      return true;
    }
    if (name == "__code__") {
      if (function->module == nullptr || function->function_id >= function->module->functions.size()) {
        value_set_none(out);
      } else {
        out = Value::code(function->module, function->function_id);
      }
      return true;
    }
    if (name == "__globals__") {
      value_assign_fast(out, function->globals_module);
      return true;
    }
    if (name == "__closure__") {
      if (function->closure.empty()) {
        value_set_none(out);
      } else {
        out = Value::tuple(function->closure);
      }
      return true;
    }
    if (name == "__dict__") {
      if (function->attrs_dict.tag == ValueTag::Invalid) {
        function->attrs_dict = Value::dict({});
      }
      value_assign_fast(out, function->attrs_dict);
      return true;
    }
    error = "function has no attribute '" + name + "'";
    return false;
  }

  if (auto* code = value_as_code(object)) {
    if (code->module == nullptr || code->function_id >= code->module->functions.size()) {
      error = "invalid code object";
      return false;
    }
    const auto& fn = code->module->functions[code->function_id];
    if (name == "co_name") {
      out = Value::string(fn.name);
      return true;
    }
    if (name == "co_qualname") {
      out = Value::string(fn.qualname.empty() ? fn.name : fn.qualname);
      return true;
    }
    if (name == "co_filename") {
      out = Value::string(!code->filename_override.empty()
                              ? code->filename_override
                              : (code->module->source_file.empty() ? "<xlang3>" : code->module->source_file));
      return true;
    }
    if (name == "co_firstlineno") {
      out = Value::int64(code->first_line_override > 0 ? code->first_line_override : fn.first_line);
      return true;
    }
    if (name == "co_argcount") {
      uint32_t count = 0;
      if (!fn.signature.empty()) {
        for (const auto& param : fn.signature) {
          if (param.kind == ir::ParamKind::PosOnly || param.kind == ir::ParamKind::PosOrKeyword) {
            ++count;
          }
        }
      } else {
        count = static_cast<uint32_t>(fn.params.size());
      }
      out = Value::int64(count);
      return true;
    }
    if (name == "co_posonlyargcount") {
      uint32_t count = 0;
      for (const auto& param : fn.signature) {
        if (param.kind == ir::ParamKind::PosOnly) {
          ++count;
        }
      }
      out = Value::int64(count);
      return true;
    }
    if (name == "co_kwonlyargcount") {
      uint32_t count = 0;
      for (const auto& param : fn.signature) {
        if (param.kind == ir::ParamKind::KeywordOnly) {
          ++count;
        }
      }
      out = Value::int64(count);
      return true;
    }
    if (name == "co_nlocals") {
      out = Value::int64(static_cast<int64_t>(fn.locals.size()));
      return true;
    }
    if (name == "co_stacksize") {
      out = Value::int64(static_cast<int64_t>(fn.register_count));
      return true;
    }
    if (name == "co_flags") {
      int64_t flags = 0x01 | 0x02;
      for (const auto& param : fn.signature) {
        if (param.kind == ir::ParamKind::VarArgs) {
          flags |= 0x04;
        } else if (param.kind == ir::ParamKind::KwArgs) {
          flags |= 0x08;
        }
      }
      if (fn.is_generator && !fn.is_coroutine) {
        flags |= 0x20;
      }
      if (fn.is_coroutine) {
        flags |= 0x80;
      }
      if (fn.is_async && fn.is_generator && !fn.is_coroutine) {
        flags |= 0x200;
      }
      out = Value::int64(flags);
      return true;
    }
    if (name == "co_varnames") {
      std::vector<Value> values;
      values.reserve(fn.locals.size());
      for (const auto& local : fn.locals) {
        values.push_back(Value::string(local));
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "co_freevars") {
      std::vector<Value> values;
      values.reserve(fn.free_vars.size());
      for (const auto& item : fn.free_vars) {
        values.push_back(Value::string(item));
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "co_cellvars") {
      std::vector<Value> values;
      values.reserve(fn.cell_slots.size());
      for (const auto slot : fn.cell_slots) {
        if (slot < fn.locals.size()) {
          values.push_back(Value::string(fn.locals[slot]));
        }
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "co_names") {
      std::vector<Value> values;
      values.reserve(fn.names.size());
      for (const auto& item : fn.names) {
        values.push_back(Value::string(item));
      }
      out = Value::tuple(std::move(values));
      return true;
    }
    if (name == "co_consts") {
      out = Value::tuple(fn.constants);
      return true;
    }
    if (name == "co_code" || name == "co_linetable" || name == "co_exceptiontable") {
      out = Value::bytes({});
      return true;
    }
    if (name == "co_lines") {
      out = Value::bound_method(object, Value::native_function(0, "code.co_lines", code_lines_method));
      return true;
    }
    if (name == "co_positions") {
      out = Value::bound_method(object, Value::native_function(0, "code.co_positions", code_positions_method));
      return true;
    }
    if (name == "replace") {
      out = Value::bound_method(
          object,
          Value::native_function(0, "code.replace", code_replace_method, nullptr, nullptr, nullptr, false,
                                 code_replace_method_kw));
      return true;
    }
    error = "code has no attribute '" + name + "'";
    return false;
  }

  if (auto* frame = value_as_frame(object)) {
    if (name == "f_code") {
      if (frame->module == nullptr) {
        value_set_none(out);
      } else {
        out = Value::code(frame->module, frame->function_id);
      }
      return true;
    }
    if (name == "f_globals") {
      value_assign_fast(out, frame->globals_module);
      return true;
    }
    if (name == "f_builtins") {
      if (frame->builtins.tag == ValueTag::Invalid) {
        out = Value::dict({});
      } else {
        value_assign_fast(out, frame->builtins);
      }
      return true;
    }
    if (name == "f_back") {
      if (frame->back.tag == ValueTag::Invalid) {
        value_set_none(out);
      } else {
        value_assign_fast(out, frame->back);
      }
      return true;
    }
    if (name == "f_lineno") {
      out = Value::int64(frame_source_line(*frame));
      return true;
    }
    if (name == "f_lasti") {
      out = Value::int64(static_cast<int64_t>(frame->instruction_index));
      return true;
    }
    if (name == "f_locals") {
      if (frame->locals.tag == ValueTag::Invalid) {
        out = Value::dict({});
      } else {
        value_assign_fast(out, frame->locals);
      }
      return true;
    }
    if (name == "f_trace") {
      value_set_none(out);
      return true;
    }
    if (name == "f_trace_lines") {
      value_set_bool(out, true);
      return true;
    }
    if (name == "f_trace_opcodes") {
      value_set_bool(out, false);
      return true;
    }
    error = "frame has no attribute '" + name + "'";
    return false;
  }

  if (auto* traceback = value_as_traceback(object)) {
    if (name == "tb_frame") {
      value_assign_fast(out, traceback->frame);
      return true;
    }
    if (name == "tb_next") {
      value_assign_fast(out, traceback->next);
      return true;
    }
    if (name == "tb_lineno") {
      out = Value::int64(traceback->line);
      return true;
    }
    if (name == "tb_lasti") {
      if (auto* frame = value_as_frame(traceback->frame)) {
        out = Value::int64(static_cast<int64_t>(frame->instruction_index));
      } else {
        out = Value::int64(-1);
      }
      return true;
    }
    error = "traceback has no attribute '" + name + "'";
    return false;
  }

  if (auto* native = value_as_native_function(object)) {
    if (native->attrs_dict != nullptr && native->attrs_dict->tag != ValueTag::Invalid) {
      std::string ignored;
      if (mapping_get_item(*native->attrs_dict, Value::string(name), out, ignored)) {
        return true;
      }
    }
    if (name == "__name__") {
      out = Value::string(native->name);
      return true;
    }
    if (name == "__module__") {
      value_set_none(out);
      return true;
    }
    if (name == "__qualname__") {
      out = Value::string(native->name);
      return true;
    }
    if (name == "__doc__") {
      value_set_none(out);
      return true;
    }
    if (name == "__annotations__") {
      out = Value::dict({});
      return true;
    }
    if (name == "__defaults__" || name == "__kwdefaults__" || name == "__closure__" ||
        name == "__text_signature__") {
      value_set_none(out);
      return true;
    }
    if (name == "__dict__") {
      if (native->attrs_dict == nullptr) {
        native->attrs_dict = new Value(Value::dict({}));
      }
      value_assign_fast(out, *native->attrs_dict);
      return true;
    }
    error = "function has no attribute '" + name + "'";
    return false;
  }

  if (auto* bound = value_as_bound_method(object)) {
    if (name == "__self__") {
      value_assign_fast(out, bound->self);
      return true;
    }
    if (name == "__func__") {
      value_assign_fast(out, bound->function);
      return true;
    }
    if (name == "__name__") {
      return callable_name_attr(bound->function, out);
    }
    if (name == "__qualname__") {
      return callable_qualname_attr(bound->function, out);
    }
    if (name == "__module__" || name == "__doc__" || name == "__annotations__") {
      return callable_metadata_attr(bound->function, name, out);
    }
    error = "method has no attribute '" + name + "'";
    return false;
  }

  if (auto* method = value_as_static_method(object)) {
    if (method->attrs_dict.tag != ValueTag::Invalid) {
      std::string ignored;
      if (mapping_get_item(method->attrs_dict, Value::string(name), out, ignored)) {
        return true;
      }
    }
    if (name == "__func__") {
      value_assign_fast(out, method->function);
      return true;
    }
    if (name == "__wrapped__") {
      value_assign_fast(out, method->function);
      return true;
    }
    if (name == "__name__") {
      return callable_name_attr(method->function, out);
    }
    if (name == "__qualname__") {
      return callable_qualname_attr(method->function, out);
    }
    if (name == "__module__" || name == "__doc__" || name == "__annotations__") {
      return callable_metadata_attr(method->function, name, out);
    }
    if (name == "__dict__") {
      if (method->attrs_dict.tag == ValueTag::Invalid) {
        method->attrs_dict = Value::dict({});
      }
      value_assign_fast(out, method->attrs_dict);
      return true;
    }
    error = "staticmethod has no attribute '" + name + "'";
    return false;
  }

  if (auto* method = value_as_class_method(object)) {
    if (method->attrs_dict.tag != ValueTag::Invalid) {
      std::string ignored;
      if (mapping_get_item(method->attrs_dict, Value::string(name), out, ignored)) {
        return true;
      }
    }
    if (name == "__func__") {
      value_assign_fast(out, method->function);
      return true;
    }
    if (name == "__wrapped__") {
      value_assign_fast(out, method->function);
      return true;
    }
    if (name == "__name__") {
      return callable_name_attr(method->function, out);
    }
    if (name == "__qualname__") {
      return callable_qualname_attr(method->function, out);
    }
    if (name == "__module__" || name == "__doc__" || name == "__annotations__") {
      return callable_metadata_attr(method->function, name, out);
    }
    if (name == "__dict__") {
      if (method->attrs_dict.tag == ValueTag::Invalid) {
        method->attrs_dict = Value::dict({});
      }
      value_assign_fast(out, method->attrs_dict);
      return true;
    }
    error = "classmethod has no attribute '" + name + "'";
    return false;
  }

  if (auto* super = value_as_super(object)) {
    if (name == "__self__") {
      value_assign_fast(out, super->self);
      return true;
    }
    if (name == "__thisclass__") {
      value_assign_fast(out, super->klass);
      return true;
    }
    auto* klass = value_as_class(super->klass);
    if (klass == nullptr) {
      error = "super object has invalid class";
      return false;
    }
    const std::vector<Value>* mro = nullptr;
    if (!class_mro_values(klass, mro, error)) {
      return false;
    }
    bool use_next = false;
    Value attr;
    bool found = false;
    for (const auto& class_value : *mro) {
      auto* candidate = value_as_class(class_value);
      if (candidate == nullptr) {
        continue;
      }
      if (!use_next) {
        if (candidate == klass) {
          use_next = true;
        }
        continue;
      }
      auto it = candidate->attrs.find(name);
      if (it != candidate->attrs.end()) {
        value_assign_fast(attr, it->second);
        found = true;
        break;
      }
    }
    if (!found) {
      error = "super object has no attribute '" + name + "'";
      return false;
    }
    if (auto* method = value_as_static_method(attr)) {
      value_assign_fast(out, method->function);
    } else if (auto* method = value_as_class_method(attr)) {
      Value function;
      value_assign_fast(function, method->function);
      out = Value::bound_method(super->klass, std::move(function));
    } else if (value_as_function(attr) != nullptr || value_as_native_function(attr) != nullptr) {
      out = Value::bound_method(super->self, attr);
    } else {
      value_assign_fast(out, attr);
    }
    return true;
  }

  if (auto* klass = value_as_class(object)) {
    if (name == "__class__") {
      if (klass->metaclass.tag == ValueTag::Invalid) {
        error = "class has no metaclass";
        return false;
      }
      value_assign_fast(out, klass->metaclass);
      return true;
    }
    if (name == "__name__") {
      out = Value::string(klass->name);
      return true;
    }
    if (name == "__base__") {
      if (klass->base.tag == ValueTag::Invalid) {
        value_set_none(out);
      } else {
        value_assign_fast(out, klass->base);
      }
      return true;
    }
    if (name == "__bases__") {
      out = Value::tuple(klass->bases);
      return true;
    }
    if (name == "__mro__") {
      const std::vector<Value>* mro = nullptr;
      if (!class_mro_values(klass, mro, error)) {
        return false;
      }
      out = Value::tuple(*mro);
      return true;
    }
    if (name == "__dict__") {
      std::vector<std::pair<Value, Value>> entries;
      entries.reserve(klass->attrs.size());
      for (const auto& attr : klass->attrs) {
        entries.push_back({Value::string(attr.first), attr.second});
      }
      out = Value::dict(std::move(entries));
      return true;
    }
    if (!class_lookup_attr(klass, name, out, error)) {
      auto* metaclass = value_as_class(klass->metaclass);
      if (metaclass != nullptr) {
        Value meta_attr;
        std::string meta_error;
        if (class_lookup_attr(metaclass, name, meta_attr, meta_error)) {
          return bind_metaclass_attr_for_class_access(object, std::move(meta_attr), out);
        }
      }
      error = "class '" + klass->name + "' has no attribute '" + name + "'";
      return false;
    }
    if (auto* method = value_as_static_method(out)) {
      Value function;
      value_assign_fast(function, method->function);
      out = std::move(function);
      return true;
    }
    if (auto* method = value_as_class_method(out)) {
      Value function;
      value_assign_fast(function, method->function);
      out = Value::bound_method(object, std::move(function));
      return true;
    }
    return true;
  }

  if (auto* instance = value_as_instance(object)) {
    if (name == "__class__") {
      value_assign_fast(out, instance->klass);
      return true;
    }
    auto* klass = value_as_class(instance->klass);
    if (klass == nullptr) {
      error = "instance has invalid class";
      return false;
    }
    auto slot_it = klass->instance_slot_indices.find(name);
    if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
      const auto& slot_value = instance_slot_at(instance, slot_it->second);
      if (slot_value.tag != ValueTag::Invalid) {
        value_assign_fast(out, slot_value);
        return true;
      }
    }
    for (const auto& attr : instance->attrs) {
      if (attr.first == name) {
        value_assign_fast(out, attr.second);
        return true;
      }
    }
    Value class_attr;
    if (!class_lookup_attr(klass, name, class_attr, error)) {
      if (value_as_dict(instance->mapping_storage) != nullptr && dict_get_method(instance->mapping_storage, name, out)) {
        return true;
      }
      error = "object has no attribute '" + name + "'";
      return false;
    }
    if (auto* slot = value_as_slot_descriptor(class_attr)) {
      if (slot->index >= instance_slot_count(instance)) {
        error = "descriptor does not apply to this object";
        return false;
      }
      const auto& slot_value = instance_slot_at(instance, slot->index);
      if (slot_value.tag == ValueTag::Invalid) {
        error = "object has no attribute '" + slot->name + "'";
        return false;
      }
      value_assign_fast(out, slot_value);
      return true;
    }
    if (auto* method = value_as_static_method(class_attr)) {
      value_assign_fast(out, method->function);
    } else if (auto* method = value_as_class_method(class_attr)) {
      Value function;
      value_assign_fast(function, method->function);
      out = Value::bound_method(instance->klass, std::move(function));
    } else if (value_as_function(class_attr) != nullptr || value_as_native_function(class_attr) != nullptr) {
      out = Value::bound_method(object, class_attr);
    } else {
      value_assign_fast(out, class_attr);
    }
    return true;
  }

  error = "object has no attributes";
  return false;
}

bool object_set_attr(Value& object, const std::string& name, const Value& value, std::string& error) {
  if (auto* function = value_as_function(object)) {
    if (name == "__qualname__") {
      auto* string = value_as_string(value);
      if (string == nullptr) {
        error = "__qualname__ must be set to a string";
        return false;
      }
      function->qualname = string_object_to_string(*string);
      return true;
    }
    if (name == "__defaults__") {
      if (value.tag == ValueTag::None) {
        function->positional_defaults.clear();
        if (function->module != nullptr && function->function_id < function->module->functions.size()) {
          const auto& fn = function->module->functions[function->function_id];
          for (const auto& param : fn.signature) {
            if ((param.kind == ir::ParamKind::PosOnly || param.kind == ir::ParamKind::PosOrKeyword) &&
                param.default_reg != UINT32_MAX &&
                param.default_reg < function->defaults.size()) {
              value_set_invalid(function->defaults[param.default_reg]);
            }
          }
        }
        return true;
      }
      auto* tuple = value_as_tuple(value);
      if (tuple == nullptr) {
        error = "__defaults__ must be set to a tuple or None";
        return false;
      }
      function->positional_defaults.clear();
      function->positional_defaults.reserve(tuple->items.size());
      for (const auto& item : tuple->items) {
        function->positional_defaults.push_back(item);
      }
      if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        const auto& fn = function->module->functions[function->function_id];
        std::vector<uint32_t> default_param_regs;
        for (const auto& param : fn.signature) {
          if ((param.kind == ir::ParamKind::PosOnly || param.kind == ir::ParamKind::PosOrKeyword) &&
              param.default_reg != UINT32_MAX &&
              param.default_reg < function->defaults.size()) {
            default_param_regs.push_back(param.default_reg);
            value_set_invalid(function->defaults[param.default_reg]);
          }
        }
        const size_t copy_count = std::min(default_param_regs.size(), function->positional_defaults.size());
        const size_t param_start = default_param_regs.size() - copy_count;
        const size_t value_start = function->positional_defaults.size() - copy_count;
        for (size_t i = 0; i < copy_count; ++i) {
          value_assign_fast(function->defaults[default_param_regs[param_start + i]],
                            function->positional_defaults[value_start + i]);
        }
      }
      return true;
    }
    if (name == "__kwdefaults__") {
      if (value.tag == ValueTag::None) {
        function->kwdefaults.clear();
        if (function->module != nullptr && function->function_id < function->module->functions.size()) {
          const auto& fn = function->module->functions[function->function_id];
          for (const auto& param : fn.signature) {
            if (param.kind == ir::ParamKind::KeywordOnly &&
                param.default_reg != UINT32_MAX &&
                param.default_reg < function->defaults.size()) {
              value_set_invalid(function->defaults[param.default_reg]);
            }
          }
        }
        return true;
      }
      auto* dict = value_as_dict(value);
      if (dict == nullptr) {
        error = "__kwdefaults__ must be set to a dict or None";
        return false;
      }
      function->kwdefaults.clear();
      function->kwdefaults.reserve(dict->entries.size());
      for (const auto& entry : dict->entries) {
        auto* key = value_as_string(entry.first);
        if (key == nullptr) {
          error = "__kwdefaults__ keys must be strings";
          return false;
        }
        function->kwdefaults.push_back({string_object_to_string(*key), entry.second});
      }
      if (function->module != nullptr && function->function_id < function->module->functions.size()) {
        const auto& fn = function->module->functions[function->function_id];
        for (const auto& param : fn.signature) {
          if (param.kind != ir::ParamKind::KeywordOnly ||
              param.default_reg == UINT32_MAX ||
              param.default_reg >= function->defaults.size()) {
            continue;
          }
          value_set_invalid(function->defaults[param.default_reg]);
          for (const auto& item : function->kwdefaults) {
            if (item.first == param.name) {
              value_assign_fast(function->defaults[param.default_reg], item.second);
              break;
            }
          }
        }
      }
      return true;
    }
    if (name == "__annotations__") {
      value_assign_fast(function->annotations, value);
      return true;
    }
    if (name == "__doc__") {
      value_assign_fast(function->doc, value);
      return true;
    }
    if (function->attrs_dict.tag == ValueTag::Invalid) {
      function->attrs_dict = Value::dict({});
    }
    return mapping_set_item(function->attrs_dict, Value::string(name), value, error);
  }
  if (auto* native = value_as_native_function(object)) {
    if (name == "__doc__" || name == "__name__" || name == "__module__") {
      if (native->attrs_dict != nullptr && native->attrs_dict->tag != ValueTag::Invalid) {
        Value existing;
        std::string ignored;
        if (mapping_get_item(*native->attrs_dict, Value::string(name), existing, ignored)) {
          return mapping_set_item(*native->attrs_dict, Value::string(name), value, error);
        }
      }
      error = "native function attribute '" + name + "' is read-only";
      return false;
    }
    if (native->attrs_dict == nullptr) {
      native->attrs_dict = new Value(Value::dict({}));
    }
    return mapping_set_item(*native->attrs_dict, Value::string(name), value, error);
  }
  if (auto* method = value_as_static_method(object)) {
    if (method->attrs_dict.tag == ValueTag::Invalid) {
      method->attrs_dict = Value::dict({});
    }
    return mapping_set_item(method->attrs_dict, Value::string(name), value, error);
  }
  if (auto* method = value_as_class_method(object)) {
    if (method->attrs_dict.tag == ValueTag::Invalid) {
      method->attrs_dict = Value::dict({});
    }
    return mapping_set_item(method->attrs_dict, Value::string(name), value, error);
  }
  if (auto* frame = value_as_frame(object)) {
    if (name == "f_lineno") {
      if (value.tag != ValueTag::Int64) {
        error = "f_lineno must be an integer";
        return false;
      }
      if (frame->module != nullptr && frame->function_id < frame->module->functions.size()) {
        const auto& fn = frame->module->functions[frame->function_id];
        for (size_t i = 0; i < fn.source_lines.size(); ++i) {
          if (static_cast<int64_t>(fn.source_lines[i]) == value.as.i64) {
            frame->instruction_index = static_cast<uint32_t>(i);
            return true;
          }
        }
      }
      error = "line is not in current frame";
      return false;
    }
    if (name == "f_trace" || name == "f_trace_lines" || name == "f_trace_opcodes") {
      return true;
    }
    error = "frame attribute '" + name + "' is read-only";
    return false;
  }
  if (auto* traceback = value_as_traceback(object)) {
    if (name == "tb_next") {
      if (value.tag != ValueTag::None && value_as_traceback(value) == nullptr) {
        error = "tb_next must be a traceback or None";
        return false;
      }
      value_assign_fast(traceback->next, value);
      return true;
    }
    if (name == "tb_lineno") {
      if (value.tag != ValueTag::Int64) {
        error = "tb_lineno must be an integer";
        return false;
      }
      traceback->line = value.as.i64;
      return true;
    }
    error = "traceback attribute '" + name + "' is read-only";
    return false;
  }
  if (auto* instance = value_as_instance(object)) {
    auto* klass = value_as_class(instance->klass);
    if (klass != nullptr) {
      auto slot_it = klass->instance_slot_indices.find(name);
      if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
        value_assign_fast(instance_slot_at(instance, slot_it->second), value);
        return true;
      }
    }
    for (auto& attr : instance->attrs) {
      if (attr.first == name) {
        value_assign_fast(attr.second, value);
        return true;
      }
    }
    if (klass != nullptr && klass->restrict_instance_attrs && !klass->allow_instance_dict) {
      error = "object has no attribute '" + name + "'";
      return false;
    }
    instance->attrs.push_back(std::make_pair(name, value));
    return true;
  }
  if (auto* klass = value_as_class(object)) {
    klass->attrs[name] = value;
    if (object_value_is_descriptor(value)) {
      klass->has_descriptors = true;
    }
    update_special_attr_flags(*klass, name);
    ++klass->version;
    return true;
  }
  error = "object does not support attribute assignment";
  return false;
}

bool object_delete_attr(Value& object, const std::string& name, std::string& error) {
  if (auto* function = value_as_function(object)) {
    if (function->attrs_dict.tag != ValueTag::Invalid &&
        mapping_delete_item(function->attrs_dict, Value::string(name), error)) {
      return true;
    }
    error = "function has no attribute '" + name + "'";
    return false;
  }
  if (auto* native = value_as_native_function(object)) {
    if (native->attrs_dict != nullptr &&
        native->attrs_dict->tag != ValueTag::Invalid &&
        mapping_delete_item(*native->attrs_dict, Value::string(name), error)) {
      return true;
    }
    error = "function has no attribute '" + name + "'";
    return false;
  }
  if (auto* method = value_as_static_method(object)) {
    if (method->attrs_dict.tag != ValueTag::Invalid &&
        mapping_delete_item(method->attrs_dict, Value::string(name), error)) {
      return true;
    }
    error = "staticmethod has no attribute '" + name + "'";
    return false;
  }
  if (auto* method = value_as_class_method(object)) {
    if (method->attrs_dict.tag != ValueTag::Invalid &&
        mapping_delete_item(method->attrs_dict, Value::string(name), error)) {
      return true;
    }
    error = "classmethod has no attribute '" + name + "'";
    return false;
  }
  if (auto* instance = value_as_instance(object)) {
    auto* klass = value_as_class(instance->klass);
    if (klass != nullptr) {
      auto slot_it = klass->instance_slot_indices.find(name);
      if (slot_it != klass->instance_slot_indices.end() && slot_it->second < instance_slot_count(instance)) {
        value_set_invalid(instance_slot_at(instance, slot_it->second));
        return true;
      }
    }
    for (auto it = instance->attrs.begin(); it != instance->attrs.end(); ++it) {
      if (it->first == name) {
        instance->attrs.erase(it);
        return true;
      }
    }
    error = "object has no attribute '" + name + "'";
    return false;
  }
  if (auto* klass = value_as_class(object)) {
    auto it = klass->attrs.find(name);
    if (it == klass->attrs.end()) {
      error = "class '" + klass->name + "' has no attribute '" + name + "'";
      return false;
    }
    klass->attrs.erase(it);
    ++klass->version;
    return true;
  }
  error = "object does not support attribute deletion";
  return false;
}

bool object_construct(Value klass, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (value_as_class(klass) == nullptr) {
    error = "object is not a class";
    return false;
  }
  if (argc != 0) {
    error = "class construction with arguments requires __init__ dispatch";
    return false;
  }
  out = Value::instance(std::move(klass));
  return true;
}

bool class_set_base(Value klass, Value base, std::string& error) {
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr) {
    error = "object is not a class";
    return false;
  }
  if (base.tag != ValueTag::Invalid && value_as_class(base) == nullptr) {
    error = "base object is not a class";
    return false;
  }
  std::vector<std::string> own_slots;
  if (!klass_obj->has_explicit_bases) {
    own_slots = klass_obj->instance_slot_names;
    klass_obj->bases.clear();
    klass_obj->instance_slot_names.clear();
    klass_obj->instance_slot_indices.clear();
    klass_obj->has_explicit_bases = true;
  }
  klass_obj->bases.push_back(base);
  if (auto* base_class = value_as_class(base)) {
    if (!choose_compatible_metaclass(klass_obj->metaclass, base_class->metaclass, error)) {
      return false;
    }
    klass_obj->has_descriptors = klass_obj->has_descriptors || class_or_bases_have_descriptors(base_class);
    inherit_special_attr_flags(*klass_obj, *base_class);
    klass_obj->allow_weakref = klass_obj->allow_weakref || base_class->allow_weakref;
    for (const auto& slot : base_class->instance_slot_names) {
      if (std::find(klass_obj->instance_slot_names.begin(), klass_obj->instance_slot_names.end(), slot) ==
          klass_obj->instance_slot_names.end()) {
        klass_obj->instance_slot_names.push_back(slot);
      }
    }
  }
  for (auto& slot : own_slots) {
    if (std::find(klass_obj->instance_slot_names.begin(), klass_obj->instance_slot_names.end(), slot) ==
        klass_obj->instance_slot_names.end()) {
      klass_obj->instance_slot_names.push_back(slot);
    }
  }
  for (size_t i = 0; i < klass_obj->instance_slot_names.size(); ++i) {
    klass_obj->instance_slot_indices[klass_obj->instance_slot_names[i]] = static_cast<uint32_t>(i);
  }
  for (const auto& slot : own_slots) {
    auto index_it = klass_obj->instance_slot_indices.find(slot);
    if (index_it == klass_obj->instance_slot_indices.end()) {
      continue;
    }
    auto attr_it = klass_obj->attrs.find(slot);
    if (attr_it == klass_obj->attrs.end() || value_as_slot_descriptor(attr_it->second) != nullptr) {
      klass_obj->attrs[slot] = slot_descriptor(klass_obj->name, slot, index_it->second);
      klass_obj->has_descriptors = true;
    }
  }
  if (klass_obj->bases.size() == 1) {
    klass_obj->base = std::move(base);
  }
  update_abc_abstract_methods_for_class(*klass_obj);
  if (!finalize_enum_class(*klass_obj)) {
    error = "enum class finalization failed";
    return false;
  }
  ++klass_obj->version;
  return true;
}

bool class_is_subclass(const ClassObject* klass, const ClassObject* base) {
  std::vector<const ClassObject*> mro;
  std::string error;
  if (!class_mro_classes(const_cast<ClassObject*>(klass), mro, error)) {
    return false;
  }
  return contains_class(mro, base);
}

bool object_get_class_attr_for_instance(const Value& object, const std::string& name, Value& out, std::string& error) {
  auto* instance = value_as_instance(object);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  if (klass == nullptr) {
    error = "instance has invalid class";
    return false;
  }
  return class_lookup_attr(klass, name, out, error);
}

bool object_lookup_class_attr(const Value& klass, const std::string& name, Value& out, std::string& error) {
  auto* klass_obj = value_as_class(klass);
  if (klass_obj == nullptr) {
    error = "object is not a class";
    return false;
  }
  return class_lookup_attr(klass_obj, name, out, error);
}

bool object_value_has_descriptor_get(const Value& value) {
  if (value_as_slot_descriptor(value) != nullptr) {
    return true;
  }
  return descriptor_lookup_method(value, "__get__");
}

bool object_value_has_descriptor_set(const Value& value) {
  if (value_as_slot_descriptor(value) != nullptr) {
    return true;
  }
  return descriptor_lookup_method(value, "__set__");
}

bool object_value_has_descriptor_delete(const Value& value) {
  if (value_as_slot_descriptor(value) != nullptr) {
    return true;
  }
  return descriptor_lookup_method(value, "__delete__");
}

bool object_value_is_descriptor(const Value& value) {
  return object_value_has_descriptor_get(value) ||
         object_value_has_descriptor_set(value) ||
         object_value_has_descriptor_delete(value);
}

bool object_value_is_data_descriptor(const Value& value) {
  return value_as_property(value) != nullptr ||
         object_value_has_descriptor_set(value) ||
         object_value_has_descriptor_delete(value);
}

bool instance_set_native_data(
    Value instance,
    std::string native_type,
    void* native_data,
    void (*native_data_cleanup)(void*),
    std::string& error) {
  auto* instance_obj = value_as_instance(instance);
  if (instance_obj == nullptr) {
    error = "object is not an instance";
    return false;
  }
  if (instance_obj->native_data_cleanup != nullptr && instance_obj->native_data != nullptr) {
    instance_obj->native_data_cleanup(instance_obj->native_data);
  }
  instance_obj->native_type = std::move(native_type);
  instance_obj->native_data = native_data;
  instance_obj->native_data_cleanup = native_data_cleanup;
  return true;
}

void* instance_get_native_data(const Value& instance, const std::string& native_type) {
  auto* instance_obj = value_as_instance(instance);
  if (instance_obj == nullptr || instance_obj->native_type != native_type) {
    return nullptr;
  }
  return instance_obj->native_data;
}

} // namespace xlang3
