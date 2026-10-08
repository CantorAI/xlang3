/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "xlang3/xlang3.h"
#include "xlang3/windows_file_path.h"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
namespace {
void require(bool condition, const char* reason) {
  if (!condition) throw std::runtime_error(reason);
}
std::string utf8(const std::wstring& text) {
  const auto bytes = std::filesystem::path(text).u8string();
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
std::wstring extended(const std::wstring& path) {
  require(path.size() >= 3 && path[1] == L':' && path[2] == L'\\',
          "Disposable file must have an absolute local drive path");
  return L"\\\\?\\" + path;
}
struct OwnedFiles {
  std::wstring root, previous_cwd;
  std::vector<std::wstring> directories, files;
  OwnedFiles() {
    const auto parent = std::filesystem::canonical(std::filesystem::temp_directory_path());
    root = (parent / ("xlang3-long-file-" + std::to_string(std::random_device{}()))).native();
    require(root.size() < 180, "Disposable temp parent must leave room for the exact 288-character case");
    require(CreateDirectoryW(root.c_str(), nullptr) != 0, "Disposable root must be newly created");
    directories.push_back(root);
    std::wstring current(32768, L'\0');
    const DWORD count = GetCurrentDirectoryW(static_cast<DWORD>(current.size()), current.data());
    require(count > 0 && count < current.size(), "Current directory capture failed");
    current.resize(count); previous_cwd = std::move(current);
    require(SetCurrentDirectoryW(root.c_str()) != 0, "Disposable working directory activation failed");
  }
  ~OwnedFiles() { if (!previous_cwd.empty()) SetCurrentDirectoryW(previous_cwd.c_str()); }
  bool contains(const std::wstring& path) const {
    const std::wstring prefix = root + L"\\";
    return path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
  }
  void own(const std::wstring& path) {
    require(contains(path), "File must remain inside the exact disposable root");
    files.push_back(path);
  }
  std::wstring file(size_t length, bool unicode = false) {
    std::wstring directory = root;
    if (unicode) {
      directory += L"\\\u4e2d\u6587-\U0001f30d";
      require(CreateDirectoryW(extended(directory).c_str(), nullptr) != 0,
              "Owned Unicode directory creation failed");
      directories.push_back(directory);
    }
    while (length > directory.size() + 1 + 200) {
      directory += L"\\" + std::wstring(64, L'd');
      if (std::find(directories.begin(), directories.end(), directory) == directories.end()) {
        require(CreateDirectoryW(extended(directory).c_str(), nullptr) != 0,
                "Owned long directory creation failed");
        directories.push_back(directory);
      }
    }
    require(length >= directory.size() + 7, "Requested owned path length is too short");
    const size_t leaf_size = length - directory.size() - 1;
    require(leaf_size <= 255, "Owned filename must respect the volume component limit");
    auto path = directory + L"\\f" + std::wstring(leaf_size - 5, L'x') + L".bin";
    require(path.size() == length, "Actual owned path length must be exact");
    own(path); return path;
  }
  void write_control(const std::wstring& path, const std::string& payload) {
    HANDLE file = CreateFileW(extended(path).c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(file != INVALID_HANDLE_VALUE, "Independent Win32 control file creation failed");
    DWORD bytes = 0;
    const bool written = WriteFile(file, payload.data(), static_cast<DWORD>(payload.size()), &bytes, nullptr) != 0;
    const bool closed = CloseHandle(file) != 0;
    require(written && bytes == payload.size() && closed, "Independent control bytes were not written exactly");
  }
  void retain_error(const std::string& error) noexcept {
    try {
      const auto path = root + L"\\private-native-error.log";
      HANDLE file = CreateFileW(extended(path).c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (file == INVALID_HANDLE_VALUE) return;
      const size_t size = error.size() < 16384 ? error.size() : 16384;
      DWORD written = 0; WriteFile(file, error.data(), static_cast<DWORD>(size), &written, nullptr);
      CloseHandle(file);
    } catch (...) {}
  }
  void cleanup() {
    require(SetCurrentDirectoryW(previous_cwd.c_str()) != 0, "Original working directory restoration failed");
    for (const auto& path : files) {
      require(contains(path), "Cleanup file escaped the owned root");
      if (!DeleteFileW(extended(path).c_str())) {
        require(GetLastError() == ERROR_FILE_NOT_FOUND, "Owned file cleanup failed");
      }
    }
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
      require(*it == root || contains(*it), "Cleanup directory escaped the owned root");
      require(RemoveDirectoryW(extended(*it).c_str()) != 0, "Owned directory cleanup failed");
    }
  }
};
void path_policy(const std::wstring& ordinary) {
  using xlang3::windows_native_file_path;
  require(windows_native_file_path(ordinary) == extended(ordinary), "Long local drive path must gain its extended prefix");
  const std::vector<std::wstring> unchanged{
      L"NUL", L"nul.txt", L"CONIN$", L"CONOUT$", L"COM1", L"LPT\u00b2",
      L"C:\\short\\file.bin", L"C:/short/file.bin", L"\\\\server\\share\\short.bin",
      extended(ordinary), L"\\\\.\\NUL", L"\\??\\C:\\already-native",
      ordinary + L".", ordinary + L" ",
      ordinary.substr(0, ordinary.find_last_of(L'\\')) + L"\\NUL.txt",
      ordinary.substr(0, ordinary.find_last_of(L'\\')) + L"\\COM\u00b9"};
  for (const auto& path : unchanged) require(windows_native_file_path(path) == path,
      "Short, reserved, trailing-dot/space and explicit namespaces must stay unchanged");
  auto slashes = ordinary; std::replace(slashes.begin(), slashes.end(), L'\\', L'/');
  require(windows_native_file_path(slashes) == extended(ordinary), "Long forward slashes must normalize before extension");
  const auto at = ordinary.find_last_of(L'\\');
  const auto dotted = ordinary.substr(0, at) + L"\\.\\unused\\.." + ordinary.substr(at);
  require(windows_native_file_path(dotted) == extended(ordinary), "Dot components must normalize before extension");
  const std::wstring unc = L"\\\\server\\share\\" + std::wstring(270, L'a');
  require(windows_native_file_path(unc) == L"\\\\?\\UNC\\" + unc.substr(2),
          "Long ordinary UNC paths must retain the server/share namespace");
  errno = EACCES; SetLastError(ERROR_ACCESS_DENIED);
  require(windows_native_file_path(ordinary) == extended(ordinary), "Error preservation probe must still normalize");
  require(errno == EACCES && GetLastError() == ERROR_ACCESS_DENIED, "Path preparation must not overwrite failed-open errors");
}
void runtime_file_contract(OwnedFiles& owned) {
  std::string payload = "Independent native control UTF-8 \xe4\xb8\xad";
  payload.push_back('\0'); payload += "end";
  const auto path288 = owned.file(288), path280 = owned.file(280), path260 = owned.file(260);
  const auto unicode = owned.file(516, true);
  for (const auto& path : {path288, path280, path260, unicode}) owned.write_control(path, payload);
  path_policy(path288); path_policy(unicode);
  const auto short_path = owned.root + L"\\short.bin"; owned.own(short_path); owned.write_control(short_path, payload);
  const auto created = owned.file(289), os_created = owned.file(290), missing = owned.file(291);
  X::Runtime runtime; auto globals = runtime.Dict(); auto paths = runtime.List();
  for (const auto& path : {path288, path280, path260, unicode, short_path, short_path + L".", short_path + L" "})
    require(paths.Append(X::Value(runtime, utf8(path))), "Native path list construction failed");
  auto forward = path288; std::replace(forward.begin(), forward.end(), L'\\', L'/');
  const auto at = path288.find_last_of(L'\\');
  const auto dotted = path288.substr(0, at) + L"\\.\\unused\\.." + path288.substr(at);
  for (const auto& path : {forward, dotted, path288.substr(owned.root.size() + 1), extended(path288)})
    require(paths.Append(X::Value(runtime, utf8(path))), "Native path form list construction failed");
  require(globals.Set("paths", paths) && globals.Set("payload", X::Value::Bytes(runtime.host(), payload.data(), payload.size())) &&
      globals.Set("created", X::Value(runtime, utf8(created))) && globals.Set("os_created", X::Value(runtime, utf8(os_created))) &&
      globals.Set("missing", X::Value(runtime, utf8(missing))), "Native fixture globals failed");
  const char* source = R"fixture(
import nt
for name in paths:
    with open(name, "rb") as stream:
        assert stream.name == name
        assert stream.read() == payload
    fd = nt.open(name, nt.O_RDONLY | nt.O_BINARY)
    try:
        assert nt.read(fd, 4096) == payload
        assert nt.get_inheritable(fd) is False
    finally:
        nt.close(fd)
with open(created, "xb") as stream:
    assert stream.write(payload) == len(payload)
try:
    open(created, "xb")
except FileExistsError as error:
    assert error.errno == 17 and error.filename == created
else:
    assert False
with open(created, "ab") as stream:
    assert stream.write(b"tail") == 4
with open(created, "rb") as stream:
    assert stream.read() == payload + b"tail"
fd = nt.open(os_created, nt.O_RDWR | nt.O_CREAT | nt.O_EXCL | nt.O_BINARY)
try:
    assert nt.write(fd, payload) == len(payload)
    assert nt.get_inheritable(fd) is False
finally:
    nt.close(fd)
try:
    nt.open(os_created, nt.O_WRONLY | nt.O_CREAT | nt.O_EXCL | nt.O_BINARY)
except FileExistsError as error:
    assert error.errno == 17 and error.filename == os_created
else:
    assert False
fd = nt.open(os_created, nt.O_WRONLY | nt.O_APPEND | nt.O_BINARY)
try:
    assert nt.write(fd, b"tail") == 4
finally:
    nt.close(fd)
seen = []
def opener(name, flags):
    seen.append(name)
    return nt.open(name, flags | nt.O_BINARY)
with open(os_created, "rb", opener=opener) as stream:
    assert stream.read() == payload + b"tail"
assert seen == [os_created]
for function in (lambda: open(missing, "rb"), lambda: nt.open(missing, nt.O_RDONLY)):
    try:
        function()
    except FileNotFoundError as error:
        assert error.errno == 2 and error.filename == missing
    else:
        assert False
with open("NUL", "wb") as stream:
    assert stream.write(b"device") == 6
fd = nt.open("NUL", nt.O_WRONLY | nt.O_BINARY)
try:
    assert nt.write(fd, b"device") == 6
finally:
    nt.close(fd)
assert len(paths) == 11
)fixture";
  X::Module builtins(runtime, "builtins"); X::Value ignored;
  if (!builtins["exec"].Call({X::Value(runtime, source), globals}, ignored)) {
    owned.retain_error(runtime.LastError());
    throw std::runtime_error("Native file open/descriptor/creation/append/error fixture failed");
  }
}
} // namespace
#endif

int main() {
  try {
#if defined(_WIN32)
    OwnedFiles owned; runtime_file_contract(owned); owned.cleanup();
    std::cout << "Native Windows ordinary long-file paths passed exact 260/280/288/516 character reads, UTF-8, relative/dot/UNC preparation, explicit namespace preservation, creation/exclusive/append/opener/errors and owned cleanup. No CPython execution.\n";
#else
    std::cout << "Windows-only native file-path contract; non-Windows open implementation unchanged.\n";
#endif
    return 0;
  } catch (const std::exception&) {
    std::cerr << "native_windows_file_path_contract_failed; disposable fixture retained\n";
    return 1;
  }
}
