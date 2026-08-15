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

#include "xlang3/value.h"

#include <string>
#include <unordered_map>

namespace xlang3 {

struct ModuleObject {
  Object header;
  std::string name;
  std::unordered_map<std::string, Value> attrs;
};

ModuleObject* value_as_module(const Value& value);

void module_release_object(Object* object);
std::string module_to_string(const Value& value);

bool module_get_attr(const Value& object, const std::string& name, Value& out, std::string& error);
bool module_set_attr(Value& object, const std::string& name, const Value& value, std::string& error);

} // namespace xlang3
