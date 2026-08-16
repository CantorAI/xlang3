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
#include <memory>
#include <string>
#include <vector>

namespace xlang3 {

enum class VfsNodeKind : uint8_t {
  Missing,
  File,
  Directory,
};

struct VfsStat {
  VfsNodeKind kind = VfsNodeKind::Missing;
  uint64_t size = 0;
};

class FileSystem {
public:
  virtual ~FileSystem() = default;
  virtual bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& error) = 0;
  virtual bool write_file(const std::string& path, const uint8_t* data, std::size_t size, std::string& error) = 0;
  virtual bool remove(const std::string& path, std::string& error) = 0;
  virtual bool list_dir(const std::string& path, std::vector<std::string>& out, std::string& error) = 0;
  virtual bool stat(const std::string& path, VfsStat& out, std::string& error) = 0;
};

struct ResolvedPath {
  FileSystem* fs = nullptr;
  std::string path;
};

class Vfs {
public:
  Vfs();
  ~Vfs();

  void set_root(std::unique_ptr<FileSystem> root);
  bool resolve(const std::string& path, ResolvedPath& out, std::string& error);
  bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& error);
  bool write_file(const std::string& path, const uint8_t* data, std::size_t size, std::string& error);
  bool remove(const std::string& path, std::string& error);
  bool list_dir(const std::string& path, std::vector<std::string>& out, std::string& error);
  bool stat(const std::string& path, VfsStat& out, std::string& error);

private:
  std::unique_ptr<FileSystem> root_;
};

} // namespace xlang3
