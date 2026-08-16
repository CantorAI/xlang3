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

#include "xlang3/rpc/file_store.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace xlang3::rpc {

class MemoryFileStore {
public:
  FileStore as_file_store();
  std::size_t file_count() const;
  std::size_t byte_count() const;

private:
  std::unordered_map<std::string, std::vector<uint8_t>> files_;
};

} // namespace xlang3::rpc
