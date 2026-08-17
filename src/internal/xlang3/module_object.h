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
#include <unordered_map>
#include <vector>

namespace xlang3 {

class Runtime;

struct ModuleObject {
  Object header;
  uint64_t version = 0;
  std::string name;
  std::unordered_map<std::string, uint32_t> name_to_slot;
  std::vector<Value> slots;
};

XLANG3_HOT_INLINE ModuleObject* value_as_module(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Module) {
    return nullptr;
  }
  return reinterpret_cast<ModuleObject*>(value.as.obj);
}

void module_release_object(Object* object);
std::string module_to_string(const Value& value);

bool module_get_attr(const Value& object, const std::string& name, Value& out, std::string& error);
bool module_set_attr(Value& object, const std::string& name, const Value& value, std::string& error);
bool module_find_attr_slot(const Value& object, const std::string& name, uint32_t& slot, std::string& error);
bool module_ensure_attr_slots(Value& object, const std::vector<std::string>& names, std::string& error);

class NativeModuleBuilder {
public:
  NativeModuleBuilder(Runtime& runtime, std::string name);

  NativeModuleBuilder& value(std::string name, Value value);
  NativeModuleBuilder& function(
      std::string name,
      NativeFunctionCallback callback,
      NativeFastCallCallback fast_callback = nullptr,
      bool fast_releases_vm_lock = false);
  Value finish();

private:
  Runtime& runtime_;
  std::string name_;
  Value module_;
};

} // namespace xlang3
