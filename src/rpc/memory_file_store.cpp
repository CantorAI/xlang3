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
#include "xlang3/rpc/memory_file_store.h"

#include <algorithm>

namespace xlang3::rpc {

FileStore MemoryFileStore::as_file_store() {
  FileStore store{};
  store.context = this;
  store.put = [](void* context, const char* path, const uint8_t* data, uint32_t size, std::string&) {
    auto* self = static_cast<MemoryFileStore*>(context);
    self->files_[path] = std::vector<uint8_t>(data, data + size);
    return true;
  };
  store.get = [](void* context, const char* path, std::vector<uint8_t>& out, std::string& error) {
    auto* self = static_cast<MemoryFileStore*>(context);
    const auto it = self->files_.find(path);
    if (it == self->files_.end()) {
      error = std::string("file not found: ") + path;
      return false;
    }
    out = it->second;
    return true;
  };
  store.remove = [](void* context, const char* path, std::string& error) {
    auto* self = static_cast<MemoryFileStore*>(context);
    if (self->files_.erase(path) == 0) {
      error = std::string("file not found: ") + path;
      return false;
    }
    return true;
  };
  store.list = [](void* context, const char* path, std::vector<std::string>& out, std::string&) {
    auto* self = static_cast<MemoryFileStore*>(context);
    const std::string prefix = path == nullptr ? "" : path;
    for (const auto& item : self->files_) {
      if (prefix.empty() || item.first.rfind(prefix, 0) == 0) {
        out.push_back(item.first);
      }
    }
    std::sort(out.begin(), out.end());
    return true;
  };
  return store;
}

std::size_t MemoryFileStore::file_count() const {
  return files_.size();
}

std::size_t MemoryFileStore::byte_count() const {
  std::size_t total = 0;
  for (const auto& item : files_) {
    total += item.second.size();
  }
  return total;
}

} // namespace xlang3::rpc
