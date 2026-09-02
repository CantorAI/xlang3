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
#include "xlang3/vfs.h"

#if !defined(XLANG3_EMBEDDED)
#include <filesystem>
#include <functional>
#include <fstream>
#include <sstream>
#if defined(_WIN32)
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif
#endif

namespace xlang3 {
namespace {

#if !defined(XLANG3_EMBEDDED)
class OsFileSystem final : public FileSystem {
public:
  bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& error) override {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      error = "cannot open file " + path;
      return false;
    }
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    file.seekg(0, std::ios::beg);
    if (end > 0) {
      out.resize(static_cast<std::size_t>(end));
      file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
      return true;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();
    out.assign(text.begin(), text.end());
    return true;
  }

  bool write_file(const std::string& path, const uint8_t* data, std::size_t size, std::string& error) override {
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
      if (ec) {
        error = "cannot create directory " + parent.string() + ": " + ec.message();
        return false;
      }
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
      error = "cannot write file " + path;
      return false;
    }
    if (data != nullptr && size != 0) {
      file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    }
    return true;
  }

  bool remove(const std::string& path, std::string& error) override {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) {
      error = "cannot remove file " + path + ": " + ec.message();
      return false;
    }
    return true;
  }

  bool rename(const std::string& old_path, const std::string& new_path, bool replace, std::string& error) override {
    std::error_code ec;
    if (!replace && std::filesystem::exists(new_path, ec)) {
      error = "destination exists: " + new_path;
      return false;
    }
    if (replace && std::filesystem::exists(new_path, ec)) {
      std::filesystem::remove(new_path, ec);
      if (ec) {
        error = "cannot replace path " + new_path + ": " + ec.message();
        return false;
      }
    }
    std::filesystem::rename(old_path, new_path, ec);
    if (ec) {
      error = "cannot rename " + old_path + " to " + new_path + ": " + ec.message();
      return false;
    }
    return true;
  }

  bool make_dirs(const std::string& path, bool exist_ok, std::string& error) override {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      if (exist_ok && std::filesystem::is_directory(path, ec)) {
        return true;
      }
      error = "path exists: " + path;
      return false;
    }
    if (!std::filesystem::create_directories(path, ec) && ec) {
      error = "cannot create directory " + path + ": " + ec.message();
      return false;
    }
    return true;
  }

  bool list_dir(const std::string& path, std::vector<std::string>& out, std::string& error) override {
    std::error_code ec;
    out.clear();
    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
      out.push_back(entry.path().filename().string());
    }
    if (ec) {
      error = "cannot list directory " + path + ": " + ec.message();
      return false;
    }
    return true;
  }

  bool stat(const std::string& path, VfsStat& out, std::string& error) override {
    std::error_code ec;
    out = VfsStat{};
    if (!std::filesystem::exists(path, ec)) {
      if (ec) {
        error = "cannot stat path " + path + ": " + ec.message();
        return false;
      }
      return true;
    }
    const auto symlink_status = std::filesystem::symlink_status(path, ec);
    if (!ec) {
      out.is_symlink = std::filesystem::is_symlink(symlink_status);
    }
    const auto status = std::filesystem::status(path, ec);
    if (ec) {
      error = "cannot stat path " + path + ": " + ec.message();
      return false;
    }
    std::error_code abs_ec;
    const auto absolute_path = std::filesystem::absolute(path, abs_ec);
    const std::string inode_key = abs_ec ? path : absolute_path.string();
    out.inode = static_cast<uint64_t>(std::hash<std::string>{}(inode_key));
    if (out.inode == 0) {
      out.inode = 1;
    }
#if defined(_WIN32)
    struct _stat64 stat_buffer {};
    if (_stat64(path.c_str(), &stat_buffer) == 0) {
      out.atime_ns = static_cast<int64_t>(stat_buffer.st_atime) * 1000000000LL;
      out.mtime_ns = static_cast<int64_t>(stat_buffer.st_mtime) * 1000000000LL;
      out.ctime_ns = static_cast<int64_t>(stat_buffer.st_ctime) * 1000000000LL;
    }
#else
    struct stat stat_buffer {};
    if (::stat(path.c_str(), &stat_buffer) == 0) {
      out.atime_ns = static_cast<int64_t>(stat_buffer.st_atime) * 1000000000LL;
      out.mtime_ns = static_cast<int64_t>(stat_buffer.st_mtime) * 1000000000LL;
      out.ctime_ns = static_cast<int64_t>(stat_buffer.st_ctime) * 1000000000LL;
    }
#endif
    if (std::filesystem::is_regular_file(status)) {
      out.kind = VfsNodeKind::File;
      out.size = static_cast<uint64_t>(std::filesystem::file_size(path, ec));
      if (ec) {
        out.size = 0;
      }
      return true;
    }
    if (std::filesystem::is_directory(status)) {
      out.kind = VfsNodeKind::Directory;
      out.size = 0;
      return true;
    }
    out.kind = VfsNodeKind::Missing;
    out.size = 0;
    out.inode = 0;
    return true;
  }
};
#endif

} // namespace

#if !defined(XLANG3_EMBEDDED)
Vfs::Vfs() : root_(std::make_unique<OsFileSystem>()) {
  std::error_code ec;
  auto cwd = std::filesystem::current_path(ec);
  current_directory_ = ec ? "." : cwd.string();
}
#else
Vfs::Vfs() : current_directory_("/") {}
#endif
Vfs::~Vfs() = default;

void Vfs::set_root(std::unique_ptr<FileSystem> root) {
  root_ = std::move(root);
}

bool Vfs::resolve(const std::string& path, ResolvedPath& out, std::string& error) {
  if (path.empty()) {
    error = "empty path";
    return false;
  }
  if (root_ == nullptr) {
    error = "no filesystem mounted";
    return false;
  }
  out.fs = root_.get();
#if !defined(XLANG3_EMBEDDED)
  std::filesystem::path fs_path(path);
  if (!fs_path.is_absolute() && !current_directory_.empty()) {
    fs_path = std::filesystem::path(current_directory_) / fs_path;
  }
  out.path = fs_path.lexically_normal().string();
#else
  if (!current_directory_.empty() && path.front() != '/') {
    out.path = current_directory_ + "/" + path;
  } else {
    out.path = path;
  }
#endif
  return true;
}

bool Vfs::read_file(const std::string& path, std::vector<uint8_t>& out, std::string& error) {
  ResolvedPath resolved;
  return resolve(path, resolved, error) && resolved.fs->read_file(resolved.path, out, error);
}

bool Vfs::write_file(const std::string& path, const uint8_t* data, std::size_t size, std::string& error) {
  ResolvedPath resolved;
  return resolve(path, resolved, error) && resolved.fs->write_file(resolved.path, data, size, error);
}

bool Vfs::remove(const std::string& path, std::string& error) {
  ResolvedPath resolved;
  return resolve(path, resolved, error) && resolved.fs->remove(resolved.path, error);
}

bool Vfs::rename(const std::string& old_path, const std::string& new_path, bool replace, std::string& error) {
  ResolvedPath old_resolved;
  ResolvedPath new_resolved;
  if (!resolve(old_path, old_resolved, error) || !resolve(new_path, new_resolved, error)) {
    return false;
  }
  if (old_resolved.fs != new_resolved.fs) {
    error = "cross-filesystem rename is not supported";
    return false;
  }
  return old_resolved.fs->rename(old_resolved.path, new_resolved.path, replace, error);
}

bool Vfs::make_dirs(const std::string& path, bool exist_ok, std::string& error) {
  ResolvedPath resolved;
  return resolve(path, resolved, error) && resolved.fs->make_dirs(resolved.path, exist_ok, error);
}

bool Vfs::list_dir(const std::string& path, std::vector<std::string>& out, std::string& error) {
  ResolvedPath resolved;
  return resolve(path, resolved, error) && resolved.fs->list_dir(resolved.path, out, error);
}

bool Vfs::stat(const std::string& path, VfsStat& out, std::string& error) {
  ResolvedPath resolved;
  return resolve(path, resolved, error) && resolved.fs->stat(resolved.path, out, error);
}

bool Vfs::chdir(const std::string& path, std::string& error) {
  ResolvedPath resolved;
  if (!resolve(path, resolved, error)) {
    return false;
  }
  VfsStat stat;
  if (!resolved.fs->stat(resolved.path, stat, error)) {
    return false;
  }
  if (stat.kind != VfsNodeKind::Directory) {
    error = "not a directory: " + path;
    return false;
  }
  current_directory_ = std::move(resolved.path);
  return true;
}

} // namespace xlang3
