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

struct TransportResult {
  bool ok = false;
  std::string error;
};

struct Transport {
  void* context = nullptr;
  TransportResult (*send)(void* context, const uint8_t* data, uint32_t size) = nullptr;
  TransportResult (*receive)(void* context, std::vector<uint8_t>& out) = nullptr;
  void (*close)(void* context) = nullptr;
};

} // namespace xlang3::rpc
