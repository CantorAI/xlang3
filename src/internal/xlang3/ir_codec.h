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

#include "xlang3/ir.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xlang3::ir {

struct EncodedModule {
  uint64_t source_hash = 0;
  std::vector<uint8_t> bytes;
};

uint64_t source_hash64(const uint8_t* data, std::size_t size);
bool encode_module(const Module& module, uint64_t source_hash, EncodedModule& out, std::string& error);
bool decode_module(const uint8_t* data, std::size_t size, uint64_t expected_source_hash, Module& out, std::string& error);

} // namespace xlang3::ir
