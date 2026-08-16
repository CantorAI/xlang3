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

#include <cstdint>
#include <string>
#include <vector>

namespace xlang3::rpc {

enum class ValueTag : uint8_t {
  None = 0,
  Bool = 1,
  Int64 = 2,
  Double = 3,
  String = 4,
  Bytes = 5,
  List = 6,
  Tuple = 7,
  Dict = 8,
  RemoteRef = 9,
  Error = 10,
};

struct RemoteRef {
  uint32_t object_id = 0;
  std::string type_name;
  uint32_t flags = 0;
};

struct EncodedValue {
  ValueTag tag = ValueTag::None;
  std::vector<uint8_t> payload;
};

} // namespace xlang3::rpc
