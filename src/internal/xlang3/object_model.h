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
#include <string_view>
#include <unordered_map>

namespace xlang3 {

struct ClassObject {
  Object header;
  std::string name;
  Value base;
  std::vector<Value> bases;
  std::unordered_map<std::string, Value> attrs;
  std::vector<std::string> instance_slot_names;
  std::unordered_map<std::string, uint32_t> instance_slot_indices;
  uint64_t version = 1;
  bool has_explicit_bases = false;
  bool has_descriptors = false;
  bool has_getattribute_hook = false;
  bool has_getattr_hook = false;
  bool has_setattr_hook = false;
  bool has_delattr_hook = false;
  bool restrict_instance_attrs = false;
  bool allow_instance_dict = true;
  std::vector<Value> mro_cache;
  uint64_t mro_cache_version = 0;
};

struct InstanceObject {
  Object header;
  Value klass;
  Value mapping_storage;
  uint32_t slot_count = 0;
  std::string native_type;
  void* native_data = nullptr;
  void (*native_data_cleanup)(void*) = nullptr;
  Value inline_slots[8];
  std::vector<Value> overflow_slots;
  std::vector<std::pair<std::string, Value>> attrs;
};

struct BoundMethodObject {
  Object header;
  Value self;
  Value function;
};

struct StaticMethodObject {
  Object header;
  Value function;
};

struct ClassMethodObject {
  Object header;
  Value function;
};

struct SuperObject {
  Object header;
  Value klass;
  Value self;
};

XLANG3_HOT_INLINE ClassObject* value_as_class(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Class) {
    return nullptr;
  }
  return reinterpret_cast<ClassObject*>(value.as.obj);
}

XLANG3_HOT_INLINE InstanceObject* value_as_instance(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Instance) {
    return nullptr;
  }
  return reinterpret_cast<InstanceObject*>(value.as.obj);
}

XLANG3_HOT_INLINE BoundMethodObject* value_as_bound_method(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::BoundMethod) {
    return nullptr;
  }
  return reinterpret_cast<BoundMethodObject*>(value.as.obj);
}

XLANG3_HOT_INLINE StaticMethodObject* value_as_static_method(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::StaticMethod) {
    return nullptr;
  }
  return reinterpret_cast<StaticMethodObject*>(value.as.obj);
}

XLANG3_HOT_INLINE ClassMethodObject* value_as_class_method(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::ClassMethod) {
    return nullptr;
  }
  return reinterpret_cast<ClassMethodObject*>(value.as.obj);
}

XLANG3_HOT_INLINE SuperObject* value_as_super(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Super) {
    return nullptr;
  }
  return reinterpret_cast<SuperObject*>(value.as.obj);
}

XLANG3_HOT_INLINE uint32_t instance_slot_count(const InstanceObject* instance) {
  return instance->slot_count;
}

XLANG3_HOT_INLINE Value& instance_slot_at(InstanceObject* instance, uint32_t index) {
  return instance->slot_count <= 8 ? instance->inline_slots[index] : instance->overflow_slots[index];
}

XLANG3_HOT_INLINE const Value& instance_slot_at(const InstanceObject* instance, uint32_t index) {
  return instance->slot_count <= 8 ? instance->inline_slots[index] : instance->overflow_slots[index];
}

void object_model_release_object(Object* object);
std::string object_model_to_string(const Value& value);

bool object_get_attr(const Value& object, const std::string& name, Value& out, std::string& error);
bool object_set_attr(Value& object, const std::string& name, const Value& value, std::string& error);
bool object_delete_attr(Value& object, const std::string& name, std::string& error);
bool object_get_class_attr_for_instance(const Value& object, const std::string& name, Value& out, std::string& error);
bool object_lookup_class_attr(const Value& klass, const std::string& name, Value& out, std::string& error);
bool object_value_has_descriptor_get(const Value& value);
bool object_value_has_descriptor_set(const Value& value);
bool object_value_has_descriptor_delete(const Value& value);
bool object_value_is_descriptor(const Value& value);
bool object_value_is_data_descriptor(const Value& value);
bool object_construct(Value klass, const Value* args, uint32_t argc, Value& out, std::string& error);
bool class_set_base(Value klass, Value base, std::string& error);
bool class_is_subclass(const ClassObject* klass, const ClassObject* base);
bool class_has_builtin_base_name(ClassObject* klass, std::string_view name);
bool instance_set_native_data(
    Value instance,
    std::string native_type,
    void* native_data,
    void (*native_data_cleanup)(void*),
    std::string& error);
void* instance_get_native_data(const Value& instance, const std::string& native_type);

} // namespace xlang3
