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

#include <unordered_set>

namespace xlang3 {

bool module_ensure_attr_slots(Value& object, const std::vector<std::string>& names, std::string& error) {
  auto* module = value_as_module(object);
  if (module == nullptr) {
    error = "object does not support module slot binding";
    return false;
  }

  bool needs_relayout = module->slots.size() < names.size();
  for (size_t slot = 0; slot < names.size(); ++slot) {
    const auto& name = names[slot];
    auto it = module->name_to_slot.find(name);
    if (it == module->name_to_slot.end() || it->second != slot) {
      needs_relayout = true;
      break;
    }
  }

  if (!needs_relayout) {
    return true;
  }

  std::unordered_set<std::string> ir_names;
  ir_names.reserve(names.size());
  for (const auto& name : names) {
    ir_names.insert(name);
  }

  std::vector<std::pair<std::string, Value>> preserved;
  preserved.reserve(module->name_to_slot.size());
  for (const auto& entry : module->name_to_slot) {
    if (ir_names.find(entry.first) != ir_names.end()) {
      continue;
    }
    if (entry.second < module->slots.size()) {
      preserved.push_back({entry.first, module->slots[entry.second]});
    }
  }

  std::vector<Value> new_slots(names.size(), Value::invalid());
  std::unordered_map<std::string, uint32_t> new_slot_map;
  new_slot_map.reserve(names.size() + preserved.size());
  for (size_t slot = 0; slot < names.size(); ++slot) {
    const auto& name = names[slot];
    auto old_it = module->name_to_slot.find(name);
    if (old_it != module->name_to_slot.end() && old_it->second < module->slots.size()) {
      new_slots[slot] = module->slots[old_it->second];
    }
    new_slot_map[name] = static_cast<uint32_t>(slot);
  }
  for (auto& entry : preserved) {
    new_slot_map[entry.first] = static_cast<uint32_t>(new_slots.size());
    new_slots.push_back(std::move(entry.second));
  }
  module->slots = std::move(new_slots);
  module->name_to_slot = std::move(new_slot_map);
  ++module->version;
  return true;
}

} // namespace xlang3
