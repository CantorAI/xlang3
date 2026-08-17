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

namespace xlang3 {

bool module_ensure_attr_slots(Value& object, const std::vector<std::string>& names, std::string& error) {
  auto* module = value_as_module(object);
  if (module == nullptr) {
    error = "object does not support module slot binding";
    return false;
  }

  for (size_t slot = 0; slot < names.size(); ++slot) {
    const auto& name = names[slot];
    auto it = module->name_to_slot.find(name);
    if (it != module->name_to_slot.end()) {
      if (it->second != slot) {
        error = "module slot order mismatch for '" + name + "'";
        return false;
      }
      continue;
    }

    module->name_to_slot[name] = static_cast<uint32_t>(slot);
    while (module->slots.size() <= slot) {
      module->slots.push_back(Value::invalid());
    }
  }
  return true;
}

} // namespace xlang3
