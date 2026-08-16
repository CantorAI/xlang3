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

} // namespace

Value Value::class_object(
    std::string name,
    std::vector<std::pair<std::string, Value>> attrs,
    std::vector<std::string> instance_slots) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<ClassObject>(ObjectKind::Class);
  obj->name = std::move(name);
  for (auto& attr : attrs) {
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
  obj->klass = std::move(klass);
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
    auto it = klass->attrs.find(name);
    if (it == klass->attrs.end()) {
      error = "class '" + klass->name + "' has no attribute '" + name + "'";
      return false;
    }
    value_assign_fast(out, it->second);
    return true;
  }

  if (auto* instance = value_as_instance(object)) {
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
    auto class_it = klass->attrs.find(name);
    if (class_it == klass->attrs.end()) {
      error = "object has no attribute '" + name + "'";
      return false;
    }
    if (value_as_function(class_it->second) != nullptr || value_as_native_function(class_it->second) != nullptr) {
      out = Value::bound_method(object, class_it->second);
    } else {
      value_assign_fast(out, class_it->second);
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
    ++klass->version;
    return true;
  }
  error = "object does not support attribute assignment";
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
