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

#include <algorithm>

#if !defined(XLANG3_EMBEDDED)
#include <filesystem>
#include <functional>
#include <fstream>
#include <limits>
#include <sstream>
#if defined(_WIN32)
#include <sys/stat.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#endif

namespace xlang3 {
namespace {

std::string normalize_virtual_path(
    const std::string& path,
    const std::string& current_directory) {
  std::string combined = path;
  std::replace(combined.begin(), combined.end(), '\\', '/');
  if (combined.empty() || combined.front() != '/') {
    std::string base = current_directory.empty() ? "/" : current_directory;
    std::replace(base.begin(), base.end(), '\\', '/');
    if (base.back() != '/') base.push_back('/');
    combined = base + combined;
  }
  std::vector<std::string> components;
  size_t start = 0;
  while (start <= combined.size()) {
    const size_t slash = combined.find('/', start);
    const std::string component = combined.substr(
        start, slash == std::string::npos ? std::string::npos : slash - start);
    if (!component.empty() && component != ".") {
      if (component == "..") {
        if (!components.empty()) components.pop_back();
      } else {
        components.push_back(component);
      }
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  std::string normalized = "/";
  for (size_t index = 0; index < components.size(); ++index) {
    if (index != 0) normalized.push_back('/');
    normalized += components[index];
  }
  return normalized;
}

#if !defined(XLANG3_EMBEDDED)
#if defined(_WIN32)
std::wstring filesystem_wide_path(std::string_view text) {
  std::wstring result;
  result.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t width = 0;
    uint32_t codepoint = 0;
    if (lead < 0x80u) {
      width = 1;
      codepoint = lead;
    } else if ((lead & 0xe0u) == 0xc0u) {
      width = 2;
      codepoint = lead & 0x1fu;
    } else if ((lead & 0xf0u) == 0xe0u) {
      width = 3;
      codepoint = lead & 0x0fu;
    } else if ((lead & 0xf8u) == 0xf0u) {
      width = 4;
      codepoint = lead & 0x07u;
    } else {
      width = 1;
      codepoint = 0xfffdu;
    }
    if (i + width <= text.size() && width > 1) {
      bool valid = true;
      for (size_t j = 1; j < width; ++j) {
        const unsigned char continuation = static_cast<unsigned char>(text[i + j]);
        if ((continuation & 0xc0u) != 0x80u) {
          valid = false;
          break;
        }
        codepoint = (codepoint << 6u) | (continuation & 0x3fu);
      }
      if (!valid) {
        width = 1;
        codepoint = 0xfffdu;
      }
    } else if (width > 1) {
      width = 1;
      codepoint = 0xfffdu;
    }
    if (codepoint <= 0xffffu) {
      result.push_back(static_cast<wchar_t>(codepoint));
    } else if (codepoint <= 0x10ffffu) {
      codepoint -= 0x10000u;
      result.push_back(static_cast<wchar_t>(0xd800u + (codepoint >> 10u)));
      result.push_back(static_cast<wchar_t>(0xdc00u + (codepoint & 0x3ffu)));
    } else {
      result.push_back(static_cast<wchar_t>(0xfffdu));
    }
    i += width;
  }
  return result;
}

int64_t filetime_to_unix_ns(const FILETIME& value) {
  ULARGE_INTEGER ticks{};
  ticks.LowPart = value.dwLowDateTime;
  ticks.HighPart = value.dwHighDateTime;
  constexpr uint64_t kUnixEpochTicks = 116444736000000000ULL;
  if (ticks.QuadPart >= kUnixEpochTicks) {
    return static_cast<int64_t>(ticks.QuadPart - kUnixEpochTicks) * 100LL;
  }
  return -static_cast<int64_t>(kUnixEpochTicks - ticks.QuadPart) * 100LL;
}
#endif

std::filesystem::path filesystem_path(const std::string& path) {
#if defined(_WIN32)
  if (path.rfind("\\\\?\\", 0) == 0 || path.rfind("//?/", 0) == 0) {
    return std::filesystem::path(filesystem_wide_path(path));
  }
  std::string normalized = path;
  size_t component_start = 0;
  for (size_t i = 0; i <= normalized.size(); ++i) {
    if (i != normalized.size() && normalized[i] != '/' && normalized[i] != '\\') {
      continue;
    }
    size_t component_end = i;
    while (component_end > component_start && normalized[component_end - 1] == ' ') {
      --component_end;
    }
    if (component_end != i) {
      normalized.erase(component_end, i - component_end);
      i = component_end;
    }
    component_start = i + 1;
  }
  return std::filesystem::path(filesystem_wide_path(normalized));
#else
  return std::filesystem::path(path);
#endif
}

std::string filesystem_path_text(const std::filesystem::path& path) {
#if defined(_WIN32)
  return path.u8string();
#else
  return path.string();
#endif
}

class OsFileSystem final : public FileSystem {
public:
  bool native_path(const std::string& path, std::string& out) override {
    out = path;
    return true;
  }

  bool read_link(const std::string& path, std::string& out, std::string& error) override {
    std::error_code ec;
    const auto target = std::filesystem::read_symlink(filesystem_path(path), ec);
    if (ec) {
      error = "cannot read symbolic link " + path + ": " + ec.message();
      return false;
    }
    out = filesystem_path_text(target);
#if defined(_WIN32)
    if (out.size() >= 3 && out[1] == ':' &&
        (out[2] == '\\' || out[2] == '/') && out.rfind("\\\\?\\", 0) != 0) {
      out = "\\\\?\\" + out;
    }
#endif
    return true;
  }
  bool read_file(const std::string& path, std::vector<uint8_t>& out, std::string& error) override {
    const auto native = filesystem_path(path);
#if defined(_WIN32)
    HANDLE file = ::CreateFileW(
        native.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
      const DWORD open_error = ::GetLastError();
      if (open_error == ERROR_ACCESS_DENIED) {
        const DWORD attributes = ::GetFileAttributesW(native.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
          error = "cannot open directory as file " + path;
          return false;
        }
      }
      error = "cannot open file " + path;
      return false;
    }
    struct FileGuard {
      HANDLE value;
      ~FileGuard() { ::CloseHandle(value); }
    } file_guard{file};
    LARGE_INTEGER file_size{};
    if (!::GetFileSizeEx(file, &file_size) || file_size.QuadPart < 0 ||
        static_cast<uint64_t>(file_size.QuadPart) > out.max_size()) {
      error = "file is too large " + path;
      return false;
    }
    out.resize(static_cast<size_t>(file_size.QuadPart));
    size_t offset = 0;
    while (offset < out.size()) {
      const DWORD requested = static_cast<DWORD>(std::min<size_t>(
          out.size() - offset, static_cast<size_t>((std::numeric_limits<DWORD>::max)())));
      DWORD received = 0;
      if (!::ReadFile(file, out.data() + offset, requested, &received, nullptr) || received == 0) {
        out.clear();
        error = "cannot read file " + path;
        return false;
      }
      offset += received;
    }
    return true;
#else
    std::ifstream file(native, std::ios::binary);
    if (!file) {
      std::error_code ec;
      if (std::filesystem::is_directory(native, ec)) {
        error = "cannot open directory as file " + path;
        return false;
      }
      error = "cannot open file " + path;
      return false;
    }
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end > 0) {
      const auto size = static_cast<uintmax_t>(end);
      if (size > out.max_size() ||
          size > static_cast<uintmax_t>((std::numeric_limits<std::streamsize>::max)())) {
        error = "file is too large " + path;
        return false;
      }
      file.seekg(0, std::ios::beg);
      if (!file) {
        error = "cannot seek file " + path;
        return false;
      }
      out.resize(static_cast<std::size_t>(size));
      if (size != 0) {
        file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
        if (!file) {
          out.clear();
          error = "cannot read file " + path;
          return false;
        }
      }
      return true;
    }
    file.clear();
    file.seekg(0, std::ios::beg);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    if (file.bad()) {
      error = "cannot read file " + path;
      return false;
    }
    const std::string text = buffer.str();
    out.assign(text.begin(), text.end());
    return true;
#endif
  }

  bool directory_mtime(const std::string& path, int64_t& mtime_ns, std::string& error) {
#if defined(_WIN32)
    std::filesystem::path native_path;
    try {
      native_path = filesystem_path(path);
    } catch (const std::exception&) {
      return false;
    }
    WIN32_FILE_ATTRIBUTE_DATA file_data{};
    if (::GetFileAttributesExW(native_path.c_str(), GetFileExInfoStandard, &file_data) == 0) {
      const DWORD code = ::GetLastError();
      if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ||
          code == ERROR_INVALID_NAME) {
        return false;
      }
      error = "cannot inspect directory " + path + ": " +
          std::error_code(static_cast<int>(code), std::system_category()).message();
      return false;
    }
    if ((file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) return false;
    mtime_ns = filetime_to_unix_ns(file_data.ftLastWriteTime);
    return true;
#else
    VfsStat info;
    if (!stat(path, info, error) || info.kind != VfsNodeKind::Directory) return false;
    mtime_ns = info.mtime_ns;
    return true;
#endif
  }

  bool write_file(const std::string& path, const uint8_t* data, std::size_t size, std::string& error) override {
    const auto native_path = filesystem_path(path);
    const auto parent = native_path.parent_path();
    if (!parent.empty()) {
      std::error_code ec;
      std::filesystem::create_directories(parent, ec);
      if (ec) {
        error = "cannot create directory " + filesystem_path_text(parent) + ": " + ec.message();
        return false;
      }
    }
    std::ofstream file(native_path, std::ios::binary | std::ios::trunc);
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
    const bool removed = std::filesystem::remove(filesystem_path(path), ec);
    if (ec) {
      error = "cannot remove file " + path + ": " + ec.message();
      return false;
    }
    if (!removed) {
      error = "file not found: " + path;
      return false;
    }
    return true;
  }

  bool rename(const std::string& old_path, const std::string& new_path, bool replace, std::string& error) override {
    std::error_code ec;
    const auto old_native_path = filesystem_path(old_path);
    const auto new_native_path = filesystem_path(new_path);
    if (!replace && std::filesystem::exists(new_native_path, ec)) {
      error = "destination exists: " + new_path;
      return false;
    }
    if (replace && std::filesystem::exists(new_native_path, ec)) {
      std::filesystem::remove(new_native_path, ec);
      if (ec) {
        error = "cannot replace path " + new_path + ": " + ec.message();
        return false;
      }
    }
    std::filesystem::rename(old_native_path, new_native_path, ec);
    if (ec) {
      error = "cannot rename " + old_path + " to " + new_path + ": " + ec.message();
      return false;
    }
    return true;
  }

  bool make_dirs(const std::string& path, bool exist_ok, std::string& error) override {
    std::error_code ec;
    const auto native_path = filesystem_path(path);
    if (std::filesystem::exists(native_path, ec)) {
      if (exist_ok && std::filesystem::is_directory(native_path, ec)) {
        return true;
      }
      error = "path exists: " + path;
      return false;
    }
    if (!std::filesystem::create_directories(native_path, ec) && ec) {
      error = "cannot create directory " + path + ": " + ec.message();
      return false;
    }
    return true;
  }

  bool list_dir(const std::string& path, std::vector<std::string>& out, std::string& error) override {
    std::error_code ec;
    out.clear();
    for (const auto& entry : std::filesystem::directory_iterator(filesystem_path(path), ec)) {
      out.push_back(filesystem_path_text(entry.path().filename()));
    }
    if (ec) {
      error = "cannot list directory " + path + ": " + ec.message();
      return false;
    }
    return true;
  }

  bool kind(const std::string& path, VfsNodeKind& out, std::string& error) override {
    out = VfsNodeKind::Missing;
    std::filesystem::path native_path;
    try {
      native_path = filesystem_path(path);
    } catch (const std::exception&) {
#if defined(_WIN32)
      return true;
#else
      throw;
#endif
    }
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesW(native_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const DWORD code = GetLastError();
      if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ||
          code == ERROR_INVALID_NAME) {
        return true;
      }
      error = "cannot inspect path " + path + ": " +
          std::error_code(static_cast<int>(code), std::system_category()).message();
      return false;
    }
    out = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
        ? VfsNodeKind::Directory : VfsNodeKind::File;
#else
    std::error_code ec;
    const auto status = std::filesystem::status(native_path, ec);
    if (ec) {
      if (ec == std::errc::no_such_file_or_directory) return true;
      error = "cannot inspect path " + path + ": " + ec.message();
      return false;
    }
    if (std::filesystem::is_regular_file(status)) out = VfsNodeKind::File;
    else if (std::filesystem::is_directory(status)) out = VfsNodeKind::Directory;
#endif
    return true;
  }

  bool stat(const std::string& path, VfsStat& out, std::string& error) override {
    std::error_code ec;
    out = VfsStat{};
    std::filesystem::path native_path;
    try {
      native_path = filesystem_path(path);
    } catch (const std::exception&) {
#if defined(_WIN32)
      // Windows cannot name an entry containing an unpaired UTF-16
      // surrogate. Treat it as a missing path, as CPython's wide APIs do.
      return true;
#else
      throw;
#endif
    }
#if defined(_WIN32)
    WIN32_FILE_ATTRIBUTE_DATA file_data{};
    if (GetFileAttributesExW(
            native_path.c_str(), GetFileExInfoStandard, &file_data) == 0) {
      const DWORD code = GetLastError();
      if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND ||
          code == ERROR_INVALID_NAME) {
        return true;
      }
      error = "cannot stat path " + path + ": " +
          std::error_code(static_cast<int>(code), std::system_category()).message();
      return false;
    }
    out.file_attributes = file_data.dwFileAttributes;
    out.is_symlink = false;
#else
    if (!std::filesystem::exists(native_path, ec)) {
      if (ec) {
        error = "cannot stat path " + path + ": " + ec.message();
        return false;
      }
      return true;
    }
#endif
#if !defined(_WIN32)
    const auto symlink_status = std::filesystem::symlink_status(native_path, ec);
    if (!ec) {
      out.is_symlink = std::filesystem::is_symlink(symlink_status);
    }
    ec.clear();
    const auto status = std::filesystem::status(native_path, ec);
    const bool have_status = !ec;
    if (ec) {
      error = "cannot stat path " + path + ": " + ec.message();
      return false;
    }
    std::error_code abs_ec;
    const auto absolute_path = std::filesystem::absolute(native_path, abs_ec);
    const std::string inode_key = abs_ec ? path : filesystem_path_text(absolute_path);
    out.inode = static_cast<uint64_t>(std::hash<std::string>{}(inode_key));
    if (out.inode == 0) {
      out.inode = 1;
    }
#endif
#if defined(_WIN32)
    out.inode = 0;
    BY_HANDLE_FILE_INFORMATION handle_info{};
    bool have_handle_info = false;
    HANDLE stat_handle = CreateFileW(
        native_path.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (stat_handle != INVALID_HANDLE_VALUE) {
      if (GetFileInformationByHandle(stat_handle, &handle_info)) {
        have_handle_info = true;
        out.inode = (static_cast<uint64_t>(handle_info.nFileIndexHigh) << 32u) |
            static_cast<uint64_t>(handle_info.nFileIndexLow);
        if (out.inode == 0) {
          out.inode = 1;
        }
      }
      CloseHandle(stat_handle);
    }
    if ((out.file_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      HANDLE reparse_handle = CreateFileW(
          native_path.c_str(), 0,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
          nullptr, OPEN_EXISTING,
          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
          nullptr);
      if (reparse_handle != INVALID_HANDLE_VALUE) {
        FILE_ATTRIBUTE_TAG_INFO tag_info{};
        if (GetFileInformationByHandleEx(
                reparse_handle, FileAttributeTagInfo, &tag_info,
                sizeof(tag_info))) {
          out.file_attributes = tag_info.FileAttributes;
          out.reparse_tag = tag_info.ReparseTag;
          out.is_symlink = tag_info.ReparseTag == IO_REPARSE_TAG_SYMLINK;
        }
        CloseHandle(reparse_handle);
      }
    }
    out.atime_ns = filetime_to_unix_ns(file_data.ftLastAccessTime);
    out.mtime_ns = filetime_to_unix_ns(file_data.ftLastWriteTime);
    out.ctime_ns = filetime_to_unix_ns(file_data.ftCreationTime);
    if (have_handle_info) {
      out.atime_ns = filetime_to_unix_ns(handle_info.ftLastAccessTime);
      out.mtime_ns = filetime_to_unix_ns(handle_info.ftLastWriteTime);
      out.ctime_ns = filetime_to_unix_ns(handle_info.ftCreationTime);
    }
#else
    struct stat stat_buffer {};
    if (::stat(native_path.c_str(), &stat_buffer) == 0) {
      out.atime_ns = static_cast<int64_t>(stat_buffer.st_atime) * 1000000000LL;
      out.mtime_ns = static_cast<int64_t>(stat_buffer.st_mtime) * 1000000000LL;
      out.ctime_ns = static_cast<int64_t>(stat_buffer.st_ctime) * 1000000000LL;
    }
#endif
#if defined(_WIN32)
    if ((out.file_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      out.kind = VfsNodeKind::Directory;
      out.size = 0;
    } else {
      out.kind = VfsNodeKind::File;
      out.size = (static_cast<uint64_t>(file_data.nFileSizeHigh) << 32u) |
          static_cast<uint64_t>(file_data.nFileSizeLow);
    }
    return true;
#else
    if (!have_status) {
      out.kind = VfsNodeKind::Missing;
      return true;
    }
    if (std::filesystem::is_regular_file(status)) {
      out.kind = VfsNodeKind::File;
      out.size = static_cast<uint64_t>(std::filesystem::file_size(native_path, ec));
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
#endif
  }
};
#endif

} // namespace

#if !defined(XLANG3_EMBEDDED)
Vfs::Vfs() : root_(std::make_unique<OsFileSystem>()), host_paths_(true) {
  std::error_code ec;
  auto cwd = std::filesystem::current_path(ec);
  current_directory_ = ec ? "." : filesystem_path_text(cwd);
}
#else
Vfs::Vfs() : current_directory_("/") {}
#endif
Vfs::~Vfs() = default;

void Vfs::set_root(std::unique_ptr<FileSystem> root) {
  root_ = std::move(root);
  mounts_.clear();
  // Mounted filesystems define their own namespace. Do not retain the host
  // process cwd when switching to an embedded or application-provided root.
  current_directory_ = "/";
  host_paths_ = false;
}

void Vfs::mount(
    std::string prefix,
    std::shared_ptr<FileSystem> filesystem) {
  prefix = normalize_virtual_path(prefix, "/");
  mounts_.erase(
      std::remove_if(
          mounts_.begin(), mounts_.end(),
          [&](const Mount& mount) { return mount.prefix == prefix; }),
      mounts_.end());
  if (filesystem == nullptr) return;
  mounts_.push_back({std::move(prefix), std::move(filesystem)});
  std::sort(
      mounts_.begin(), mounts_.end(),
      [](const Mount& left, const Mount& right) {
        return left.prefix.size() > right.prefix.size();
      });
}

bool Vfs::is_mounted_path(const std::string& path) const {
  if (path.empty()) return false;
  const std::string normalized = normalize_virtual_path(path, current_directory_);
  for (const auto& mount : mounts_) {
    if (normalized == mount.prefix ||
        (normalized.size() > mount.prefix.size() &&
         normalized.compare(0, mount.prefix.size(), mount.prefix) == 0 &&
         normalized[mount.prefix.size()] == '/')) {
      return true;
    }
  }
  return false;
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
  const std::string mounted_path = normalize_virtual_path(path, current_directory_);
  for (const auto& mount : mounts_) {
    if (mounted_path == mount.prefix ||
        (mounted_path.size() > mount.prefix.size() &&
         mounted_path.compare(0, mount.prefix.size(), mount.prefix) == 0 &&
         mounted_path[mount.prefix.size()] == '/')) {
      out.fs = mount.filesystem.get();
      out.path = mounted_path;
      return true;
    }
  }
  out.fs = root_.get();
#if !defined(XLANG3_EMBEDDED)
  if (!host_paths_) {
    out.path = normalize_virtual_path(path, current_directory_);
    return true;
  }

#if defined(_WIN32)
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
          static_cast<int>(path.size()), nullptr, 0) <= 0) {
    const bool absolute = path.front() == '/' || path.front() == '\\' ||
        (path.size() >= 3 && path[1] == ':' &&
         (path[2] == '/' || path[2] == '\\'));
    if (absolute || current_directory_.empty()) {
      out.path = path;
    } else {
      out.path = current_directory_;
      if (!out.path.empty() && out.path.back() != '/' && out.path.back() != '\\') {
        out.path.push_back('\\');
      }
      out.path += path;
    }
    return true;
  }
#endif
  std::filesystem::path fs_path = filesystem_path(path);
  if (!fs_path.is_absolute() && !current_directory_.empty()) {
    fs_path = filesystem_path(current_directory_) / fs_path;
  }
  out.path = filesystem_path_text(fs_path.lexically_normal());
#else
  out.path = normalize_virtual_path(path, current_directory_);
#endif
  return true;
}

bool Vfs::read_file(const std::string& path, std::vector<uint8_t>& out, std::string& error) {
  ResolvedPath resolved;
  return resolve(path, resolved, error) && resolved.fs->read_file(resolved.path, out, error);
}

bool Vfs::read_link(const std::string& path, std::string& out, std::string& error) {
  ResolvedPath resolved;
  return resolve(path, resolved, error) && resolved.fs->read_link(resolved.path, out, error);
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

bool Vfs::directory_mtime(const std::string& path, int64_t& mtime_ns, std::string& error) {
  ResolvedPath resolved;
  if (!resolve(path, resolved, error)) return false;
  if (auto* os = dynamic_cast<OsFileSystem*>(resolved.fs)) {
    return os->directory_mtime(resolved.path, mtime_ns, error);
  }
  VfsStat info;
  if (!resolved.fs->stat(resolved.path, info, error) ||
      info.kind != VfsNodeKind::Directory) {
    return false;
  }
  mtime_ns = info.mtime_ns;
  return true;
}

bool Vfs::kind(const std::string& path, VfsNodeKind& out, std::string& error) {
  ResolvedPath resolved;
  return resolve(path, resolved, error) && resolved.fs->kind(resolved.path, out, error);
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
