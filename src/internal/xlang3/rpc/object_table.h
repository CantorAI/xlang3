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

#include <cstdint>
#include <unordered_map>

namespace xlang3::rpc {

class ObjectTable {
public:
  uint32_t retain(Value value);
  bool get(uint32_t id, Value& out) const;
  bool release(uint32_t id);
  void clear();

private:
  uint32_t next_id_ = 1;
  std::unordered_map<uint32_t, Value> objects_;
};

} // namespace xlang3::rpc
