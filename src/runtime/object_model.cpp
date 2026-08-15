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

namespace xlang3 {

namespace {

template <typename T>
T* allocate_object_model(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  return obj;
}

} // namespace

Value Value::class_object(std::string name, std::vector<std::pair<std::string, Value>> attrs) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<ClassObject>(ObjectKind::Class);
  obj->name = std::move(name);
  for (auto& attr : attrs) {
    obj->attrs[std::move(attr.first)] = std::move(attr.second);
  }
  v.as.obj = &obj->header;
  return v;
}

Value Value::instance(Value klass) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_object_model<InstanceObject>(ObjectKind::Instance);
  obj->klass = std::move(klass);
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
      delete reinterpret_cast<InstanceObject*>(object);
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
    auto inst_it = instance->attrs.find(name);
    if (inst_it != instance->attrs.end()) {
      value_assign_fast(out, inst_it->second);
      return true;
    }
    auto* klass = value_as_class(instance->klass);
    if (klass == nullptr) {
      error = "instance has invalid class";
      return false;
    }
    auto class_it = klass->attrs.find(name);
    if (class_it == klass->attrs.end()) {
      error = "object has no attribute '" + name + "'";
      return false;
    }
    if (value_as_function(class_it->second) != nullptr) {
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
    instance->attrs[name] = value;
    return true;
  }
  if (auto* klass = value_as_class(object)) {
    klass->attrs[name] = value;
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

} // namespace xlang3
