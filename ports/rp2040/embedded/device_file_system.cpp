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
#include "embedded/device_file_system.h"

#include <cstdint>

namespace xlang3::pico {
namespace {

std::string strip_mount(const std::string& path, const char* mount) {
  const std::string prefix = mount;
  if (path == prefix) {
    return "/";
  }
  if (path.rfind(prefix + "/", 0) == 0) {
    return path.substr(prefix.size());
  }
  return {};
}

std::string normalize_path(const std::string& path) {
  if (path.empty()) {
    return "/";
  }
  return path[0] == '/' ? path : "/" + path;
}

} // namespace

DeviceFileSystem::DeviceFileSystem(xlang3::rpc::FileStore* ram, xlang3::rpc::FileStore* flash)
    : ram_(ram), flash_(flash) {}

bool DeviceFileSystem::route(const std::string& path, Route& out, std::string& error) const {
  const std::string normalized = normalize_path(path);
  const std::string ram_path = strip_mount(normalized, "/ram");
  if (!ram_path.empty()) {
    if (ram_ == nullptr) {
      error = "RAM filesystem is not mounted";
      return false;
    }
    out.store = ram_;
    out.path = ram_path;
    return true;
  }

  const std::string flash_path = strip_mount(normalized, "/flash");
  if (!flash_path.empty()) {
    if (flash_ == nullptr) {
      error = "flash filesystem is not mounted";
      return false;
    }
    out.store = flash_;
    out.path = flash_path;
    return true;
  }

  if (flash_ == nullptr) {
    error = "flash filesystem is not mounted";
    return false;
  }
  out.store = flash_;
  out.path = normalized;
  return true;
}

bool DeviceFileSystem::read_file(const std::string& path, std::vector<uint8_t>& out, std::string& error) {
  Route resolved;
  return route(path, resolved, error) &&
         resolved.store->get(resolved.store->context, resolved.path.c_str(), out, error);
}

bool DeviceFileSystem::write_file(const std::string& path, const uint8_t* data, std::size_t size, std::string& error) {
  if (size > UINT32_MAX) {
    error = "file is too large";
    return false;
  }
  Route resolved;
  return route(path, resolved, error) &&
         resolved.store->put(resolved.store->context, resolved.path.c_str(), data, static_cast<uint32_t>(size), error);
}

bool DeviceFileSystem::remove(const std::string& path, std::string& error) {
  Route resolved;
  return route(path, resolved, error) &&
         resolved.store->remove(resolved.store->context, resolved.path.c_str(), error);
}

bool DeviceFileSystem::list_dir(const std::string& path, std::vector<std::string>& out, std::string& error) {
  Route resolved;
  return route(path, resolved, error) &&
         resolved.store->list(resolved.store->context, resolved.path.c_str(), out, error);
}

bool DeviceFileSystem::stat(const std::string& path, VfsStat& out, std::string& error) {
  std::vector<uint8_t> data;
  if (read_file(path, data, error)) {
    out.kind = VfsNodeKind::File;
    out.size = data.size();
    return true;
  }

  std::vector<std::string> entries;
  std::string list_error;
  if (list_dir(path, entries, list_error) && !entries.empty()) {
    out.kind = VfsNodeKind::Directory;
    out.size = 0;
    error.clear();
    return true;
  }

  out.kind = VfsNodeKind::Missing;
  out.size = 0;
  return true;
}

} // namespace xlang3::pico
