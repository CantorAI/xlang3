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
#include <set>

namespace xlang3::rpc {
namespace {

std::string normalize_path(const char* raw) {
  const std::string input = raw == nullptr ? "" : raw;
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= input.size()) {
    const size_t slash = input.find('/', start);
    const std::string part = input.substr(
        start, slash == std::string::npos ? std::string::npos : slash - start);
    if (!part.empty() && part != ".") {
      if (part == "..") {
        if (!parts.empty()) parts.pop_back();
      } else {
        parts.push_back(part);
      }
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  std::string normalized = "/";
  for (size_t index = 0; index < parts.size(); ++index) {
    if (index != 0) normalized.push_back('/');
    normalized += parts[index];
  }
  return normalized;
}

bool direct_child_of(
    const std::string& path,
    const std::string& directory,
    std::string& child) {
  const std::string prefix = directory == "/" ? "/" : directory + "/";
  if (path.rfind(prefix, 0) != 0 || path.size() <= prefix.size()) return false;
  const size_t start = prefix.size();
  const size_t slash = path.find('/', start);
  child = path.substr(
      start, slash == std::string::npos ? std::string::npos : slash - start);
  return !child.empty();
}

} // namespace

FileStore MemoryFileStore::as_file_store() {
  FileStore store{};
  store.context = this;
  store.put = [](void* context, const char* path, const uint8_t* data, uint32_t size, std::string&) {
    auto* self = static_cast<MemoryFileStore*>(context);
    auto& contents = self->files_[normalize_path(path)];
    if (size == 0) {
      contents.clear();
    } else {
      contents.assign(data, data + size);
    }
    return true;
  };
  store.get = [](void* context, const char* path, std::vector<uint8_t>& out, std::string& error) {
    auto* self = static_cast<MemoryFileStore*>(context);
    const std::string normalized = normalize_path(path);
    const auto it = self->files_.find(normalized);
    if (it == self->files_.end()) {
      error = "file not found: " + normalized;
      return false;
    }
    out = it->second;
    return true;
  };
  store.remove = [](void* context, const char* path, std::string& error) {
    auto* self = static_cast<MemoryFileStore*>(context);
    const std::string normalized = normalize_path(path);
    if (self->files_.erase(normalized) == 0) {
      error = "file not found: " + normalized;
      return false;
    }
    return true;
  };
  store.list = [](void* context, const char* path, std::vector<std::string>& out, std::string&) {
    auto* self = static_cast<MemoryFileStore*>(context);
    const std::string directory = normalize_path(path);
    std::set<std::string> children;
    for (const auto& item : self->files_) {
      std::string child;
      if (direct_child_of(item.first, directory, child)) children.insert(child);
    }
    out.assign(children.begin(), children.end());
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
