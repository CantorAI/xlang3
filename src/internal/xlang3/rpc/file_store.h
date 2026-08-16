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

struct FileStore {
  void* context = nullptr;
  bool (*put)(void* context, const char* path, const uint8_t* data, uint32_t size, std::string& error) = nullptr;
  bool (*get)(void* context, const char* path, std::vector<uint8_t>& out, std::string& error) = nullptr;
  bool (*remove)(void* context, const char* path, std::string& error) = nullptr;
  bool (*list)(void* context, const char* path, std::vector<std::string>& out, std::string& error) = nullptr;
};

} // namespace xlang3::rpc
