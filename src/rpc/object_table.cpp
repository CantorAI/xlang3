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
#include "xlang3/rpc/object_table.h"

namespace xlang3::rpc {

uint32_t ObjectTable::retain(Value value) {
  xlang3::retain(value);
  const uint32_t id = next_id_++;
  objects_.emplace(id, value);
  return id;
}

bool ObjectTable::get(uint32_t id, Value& out) const {
  const auto it = objects_.find(id);
  if (it == objects_.end()) {
    return false;
  }
  out = it->second;
  return true;
}

bool ObjectTable::release(uint32_t id) {
  const auto it = objects_.find(id);
  if (it == objects_.end()) {
    return false;
  }
  xlang3::release(it->second);
  objects_.erase(it);
  return true;
}

void ObjectTable::clear() {
  for (auto& item : objects_) {
    xlang3::release(item.second);
  }
  objects_.clear();
}

} // namespace xlang3::rpc
