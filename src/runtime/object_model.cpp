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

#include "xlang3/exceptions.h"
#include "xlang3/value.h"

#include <algorithm>

namespace xlang3 {

namespace {

template <typename T>
T* allocate_object_model(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
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
    return obj;
  }
  return allocate_object_model<InstanceObject>(ObjectKind::Instance);
}

void recycle_instance_object(InstanceObject* instance) {
  if (instance->native_data_cleanup != nullptr && instance->native_data != nullptr) {
    instance->native_data_cleanup(instance->native_data);
  }
  instance->klass = Value::invalid();
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

} // namespace

Value Value::class_object(
    std::string name,
    std::vector<std::pair<std::string, Value>> attrs,
    Value base,
    std::vector<std::string> instance_slots) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<ClassObject>(ObjectKind::Class);
  obj->name = std::move(name);
  obj->base = std::move(base);
  if (obj->base.tag != ValueTag::Invalid) {
    obj->bases.push_back(obj->base);
    if (auto* base_class = value_as_class(obj->base)) {
      obj->has_descriptors = obj->has_descriptors || class_or_bases_have_descriptors(base_class);
    }
  }
  for (auto& attr : attrs) {
    if (value_as_property(attr.second) != nullptr) {
      obj->has_descriptors = true;
    }
    obj->attrs[std::move(attr.first)] = std::move(attr.second);
  }
  obj->instance_slot_names = std::move(instance_slots);
  for (size_t i = 0; i < obj->instance_slot_names.size(); ++i) {
    obj->instance_slot_indices[obj->instance_slot_names[i]] = static_cast<uint32_t>(i);
  }
  v.as.obj = &obj->header;
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
      return "<" + klass->name + " object>";
    }
    return "<object>";
  }
  if (value_as_bound_method(value) != nullptr) {
    return "<bound method>";
  }
  return "<object>";
}

bool object_get_attr(const Value& object, const std::string& name, Value& out, std::string& error) {
  if (auto* klass = value_as_class(object)) {
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
    if (!class_lookup_attr(klass, name, out, error)) {
      error = "class '" + klass->name + "' has no attribute '" + name + "'";
      return false;
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
      error = "object has no attribute '" + name + "'";
      return false;
    }
    for (const auto& attr : instance->attrs) {
      if (attr.first == name) {
        value_assign_fast(out, attr.second);
        return true;
      }
    }
    Value class_attr;
    if (!class_lookup_attr(klass, name, class_attr, error)) {
      error = "object has no attribute '" + name + "'";
      return false;
    }
    if (value_as_function(class_attr) != nullptr || value_as_native_function(class_attr) != nullptr) {
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
    instance->attrs.push_back(std::make_pair(name, value));
    return true;
  }
  if (auto* klass = value_as_class(object)) {
    klass->attrs[name] = value;
    if (value_as_property(value) != nullptr) {
      klass->has_descriptors = true;
    }
    ++klass->version;
    return true;
  }
  error = "object does not support attribute assignment";
  return false;
}

bool object_delete_attr(Value& object, const std::string& name, std::string& error) {
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
  if (!klass_obj->has_explicit_bases) {
    klass_obj->bases.clear();
    klass_obj->has_explicit_bases = true;
  }
  klass_obj->bases.push_back(base);
  if (auto* base_class = value_as_class(base)) {
    klass_obj->has_descriptors = klass_obj->has_descriptors || class_or_bases_have_descriptors(base_class);
  }
  if (klass_obj->bases.size() == 1) {
    klass_obj->base = std::move(base);
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
