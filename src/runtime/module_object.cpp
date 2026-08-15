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
#include "xlang3/module_object.h"

#include "xlang3/runtime.h"

namespace xlang3 {

namespace {

template <typename T>
T* allocate_module_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  return obj;
}

} // namespace

Value Value::module(std::string name) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_module_object<ModuleObject>(ObjectKind::Module);
  obj->name = std::move(name);
  v.as.obj = &obj->header;
  return v;
}

ModuleObject* value_as_module(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Module) {
    return nullptr;
  }
  return reinterpret_cast<ModuleObject*>(value.as.obj);
}

void module_release_object(Object* object) {
  if (object->kind == ObjectKind::Module) {
    delete reinterpret_cast<ModuleObject*>(object);
  }
}

std::string module_to_string(const Value& value) {
  auto* module = value_as_module(value);
  if (module == nullptr) {
    return "<module>";
  }
  return "<module '" + module->name + "'>";
}

bool module_get_attr(const Value& object, const std::string& name, Value& out, std::string& error) {
  auto* module = value_as_module(object);
  if (module == nullptr) {
    error = "object has no attributes";
    return false;
  }
  auto it = module->attrs.find(name);
  if (it == module->attrs.end()) {
    error = "module '" + module->name + "' has no attribute '" + name + "'";
    return false;
  }
  out = it->second;
  return true;
}

bool module_set_attr(Value& object, const std::string& name, const Value& value, std::string& error) {
  auto* module = value_as_module(object);
  if (module == nullptr) {
    error = "object does not support attribute assignment";
    return false;
  }
  module->attrs[name] = value;
  return true;
}

NativeModuleBuilder::NativeModuleBuilder(Runtime& runtime, std::string name)
    : runtime_(runtime), name_(std::move(name)), module_(Value::module(name_)) {}

NativeModuleBuilder& NativeModuleBuilder::value(std::string name, Value value) {
  std::string error;
  module_set_attr(module_, name, value, error);
  return *this;
}

NativeModuleBuilder& NativeModuleBuilder::function(std::string name, NativeFunctionCallback callback) {
  auto full_name = name_ + "." + name;
  value(std::move(name), runtime_.make_native_function(std::move(full_name), callback));
  return *this;
}

Value NativeModuleBuilder::finish() {
  return std::move(module_);
}

} // namespace xlang3
