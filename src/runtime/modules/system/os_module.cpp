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
#include "xlang3/builtins.h"
#include "xlang3/builtin_methods.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <sys/utime.h>
#include <windows.h>
#include <tlhelp32.h>
#include <sddl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

namespace xlang3 {

namespace {

constexpr const char* kScandirIteratorNativeType = "os.ScandirIterator";

#if defined(_WIN32)
void ignore_invalid_parameter(
    const wchar_t*,
    const wchar_t*,
    const wchar_t*,
    unsigned int,
    uintptr_t) {}

intptr_t safe_get_osfhandle(int fd) {
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
  const intptr_t handle = _get_osfhandle(fd);
  _set_thread_local_invalid_parameter_handler(previous);
  return handle;
}

int safe_fstat64(int fd, struct _stat64* result) {
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
  const int status = _fstat64(fd, result);
  _set_thread_local_invalid_parameter_handler(previous);
  return status;
}

int safe_close(int fd) {
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
  const int status = _close(fd);
  _set_thread_local_invalid_parameter_handler(previous);
  return status;
}

int safe_read(int fd, void* buffer, unsigned int count) {
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
  const int status = _read(fd, buffer, count);
  _set_thread_local_invalid_parameter_handler(previous);
  return status;
}

int safe_write(int fd, const void* buffer, unsigned int count) {
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
  const int status = _write(fd, buffer, count);
  _set_thread_local_invalid_parameter_handler(previous);
  return status;
}

__int64 safe_lseek64(int fd, __int64 offset, int origin) {
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
  const __int64 status = _lseeki64(fd, offset, origin);
  _set_thread_local_invalid_parameter_handler(previous);
  return status;
}

int safe_isatty(int fd) {
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
  const int status = _isatty(fd);
  _set_thread_local_invalid_parameter_handler(previous);
  return status;
}

int safe_dup(int fd) {
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
  const int status = _dup(fd);
  _set_thread_local_invalid_parameter_handler(previous);
  return status;
}

int safe_dup2(int fd, int fd2) {
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
  const int status = _dup2(fd, fd2);
  _set_thread_local_invalid_parameter_handler(previous);
  return status;
}
#endif

#if !defined(_WIN32)
char** process_environment() {
#if defined(__APPLE__)
  return *_NSGetEnviron();
#else
  return ::environ;
#endif
}
#endif

Value make_process_environ_dict() {
  std::vector<std::pair<Value, Value>> entries;
#if defined(_WIN32)
  LPCH block = GetEnvironmentStringsA();
  if (block != nullptr) {
    for (LPCCH current = block; current[0] != '\0'; current += std::strlen(current) + 1) {
      std::string_view item(current);
      const size_t equals = item.find('=');
      if (equals == std::string_view::npos || equals == 0) {
        continue;
      }
      entries.push_back({
          Value::string(std::string(item.substr(0, equals))),
          Value::string(std::string(item.substr(equals + 1)))});
    }
    FreeEnvironmentStringsA(block);
  }
#else
  char** environment = process_environment();
  if (environment != nullptr) {
    for (char** current = environment; *current != nullptr; ++current) {
      std::string_view item(*current);
      const size_t equals = item.find('=');
      if (equals == std::string_view::npos) {
        continue;
      }
      entries.push_back({
          Value::bytes(std::string(item.substr(0, equals))),
          Value::bytes(std::string(item.substr(equals + 1)))});
    }
  }
#endif
  return Value::dict(std::move(entries));
}

bool os_create_environ(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    error = "_create_environ() takes no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = make_process_environ_dict();
  return true;
}

struct PathArg {
  std::string text;
  bool bytes = false;
};

struct ScandirState {
  Runtime* runtime = nullptr;
  std::vector<Value> entries;
  size_t index = 0;
  bool closed = false;
};

struct OsModuleState {
  Value dir_entry_class;
  Value scandir_iterator_class;
  Value stat_result_class;
  Value terminal_size_class;
  Value times_result_class;
};

Value make_terminal_size(const Value& klass, int64_t columns, int64_t lines);
Value make_stat_result(const Value& klass, const VfsStat& stat);

void scandir_state_cleanup(void* data) {
  auto* state = static_cast<ScandirState*>(data);
  if (state != nullptr && state->runtime != nullptr && !state->closed &&
      state->index < state->entries.size()) {
    std::string ignored;
    Value warnings;
    Value warn;
    const Value* warning_class = state->runtime->find_builtin("ResourceWarning");
    if (warning_class != nullptr &&
        state->runtime->import_module("warnings", warnings, ignored) &&
        module_get_attr(warnings, "warn", warn, ignored)) {
      Value warning_args[] = {
          Value::string("unclosed scandir iterator"),
          *warning_class,
      };
      Value warning_result;
      (void)runtime_call_callable(
          *state->runtime, warn, warning_args, 2, warning_result, ignored);
    }
  }
  delete state;
}

void os_module_state_cleanup(void* data) {
  delete static_cast<OsModuleState*>(data);
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

bool get_path_arg(Runtime& runtime, const Value& value, const char* name, PathArg& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out.text = string_object_to_string(*str);
    out.bytes = false;
    if (out.text.find('\0') != std::string::npos) {
      error = "embedded null character in path";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    out.text = bytes_object_to_string(*bytes);
    out.bytes = true;
    if (out.text.find('\0') != std::string::npos) {
      error = "embedded null byte in path";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
#if defined(_WIN32)
    if (!out.text.empty() && MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, out.text.data(),
            static_cast<int>(out.text.size()), nullptr, 0) <= 0) {
      error = "path cannot be decoded using the filesystem encoding";
      runtime.raise_class_error("UnicodeDecodeError", error);
      return false;
    }
#endif
    return true;
  }
  std::string ignored;
  Value path_value;
  if ((object_get_attr(value, "_path", path_value, ignored) ||
       object_get_attr(value, "__xlang3_string_value__", path_value, ignored)) &&
      get_path_arg(runtime, path_value, name, out, error)) {
    return true;
  }

  Value fspath;
  if (object_get_attr(value, "__fspath__", fspath, ignored) &&
      fspath.tag != ValueTag::None) {
    Value result;
    std::string call_error;
    if (!runtime_call_callable(runtime, fspath, nullptr, 0, result, call_error)) {
      error = call_error.empty() ? std::string(name) + " __fspath__ failed" : call_error;
      return false;
    }
    if (value_as_string(result) != nullptr || value_as_bytes(result) != nullptr) {
      return get_path_arg(runtime, result, name, out, error);
    }
    error = "expected " + std::string(value_binary_type_name(value)) +
        ".__fspath__() to return str or bytes, not " +
        value_binary_type_name(result);
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  if (std::string(name) == "src" || std::string(name) == "dst") {
    error = std::string(name) + " should be string, bytes or os.PathLike, not " +
        value_binary_type_name(value);
  } else {
    error = std::string(name) + " must be str, bytes, or os.PathLike";
  }
  runtime.raise_class_error("TypeError", error);
  return false;
}

Value path_name_value(const std::string& text, bool bytes) {
  return bytes ? Value::bytes(text) : Value::string(text);
}

bool no_args(uint32_t argc, const char* name, std::string& error) {
  if (argc == 0) {
    return true;
  }
  error = std::string(name) + "() expected no arguments";
  return false;
}

bool raise_os_error_with_errno(
    Runtime& runtime,
    const char* class_name,
    int error_number,
    const std::string& message) {
  Value exception = runtime.make_exception(class_name, message);
  std::string ignored;
  object_set_attr(exception, "errno", Value::int64(error_number), ignored);
  object_set_attr(exception, "strerror", Value::string(message), ignored);
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool raise_path_not_found(
    Runtime& runtime,
    const std::string& error,
    const Value* filename = nullptr) {
  Value exception = runtime.make_exception("FileNotFoundError", error);
  std::string ignored;
  object_set_attr(exception, "errno", Value::int64(2), ignored);
#if defined(_WIN32)
  object_set_attr(exception, "winerror", Value::int64(2), ignored);
#endif
  object_set_attr(exception, "strerror", Value::string(error), ignored);
  if (filename != nullptr) {
    object_set_attr(exception, "filename", *filename, ignored);
  }
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool os_getcwd(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "os.getcwd", error)) {
    return false;
  }
  std::string cwd = runtime.vfs().cwd();
  while (cwd.size() > 1 && (cwd.back() == '/' || cwd.back() == '\\')) {
#if defined(_WIN32)
    if (cwd.size() == 3 && cwd[1] == ':' && (cwd[2] == '/' || cwd[2] == '\\')) {
      break;
    }
#endif
    cwd.pop_back();
  }
  out = Value::string(std::move(cwd));
  return true;
}

bool os_readlink(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.readlink() expected one path argument";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.readlink path", path, error)) {
    return false;
  }
  if (path.text.find('\0') != std::string::npos) {
    error = "embedded null character in path";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::string target;
  if (!runtime.vfs().read_link(path.text, target, error)) {
    return raise_path_not_found(runtime, error, &args[0]);
  }
  out = path_name_value(target, path.bytes);
  return true;
}

bool os_getcwdb(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "os.getcwdb() expected no arguments";
    return false;
  }
  out = Value::bytes(runtime.vfs().cwd());
  return true;
}

bool os_chdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.chdir() expected one argument";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.chdir path", path, error)) {
    return false;
  }
  if (!runtime.vfs().chdir(path.text, error)) {
    return raise_path_not_found(runtime, error, &args[0]);
  }
  value_set_none(out);
  return true;
}

bool os_fsencode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.fsencode() expected one argument";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.fsencode path", path, error)) {
    return false;
  }
  out = Value::bytes(path.text);
  return true;
}

bool os_fsdecode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.fsdecode() expected one argument";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.fsdecode path", path, error)) {
    return false;
  }
  out = Value::string(path.text);
  return true;
}

bool os_urandom(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "os.urandom() expected size";
    return false;
  }
  const int64_t requested = args[0].as.i64;
  if (requested < 0) {
    error = "negative argument not allowed";
    return false;
  }
  std::string bytes;
  bytes.resize(static_cast<size_t>(requested));
  std::random_device random;
  size_t offset = 0;
  while (offset < bytes.size()) {
    unsigned int random_value = random();
    for (size_t i = 0; i < sizeof(random_value) && offset < bytes.size(); ++i) {
      bytes[offset++] = static_cast<char>((random_value >> (i * 8)) & 0xffu);
    }
  }
  out = Value::bytes(std::move(bytes));
  return true;
}

bool os_getpid(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "os.getpid", error)) {
    return false;
  }
#if defined(_WIN32)
  value_set_int64(out, static_cast<int64_t>(_getpid()));
#else
  value_set_int64(out, static_cast<int64_t>(getpid()));
#endif
  return true;
}

bool os_getppid(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "os.getppid", error)) {
    return false;
  }
#if defined(_WIN32)
  DWORD parent = 0;
  const DWORD current = GetCurrentProcessId();
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot != INVALID_HANDLE_VALUE) {
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
      do {
        if (entry.th32ProcessID == current) {
          parent = entry.th32ParentProcessID;
          break;
        }
      } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
  }
  value_set_int64(out, static_cast<int64_t>(parent));
#else
  value_set_int64(out, static_cast<int64_t>(getppid()));
#endif
  return true;
}

bool os_open(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    runtime.raise_class_error("TypeError", "open() expected path, flags, optional mode and dir_fd");
    error = "open() expected path, flags, optional mode and dir_fd";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "open path", path, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[1].tag != ValueTag::Int64) {
    error = "open flags must be int";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int mode = 0666;
  if (argc >= 3 && args[2].tag != ValueTag::None) {
    if (args[2].tag != ValueTag::Int64) {
      error = "open mode must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    mode = static_cast<int>(args[2].as.i64);
  }
  if (argc >= 4 && args[3].tag != ValueTag::None) {
    error = "dir_fd is not supported yet";
    runtime.raise_class_error("NotImplementedError", error);
    return false;
  }

#if defined(_WIN32)
  const int fd = _wopen(
      std::filesystem::u8path(path.text).c_str(),
      static_cast<int>(args[1].as.i64) | _O_NOINHERIT,
      mode);
#else
  const int fd = ::open(path.text.c_str(), static_cast<int>(args[1].as.i64), static_cast<mode_t>(mode));
#endif
  if (fd < 0) {
    error = "open failed";
    return raise_path_not_found(runtime, error, &args[0]);
  }
  value_set_int64(out, fd);
  return true;
}

#if defined(_WIN32)
std::wstring os_utf8_to_wide(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0) return {};
  std::wstring result(static_cast<size_t>(size), L'\0');
  MultiByteToWideChar(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
  return result;
}

bool os_spawn_env_text(
    Runtime& runtime,
    const Value& value,
    const char* field,
    std::wstring& out,
    std::string& error) {
  if (auto* text = value_as_string(value)) {
    out = os_utf8_to_wide(string_object_to_string(*text));
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    out = os_utf8_to_wide(bytes_object_to_string(*bytes));
    return true;
  }
  error = std::string("spawnve() environment ") + field + " must be str or bytes";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool os_append_spawn_env_pair(
    Runtime& runtime,
    const Value& key,
    const Value& value,
    std::vector<std::wstring>& entries,
    std::string& error) {
  std::wstring key_text;
  std::wstring value_text;
  if (!os_spawn_env_text(runtime, key, "key", key_text, error) ||
      !os_spawn_env_text(runtime, value, "value", value_text, error)) {
    return false;
  }
  if (key_text.empty() || key_text.find(L'=') != std::wstring::npos ||
      key_text.find(L'\0') != std::wstring::npos ||
      value_text.find(L'\0') != std::wstring::npos) {
    error = "spawnve() environment contains an invalid variable";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  entries.push_back(std::move(key_text) + L"=" + std::move(value_text));
  return true;
}

bool os_append_spawn_env_item(
    Runtime& runtime,
    const Value& item,
    std::vector<std::wstring>& entries,
    std::string& error) {
  if (auto* tuple = value_as_tuple(item); tuple != nullptr && tuple->items.size() == 2) {
    return os_append_spawn_env_pair(runtime, tuple->items[0], tuple->items[1], entries, error);
  }
  if (auto* list = value_as_list(item); list != nullptr && list->items.size() == 2) {
    return os_append_spawn_env_pair(runtime, list->items[0], list->items[1], entries, error);
  }
  error = "spawnve() environment items must be key/value pairs";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool os_collect_spawn_env(
    Runtime& runtime,
    const Value& env,
    std::vector<std::wstring>& entries,
    std::string& error) {
  if (const auto* dict = value_as_dict(env)) {
    entries.reserve(dict->entries.size());
    for (const auto& item : dict->entries) {
      if (!os_append_spawn_env_pair(runtime, item.first, item.second, entries, error)) {
        return false;
      }
    }
    return true;
  }
  Value items_method;
  if (!object_get_attr(env, "items", items_method, error)) {
    error = "spawnve() environment must be a mapping";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value items_result;
  if (!runtime_call_callable(runtime, items_method, nullptr, 0, items_result, error)) {
    return false;
  }
  std::vector<Value> items;
  if (!runtime_collect_iterable(runtime, items_result, items, error)) {
    return false;
  }
  entries.reserve(items.size());
  for (const auto& item : items) {
    if (!os_append_spawn_env_item(runtime, item, entries, error)) {
      return false;
    }
  }
  return true;
}

bool os_spawn_windows_process(
    Runtime& runtime,
    int64_t mode,
    const std::wstring& path,
    const std::vector<std::wstring>& arguments,
    const std::vector<std::wstring>* environment,
    Value& out,
    std::string& error) {
  if (mode != _P_WAIT && mode != _P_NOWAIT && mode != _P_NOWAITO) {
    error = "spawn mode is not supported";
    runtime.raise_class_error("ValueError", error);
    return false;
  }

  std::wstring command_line;
  for (size_t index = 0; index < arguments.size(); ++index) {
    if (index != 0) command_line.push_back(L' ');
    command_line += arguments[index];
  }

  std::vector<wchar_t> environment_block;
  DWORD creation_flags = 0;
  if (environment != nullptr) {
    std::vector<std::wstring> sorted = *environment;
    std::sort(sorted.begin(), sorted.end());
    for (const auto& entry : sorted) {
      environment_block.insert(environment_block.end(), entry.begin(), entry.end());
      environment_block.push_back(L'\0');
    }
    environment_block.push_back(L'\0');
    if (sorted.empty()) environment_block.push_back(L'\0');
    creation_flags |= CREATE_UNICODE_ENVIRONMENT;
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(
          path.c_str(), command_line.data(), nullptr, nullptr, TRUE,
          creation_flags,
          environment == nullptr ? nullptr : environment_block.data(),
          nullptr, &startup, &process)) {
    const DWORD code = GetLastError();
    error = "spawn failed with Win32 error " + std::to_string(code);
    runtime.raise_class_error(
        code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
            ? "FileNotFoundError"
            : "OSError",
        error);
    return false;
  }
  CloseHandle(process.hThread);
  if (mode == _P_WAIT) {
    if (WaitForSingleObject(process.hProcess, INFINITE) == WAIT_FAILED) {
      CloseHandle(process.hProcess);
      error = "spawn wait failed";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
      CloseHandle(process.hProcess);
      error = "spawn could not read process status";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    CloseHandle(process.hProcess);
    out = Value::int64(static_cast<int64_t>(exit_code));
    return true;
  }
  out = Value::int64(static_cast<int64_t>(reinterpret_cast<intptr_t>(process.hProcess)));
  return true;
}

bool os_spawnv(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3 || args[0].tag != ValueTag::Int64) {
    error = "spawnv() expected mode, path and argv";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[1], "spawnv path", path, error)) {
    return false;
  }
  std::vector<Value> values;
  if (!runtime_collect_iterable(runtime, args[2], values, error)) {
    return false;
  }
  if (values.empty()) {
    error = "spawnv() argv must not be empty";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::vector<std::wstring> wide_args;
  wide_args.reserve(values.size());
  for (size_t index = 0; index < values.size(); ++index) {
    const auto& value = values[index];
    if (value_as_string(value) == nullptr && value_as_bytes(value) == nullptr) {
      Value fspath;
      std::string ignored;
      if (!object_get_attr(value, "__fspath__", fspath, ignored) || fspath.tag == ValueTag::None) {
        error = "spawnv() arg 3 must contain only strings";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    }
    PathArg argument;
    if (!get_path_arg(runtime, value, "spawnv argv item", argument, error)) {
      return false;
    }
    if (index == 0 && argument.text.empty()) {
      error = "spawnv() argv first element cannot be empty";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    wide_args.push_back(os_utf8_to_wide(argument.text));
  }
  std::vector<const wchar_t*> argv;
  argv.reserve(wide_args.size() + 1);
  for (const auto& argument : wide_args) argv.push_back(argument.c_str());
  argv.push_back(nullptr);
  const std::wstring wide_path = os_utf8_to_wide(path.text);
  return os_spawn_windows_process(
      runtime, args[0].as.i64, wide_path, wide_args, nullptr, out, error);
}

bool os_spawnve(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4 || args[0].tag != ValueTag::Int64) {
    error = "spawnve() expected mode, path, argv and env";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[1], "spawnve path", path, error)) {
    return false;
  }
  std::vector<Value> values;
  if (!runtime_collect_iterable(runtime, args[2], values, error)) {
    return false;
  }
  if (values.empty()) {
    error = "spawnve() argv must not be empty";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::vector<std::wstring> wide_args;
  wide_args.reserve(values.size());
  for (size_t index = 0; index < values.size(); ++index) {
    PathArg argument;
    if (!get_path_arg(runtime, values[index], "spawnve argv item", argument, error)) {
      return false;
    }
    if (index == 0 && argument.text.empty()) {
      error = "spawnve() argv first element cannot be empty";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    wide_args.push_back(os_utf8_to_wide(argument.text));
  }
  std::vector<const wchar_t*> argv;
  argv.reserve(wide_args.size() + 1);
  for (const auto& argument : wide_args) argv.push_back(argument.c_str());
  argv.push_back(nullptr);

  std::vector<std::wstring> wide_env;
  if (!os_collect_spawn_env(runtime, args[3], wide_env, error)) {
    return false;
  }
  std::vector<const wchar_t*> envp;
  envp.reserve(wide_env.size() + 1);
  for (const auto& entry : wide_env) envp.push_back(entry.c_str());
  envp.push_back(nullptr);

  const std::wstring wide_path = os_utf8_to_wide(path.text);
  return os_spawn_windows_process(
      runtime, args[0].as.i64, wide_path, wide_args, &wide_env, out, error);
}

bool os_waitpid(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64) {
    error = "waitpid() expected pid and options";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[1].as.i64 != 0) {
    error = "waitpid() options are not supported on Windows";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  HANDLE process = reinterpret_cast<HANDLE>(static_cast<intptr_t>(args[0].as.i64));
  if (WaitForSingleObject(process, INFINITE) == WAIT_FAILED) {
    error = "waitpid() failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  DWORD exit_code = 0;
  if (!GetExitCodeProcess(process, &exit_code)) {
    error = "waitpid() could not read process status";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  CloseHandle(process);
  out = Value::tuple({
      args[0],
      Value::int64(static_cast<int64_t>(exit_code) << 8),
  });
  return true;
}

bool os_waitstatus_to_exitcode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "waitstatus_to_exitcode() expected an integer status";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  constexpr int64_t maximum = static_cast<int64_t>(UINT32_MAX) << 8;
  if (args[0].as.i64 < 0) {
    error = "wait status is negative";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  if (args[0].as.i64 > maximum) {
    error = "wait status is too large";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::int64(args[0].as.i64 >> 8);
  return true;
}
#endif

bool os_open_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc > 4) {
    error = "open() expected at most 4 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> values = {
      Value::none(), Value::none(), Value::int64(0777), Value::none()};
  std::vector<bool> supplied(4, false);
  for (uint32_t i = 0; i < argc; ++i) {
    values[i] = args[i];
    supplied[i] = true;
  }
  static const char* names[] = {"path", "flags", "mode", "dir_fd"};
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].value == nullptr) {
      error = "open() received an invalid keyword value";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const std::string name = kwargs[i].name == nullptr ? std::string() : kwargs[i].name;
    size_t index = 4;
    for (size_t candidate = 0; candidate < 4; ++candidate) {
      if (name == names[candidate]) {
        index = candidate;
        break;
      }
    }
    if (index == 4) {
      error = "open() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (supplied[index]) {
      error = "open() got multiple values for argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    values[index] = *kwargs[i].value;
    supplied[index] = true;
  }
  if (!supplied[0] || !supplied[1]) {
    error = "open() missing required path or flags argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return os_open(runtime, values.data(), 4, out, error, user_data);
}

bool os_close(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "close() expected fd";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const int rc = safe_close(static_cast<int>(args[0].as.i64));
#else
  const int rc = ::close(static_cast<int>(args[0].as.i64));
#endif
  if (rc != 0) {
    error = "close failed";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  value_set_none(out);
  return true;
}

bool os_read(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64) {
    error = "read() expected fd and length";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[1].as.i64 < 0) {
    error = "negative read length";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::string buffer;
  buffer.resize(static_cast<size_t>(args[1].as.i64));
#if defined(_WIN32)
  const int count = safe_read(static_cast<int>(args[0].as.i64), buffer.data(), static_cast<unsigned int>(buffer.size()));
#else
  const ssize_t count = ::read(static_cast<int>(args[0].as.i64), buffer.data(), buffer.size());
#endif
  if (count < 0) {
    error = "read failed";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  buffer.resize(static_cast<size_t>(count));
  out = Value::bytes(std::move(buffer));
  return true;
}

bool os_readinto(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64) {
    error = "readinto() expected fd and writable buffer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  char* data = nullptr;
  size_t size = 0;
  if (auto* bytearray = value_as_bytearray(args[1])) {
    data = bytearray->value.data();
    size = bytearray->value.size();
  } else if (auto* view = value_as_memoryview(args[1])) {
    data = memoryview_object_writable_data(*view);
    size = view->size;
    if (data == nullptr && size != 0) {
      error = "readinto() argument must be read-write bytes-like object";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  } else {
    error = "readinto() argument must be read-write bytes-like object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const int count = safe_read(
      static_cast<int>(args[0].as.i64), data,
      static_cast<unsigned int>(std::min<size_t>(size, 0x7fffffffu)));
#else
  const ssize_t count = ::read(static_cast<int>(args[0].as.i64), data, size);
#endif
  if (count < 0) {
    error = "readinto failed";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  out = Value::int64(static_cast<int64_t>(count));
  return true;
}

bool os_write(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64) {
    error = "write() expected fd and bytes-like data";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string_view data;
  if (auto* bytes = value_as_bytes(args[1])) {
    data = bytes_object_view(*bytes);
  } else if (auto* bytearray = value_as_bytearray(args[1])) {
    data = std::string_view(bytearray->value.data(), bytearray->value.size());
  } else if (auto* view = value_as_memoryview(args[1])) {
    data = memoryview_object_view(*view);
    if (data.data() == nullptr) {
      error = "operation forbidden on invalid or released memoryview";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
  } else {
    error = "write data must be bytes-like";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const int count = safe_write(static_cast<int>(args[0].as.i64), data.data(), static_cast<unsigned int>(data.size()));
#else
  const ssize_t count = ::write(static_cast<int>(args[0].as.i64), data.data(), data.size());
#endif
  if (count < 0) {
    error = "write failed";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  value_set_int64(out, static_cast<int64_t>(count));
  return true;
}

bool os_lseek(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3 || args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64 || args[2].tag != ValueTag::Int64) {
    error = "lseek() expected fd, position, and how";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const auto position =
      safe_lseek64(static_cast<int>(args[0].as.i64), static_cast<__int64>(args[1].as.i64), static_cast<int>(args[2].as.i64));
  if (position < 0) {
#else
  const auto position =
      ::lseek(static_cast<int>(args[0].as.i64), static_cast<off_t>(args[1].as.i64), static_cast<int>(args[2].as.i64));
  if (position == static_cast<off_t>(-1)) {
#endif
    error = "lseek failed";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  value_set_int64(out, static_cast<int64_t>(position));
  return true;
}

bool os_fstat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "fstat() expected fd";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = static_cast<OsModuleState*>(user_data);
  if (state == nullptr) {
    error = "fstat() missing os module state";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  VfsStat stat;
#if defined(_WIN32)
  if (args[0].as.i64 < 0 || args[0].as.i64 > (std::numeric_limits<int>::max)()) {
    error = "bad file descriptor";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  struct _stat64 native_stat;
  if (safe_fstat64(static_cast<int>(args[0].as.i64), &native_stat) != 0) {
    error = "fstat failed";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  stat.kind = (native_stat.st_mode & _S_IFDIR) != 0 ? VfsNodeKind::Directory : VfsNodeKind::File;
  stat.size = static_cast<uint64_t>(native_stat.st_size);
  stat.inode = static_cast<uint64_t>(native_stat.st_ino);
  const intptr_t os_handle = safe_get_osfhandle(static_cast<int>(args[0].as.i64));
  BY_HANDLE_FILE_INFORMATION handle_info{};
  if (os_handle != -1 && GetFileInformationByHandle(
          reinterpret_cast<HANDLE>(os_handle), &handle_info) != 0) {
    stat.inode = (static_cast<uint64_t>(handle_info.nFileIndexHigh) << 32) |
        static_cast<uint64_t>(handle_info.nFileIndexLow);
  }
  stat.atime_ns = static_cast<int64_t>(native_stat.st_atime) * 1000000000LL;
  stat.mtime_ns = static_cast<int64_t>(native_stat.st_mtime) * 1000000000LL;
  stat.ctime_ns = static_cast<int64_t>(native_stat.st_ctime) * 1000000000LL;
#else
  struct stat native_stat;
  if (::fstat(static_cast<int>(args[0].as.i64), &native_stat) != 0) {
    error = "fstat failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  stat.kind = S_ISDIR(native_stat.st_mode) ? VfsNodeKind::Directory : VfsNodeKind::File;
  stat.size = static_cast<uint64_t>(native_stat.st_size);
  stat.inode = static_cast<uint64_t>(native_stat.st_ino);
  stat.atime_ns = static_cast<int64_t>(native_stat.st_atime) * 1000000000LL;
  stat.mtime_ns = static_cast<int64_t>(native_stat.st_mtime) * 1000000000LL;
  stat.ctime_ns = static_cast<int64_t>(native_stat.st_ctime) * 1000000000LL;
#endif
  out = make_stat_result(state->stat_result_class, stat);
  return true;
}

bool set_fd_inheritable(Runtime& runtime, int fd, bool inheritable, std::string& error) {
#if defined(_WIN32)
  const intptr_t os_handle = safe_get_osfhandle(fd);
  if (os_handle == -1) {
    error = "invalid file descriptor";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  const DWORD flags = inheritable ? HANDLE_FLAG_INHERIT : 0;
  if (SetHandleInformation(reinterpret_cast<HANDLE>(os_handle), HANDLE_FLAG_INHERIT, flags) == 0) {
    error = "set handle inheritance failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  return true;
#else
  const int current = fcntl(fd, F_GETFD);
  if (current < 0) {
    error = "get fd flags failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  const int next = inheritable ? (current & ~FD_CLOEXEC) : (current | FD_CLOEXEC);
  if (fcntl(fd, F_SETFD, next) < 0) {
    error = "set fd flags failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  return true;
#endif
}

bool os_dup(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "dup() expected fd";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const int fd = safe_dup(static_cast<int>(args[0].as.i64));
#else
  const int fd = ::dup(static_cast<int>(args[0].as.i64));
#endif
  if (fd < 0) {
    error = "dup failed";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  if (!set_fd_inheritable(runtime, fd, false, error)) {
#if defined(_WIN32)
    _close(fd);
#else
    ::close(fd);
#endif
    return false;
  }
  value_set_int64(out, fd);
  return true;
}

bool os_dup2(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3 || args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64) {
    error = "dup2() expected fd, fd2, and optional inheritable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int fd = static_cast<int>(args[0].as.i64);
  const int fd2 = static_cast<int>(args[1].as.i64);
  const bool inheritable = argc >= 3 ? value_truthy(args[2]) : true;
#if defined(_WIN32)
  const int rc = safe_dup2(fd, fd2);
#else
  const int rc = ::dup2(fd, fd2);
#endif
  if (rc != 0) {
    error = "dup2 failed";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  if (!set_fd_inheritable(runtime, fd2, inheritable, error)) {
    return false;
  }
  value_set_int64(out, fd2);
  return true;
}

bool os_dup2_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 2 || argc > 3 || kwargc > 1) {
    error = "dup2() expected fd, fd2, and optional inheritable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> values(args, args + argc);
  if (kwargc == 1) {
    if (kwargs[0].name == nullptr || std::string(kwargs[0].name) != "inheritable" ||
        kwargs[0].value == nullptr) {
      error = "dup2() got an unexpected keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (argc == 3) {
      error = "dup2() got multiple values for argument 'inheritable'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    values.push_back(*kwargs[0].value);
  }
  return os_dup2(
      runtime, values.data(), static_cast<uint32_t>(values.size()),
      out, error, user_data);
}

bool os_pipe(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "pipe", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int fds[2] = {-1, -1};
#if defined(_WIN32)
  if (_pipe(fds, 0, _O_BINARY | _O_NOINHERIT) != 0) {
#else
  if (::pipe(fds) != 0) {
#endif
    error = "pipe failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
#if !defined(_WIN32)
  if (!set_fd_inheritable(runtime, fds[0], false, error) || !set_fd_inheritable(runtime, fds[1], false, error)) {
    ::close(fds[0]);
    ::close(fds[1]);
    return false;
  }
#endif
  out = Value::tuple({Value::int64(fds[0]), Value::int64(fds[1])});
  return true;
}

bool os_isatty(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "isatty() expected fd";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  value_set_bool(out, safe_isatty(static_cast<int>(args[0].as.i64)) != 0);
#else
  value_set_bool(out, ::isatty(static_cast<int>(args[0].as.i64)) != 0);
#endif
  return true;
}

bool os_get_inheritable(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "get_inheritable() expected fd";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const intptr_t os_handle = safe_get_osfhandle(static_cast<int>(args[0].as.i64));
  if (os_handle == -1) {
    error = "invalid file descriptor";
    return raise_os_error_with_errno(runtime, "OSError", 9, error);
  }
  DWORD flags = 0;
  if (GetHandleInformation(reinterpret_cast<HANDLE>(os_handle), &flags) == 0) {
    error = "get handle inheritance failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_bool(out, (flags & HANDLE_FLAG_INHERIT) != 0);
#else
  const int flags = fcntl(static_cast<int>(args[0].as.i64), F_GETFD);
  if (flags < 0) {
    error = "get fd flags failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_bool(out, (flags & FD_CLOEXEC) == 0);
#endif
  return true;
}

bool os_set_inheritable(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64) {
    error = "set_inheritable() expected fd and inheritable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!set_fd_inheritable(runtime, static_cast<int>(args[0].as.i64), value_truthy(args[1]), error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool os_cpu_count(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "os.cpu_count", error)) {
    return false;
  }
  unsigned count = std::thread::hardware_concurrency();
  if (count == 0) {
    out = Value::none();
  } else {
    value_set_int64(out, static_cast<int64_t>(count));
  }
  return true;
}

bool os_get_terminal_size(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc > 1) {
    error = "os.get_terminal_size() expected optional fd";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t fd = 1;
  if (argc == 1) {
    if (args[0].tag != ValueTag::Int64) {
      error = "os.get_terminal_size() fd must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    fd = args[0].as.i64;
  }
  auto* state = static_cast<OsModuleState*>(user_data);
#if defined(_WIN32)
  intptr_t os_handle = _get_osfhandle(static_cast<int>(fd));
  if (os_handle == -1) {
    error = "bad file descriptor";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  CONSOLE_SCREEN_BUFFER_INFO info{};
  if (!GetConsoleScreenBufferInfo(reinterpret_cast<HANDLE>(os_handle), &info)) {
    out = make_terminal_size(state->terminal_size_class, 0, 0);
    return true;
  }
  const int64_t columns = static_cast<int64_t>(info.srWindow.Right - info.srWindow.Left + 1);
  const int64_t lines = static_cast<int64_t>(info.srWindow.Bottom - info.srWindow.Top + 1);
  out = make_terminal_size(state->terminal_size_class, columns, lines);
  return true;
#else
  (void)state;
  error = "terminal size query is not implemented for this platform";
  runtime.raise_class_error("OSError", error);
  return false;
#endif
}

bool os_exit(Runtime&, const Value* args, uint32_t argc, Value&, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "os._exit() expected integer status";
    return false;
  }
#if defined(_WIN32)
  _exit(static_cast<int>(args[0].as.i64));
#else
  _exit(static_cast<int>(args[0].as.i64));
#endif
}

bool os_listdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "os.listdir() expected at most one argument";
    return false;
  }
  PathArg path;
  path.text = ".";
  if (argc == 1) {
    if (!get_path_arg(runtime, args[0], "os.listdir path", path, error)) {
      return false;
    }
  }
  std::vector<std::string> names;
  if (!runtime.vfs().list_dir(path.text, names, error)) {
    return raise_path_not_found(runtime, error, argc == 1 ? &args[0] : nullptr);
  }
  std::vector<Value> values;
  values.reserve(names.size());
  for (auto& name : names) {
    values.push_back(path_name_value(name, path.bytes));
  }
  out = Value::list(std::move(values));
  return true;
}

std::string dir_entry_path(const Value& self) {
  Value path;
  std::string ignored;
  if (object_get_attr(self, "path", path, ignored)) {
    if (auto* text = value_as_string(path)) {
      return string_object_to_string(*text);
    }
    if (auto* bytes = value_as_bytes(path)) {
      return bytes_object_to_string(*bytes);
    }
    if (auto* bytearray = value_as_bytearray(path)) {
      return bytearray->value;
    }
  }
  return {};
}

std::string stat_result_field_repr(const Value& value) {
  if (value.tag == ValueTag::None) {
    return "None";
  }
  if (value.tag == ValueTag::Bool) {
    return value.as.b ? "True" : "False";
  }
  if (value.tag == ValueTag::Int64) {
    return std::to_string(value.as.i64);
  }
  if (value.tag == ValueTag::Double) {
    std::ostringstream stream;
    stream << value.as.f64;
    return stream.str();
  }
  if (auto* string = value_as_string(value)) {
    return "'" + string_object_to_string(*string) + "'";
  }
  return value_to_string(value);
}

bool os_stat_result_repr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "descriptor '__repr__' of 'os.stat_result' object needs an argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc != 1) {
    error = "expected 0 arguments, got " + std::to_string(argc - 1);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value stored;
  std::string ignored;
  if (object_get_attr(args[0], "__xlang3_string_value__", stored, ignored)) {
    out = stored;
    return true;
  }
  Value tuple_value;
  TupleObject* tuple = nullptr;
  if (!object_get_attr(args[0], "_tuple", tuple_value, ignored) || (tuple = value_as_tuple(tuple_value)) == nullptr) {
    error = "descriptor '__repr__' requires a 'os.stat_result' object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  static const char* names[] = {
      "st_mode",
      "st_ino",
      "st_dev",
      "st_nlink",
      "st_uid",
      "st_gid",
      "st_size",
      "st_atime",
      "st_mtime",
      "st_ctime",
  };
  std::string text = "os.stat_result(";
  for (size_t i = 0; i < 10 && i < tuple->items.size(); ++i) {
    if (i != 0) {
      text += ", ";
    }
    text += names[i];
    text += "=";
    text += stat_result_field_repr(tuple->items[i]);
  }
  text += ")";
  out = Value::string(std::move(text));
  return true;
}

bool os_stat_result_repr_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t kwargc,
    Value&,
    std::string& error,
    void*) {
  if (kwargc == 0) {
    return true;
  }
  error = "wrapper __repr__() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
}

Value stat_result_match_args() {
  return Value::tuple({
      Value::string("st_mode"),
      Value::string("st_ino"),
      Value::string("st_dev"),
      Value::string("st_nlink"),
      Value::string("st_uid"),
      Value::string("st_gid"),
      Value::string("st_size"),
  });
}

bool immutable_structseq_setattr(
    Runtime& runtime,
    const Value*,
    uint32_t argc,
    Value&,
    std::string& error,
    void*) {
  if (argc != 3) {
    error = "__setattr__ expected name and value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  error = "readonly attribute";
  runtime.raise_class_error("AttributeError", error);
  return false;
}

bool os_stat_result_new(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2 || value_as_class(args[0]) == nullptr || value_as_tuple(args[1]) == nullptr) {
    error = "os.stat_result() expected a sequence of at least 10 items";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* items = value_as_tuple(args[1]);
  if (items->items.size() < 10) {
    error = "os.stat_result() takes an at least 10-sequence (" +
        std::to_string(items->items.size()) + "-sequence given)";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::instance(args[0]);
  value_as_instance(out)->attrs.push_back({"_tuple", args[1]});
  return true;
}

bool os_stat_result_reduce(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    error = "os.stat_result.__reduce_ex__ expected optional protocol";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value klass;
  Value tuple;
  if (!object_get_attr(args[0], "__class__", klass, error) ||
      !object_get_attr(args[0], "_tuple", tuple, error)) {
    runtime.raise_class_error("TypeError", "invalid os.stat_result object");
    return false;
  }
  out = Value::tuple({klass, Value::tuple({tuple})});
  return true;
}

bool os_stat_result_equal(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 2) {
    error = "os.stat_result comparison expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value left;
  Value right;
  std::string ignored;
  if (!object_get_attr(args[0], "_tuple", left, ignored)) {
    value_set_bool(out, false);
    return true;
  }
  if (value_as_tuple(args[1]) != nullptr) {
    value_assign_fast(right, args[1]);
  } else if (!object_get_attr(args[1], "_tuple", right, ignored)) {
    value_set_bool(out, reinterpret_cast<intptr_t>(user_data) != 0);
    return true;
  }
  Value equal;
  if (!runtime_value_compare(runtime, "==", left, right, equal, error)) {
    return false;
  }
  const bool is_equal = value_truthy(equal);
  value_set_bool(out, reinterpret_cast<intptr_t>(user_data) != 0 ? !is_equal : is_equal);
  return true;
}

Value make_stat_result_class(Runtime& runtime) {
  const Value* tuple_base = runtime.find_builtin("tuple");
  Value result = Value::class_object(
      "stat_result",
      {
          {"__module__", Value::string("os")},
          {"__qualname__", Value::string("stat_result")},
          {"__new__", runtime.make_native_function("os.stat_result.__new__", os_stat_result_new)},
          {"__reduce__", runtime.make_native_function("os.stat_result.__reduce__", os_stat_result_reduce)},
          {"__reduce_ex__", runtime.make_native_function("os.stat_result.__reduce_ex__", os_stat_result_reduce)},
          {"__eq__", runtime.make_native_function("os.stat_result.__eq__", os_stat_result_equal)},
          {"__ne__", runtime.make_native_function("os.stat_result.__ne__", os_stat_result_equal, reinterpret_cast<void*>(1))},
          {"__repr__", runtime.make_native_function("os.stat_result.__repr__", os_stat_result_repr, nullptr, nullptr, nullptr, false, os_stat_result_repr_kw)},
          {"__str__", runtime.make_native_function("os.stat_result.__str__", os_stat_result_repr, nullptr, nullptr, nullptr, false, os_stat_result_repr_kw)},
          {"__setattr__", runtime.make_native_function("os.stat_result.__setattr__", immutable_structseq_setattr)},
          {"n_sequence_fields", Value::int64(10)},
          {"n_fields", Value::int64(20)},
          {"n_unnamed_fields", Value::int64(3)},
          {"st_mode", slot_descriptor("os.stat_result", "st_mode", 0)},
          {"st_ino", slot_descriptor("os.stat_result", "st_ino", 1)},
          {"st_dev", slot_descriptor("os.stat_result", "st_dev", 2)},
          {"st_nlink", slot_descriptor("os.stat_result", "st_nlink", 3)},
          {"st_uid", slot_descriptor("os.stat_result", "st_uid", 4)},
          {"st_gid", slot_descriptor("os.stat_result", "st_gid", 5)},
          {"st_size", slot_descriptor("os.stat_result", "st_size", 6)},
          {"st_atime", slot_descriptor("os.stat_result", "st_atime", 7)},
          {"st_mtime", slot_descriptor("os.stat_result", "st_mtime", 8)},
          {"st_ctime", slot_descriptor("os.stat_result", "st_ctime", 9)},
          {"st_atime_ns", Value::int64(0)},
          {"st_mtime_ns", Value::int64(0)},
          {"st_ctime_ns", Value::int64(0)},
          {"st_birthtime", Value::none()},
          {"st_birthtime_ns", Value::none()},
          {"st_file_attributes", Value::int64(0)},
          {"st_reparse_tag", Value::int64(0)},
          {"__match_args__", stat_result_match_args()},
      },
      tuple_base != nullptr ? *tuple_base : Value::invalid());
  if (auto* klass = value_as_class(result)) {
    klass->restrict_instance_attrs = true;
    klass->allow_instance_dict = false;
    klass->has_setattr_hook = true;
  }
  return result;
}

Value make_stat_result(const Value& klass, const VfsStat& stat) {
  const int64_t mode = stat.is_block_device
      ? 0060000
      : stat.is_character_device
      ? 0020000
      : stat.is_symlink
      ? 0120000
      : stat.kind == VfsNodeKind::Directory ? 0040000 : stat.kind == VfsNodeKind::File ? 0100000 : 0;
  const double atime = static_cast<double>(stat.atime_ns) / 1000000000.0;
  const double mtime = static_cast<double>(stat.mtime_ns) / 1000000000.0;
  const double ctime = static_cast<double>(stat.ctime_ns) / 1000000000.0;
  std::vector<Value> tuple_items = {
      Value::int64(mode),
      stat.inode <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())
          ? Value::int64(static_cast<int64_t>(stat.inode))
          : value_bigint_from_u64(stat.inode),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(static_cast<int64_t>(stat.size)),
      Value::int64(stat.atime_ns / 1000000000LL),
      Value::int64(stat.mtime_ns / 1000000000LL),
      Value::int64(stat.ctime_ns / 1000000000LL),
  };

  Value instance = Value::instance(klass);
  auto* object = value_as_instance(instance);
  object->attrs.push_back({"_tuple", Value::tuple(tuple_items)});
  object->attrs.push_back({"st_atime", Value::number(atime)});
  object->attrs.push_back({"st_mtime", Value::number(mtime)});
  object->attrs.push_back({"st_ctime", Value::number(ctime)});
  object->attrs.push_back({"st_atime_ns", Value::int64(stat.atime_ns)});
  object->attrs.push_back({"st_mtime_ns", Value::int64(stat.mtime_ns)});
  object->attrs.push_back({"st_ctime_ns", Value::int64(stat.ctime_ns)});
  object->attrs.push_back({"st_birthtime", Value::number(ctime)});
  object->attrs.push_back({"st_birthtime_ns", Value::int64(stat.ctime_ns)});
  object->attrs.push_back({
      "st_file_attributes",
      Value::int64(static_cast<int64_t>(stat.file_attributes))});
  object->attrs.push_back({
      "st_reparse_tag",
      Value::int64(static_cast<int64_t>(stat.reparse_tag))});
  return instance;
}

Value terminal_size_match_args() {
  return Value::tuple({
      Value::string("columns"),
      Value::string("lines"),
  });
}

Value make_terminal_size_class(Runtime& runtime) {
  const Value* tuple_base = runtime.find_builtin("tuple");
  Value result = Value::class_object(
      "terminal_size",
      {
          {"__module__", Value::string("os")},
          {"__qualname__", Value::string("terminal_size")},
          {"__setattr__", runtime.make_native_function("os.terminal_size.__setattr__", immutable_structseq_setattr)},
          {"n_sequence_fields", Value::int64(2)},
          {"n_fields", Value::int64(2)},
          {"n_unnamed_fields", Value::int64(0)},
          {"columns", slot_descriptor("os.terminal_size", "columns", 0)},
          {"lines", slot_descriptor("os.terminal_size", "lines", 1)},
          {"__match_args__", terminal_size_match_args()},
      },
      tuple_base != nullptr ? *tuple_base : Value::invalid());
  if (auto* klass = value_as_class(result)) {
    klass->restrict_instance_attrs = true;
    klass->allow_instance_dict = false;
    klass->has_setattr_hook = true;
  }
  return result;
}

Value make_terminal_size(const Value& klass, int64_t columns, int64_t lines) {
  Value instance = Value::instance(klass);
  value_as_instance(instance)->attrs.push_back({
      "_tuple", Value::tuple({Value::int64(columns), Value::int64(lines)})});
  return instance;
}

Value make_times_result_class(Runtime& runtime) {
  const Value* tuple_base = runtime.find_builtin("tuple");
  Value result = Value::class_object(
      "times_result",
      {
          {"__module__", Value::string("os")},
          {"__qualname__", Value::string("times_result")},
          {"__setattr__", runtime.make_native_function("os.times_result.__setattr__", immutable_structseq_setattr)},
          {"n_sequence_fields", Value::int64(5)},
          {"n_fields", Value::int64(5)},
          {"n_unnamed_fields", Value::int64(0)},
          {"user", slot_descriptor("os.times_result", "user", 0)},
          {"system", slot_descriptor("os.times_result", "system", 1)},
          {"children_user", slot_descriptor("os.times_result", "children_user", 2)},
          {"children_system", slot_descriptor("os.times_result", "children_system", 3)},
          {"elapsed", slot_descriptor("os.times_result", "elapsed", 4)},
      },
      tuple_base != nullptr ? *tuple_base : Value::invalid());
  if (auto* klass = value_as_class(result)) {
    klass->restrict_instance_attrs = true;
    klass->allow_instance_dict = false;
    klass->has_setattr_hook = true;
  }
  return result;
}

bool os_times(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (!no_args(argc, "os.times", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = static_cast<OsModuleState*>(user_data);
  if (state == nullptr) {
    error = "os.times module state is missing";
    return false;
  }
  double user = 0.0;
  double system = 0.0;
#if defined(_WIN32)
  FILETIME creation{};
  FILETIME exit{};
  FILETIME kernel{};
  FILETIME user_time{};
  if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user_time)) {
    error = "GetProcessTimes failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  const auto seconds = [](const FILETIME& value) {
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return static_cast<double>(ticks.QuadPart) / 10000000.0;
  };
  user = seconds(user_time);
  system = seconds(kernel);
#endif
  out = Value::instance(state->times_result_class);
  value_as_instance(out)->attrs.push_back({
      "_tuple",
      Value::tuple({
          Value::number(user),
          Value::number(system),
          Value::number(0.0),
          Value::number(0.0),
          Value::number(0.0),
      })});
  return true;
}

bool dir_entry_is_dir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "DirEntry.is_dir() expected optional follow_symlinks";
    return false;
  }
  if (!object_get_attr(args[0], "_cached_is_dir", out, error)) {
    error = "invalid DirEntry object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool accept_follow_symlinks_kw(
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    std::string& error) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "DirEntry method got invalid keyword argument";
      return false;
    }
    if (std::string(kwargs[i].name) != "follow_symlinks") {
      error = "DirEntry method got unexpected keyword argument '" + std::string(kwargs[i].name) + "'";
      return false;
    }
  }
  return true;
}

bool dir_entry_is_dir_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  return accept_follow_symlinks_kw(kwargs, kwargc, error) &&
         dir_entry_is_dir(runtime, args, argc, out, error, user_data);
}

bool dir_entry_is_file(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "DirEntry.is_file() expected optional follow_symlinks";
    return false;
  }
  if (!object_get_attr(args[0], "_cached_is_file", out, error)) {
    error = "invalid DirEntry object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool dir_entry_is_file_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  return accept_follow_symlinks_kw(kwargs, kwargc, error) &&
         dir_entry_is_file(runtime, args, argc, out, error, user_data);
}

bool dir_entry_is_symlink(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "DirEntry.is_symlink() expected no arguments";
    return false;
  }
  if (!object_get_attr(args[0], "_cached_is_symlink", out, error)) {
    error = "invalid DirEntry object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool dir_entry_is_junction(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "DirEntry.is_junction() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const auto path = std::filesystem::u8path(dir_entry_path(args[0]));
  HANDLE handle = CreateFileW(
      path.c_str(), 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    value_set_bool(out, false);
    return true;
  }
  FILE_ATTRIBUTE_TAG_INFO info{};
  const bool is_junction =
      GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) != 0 &&
      (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
      info.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT;
  CloseHandle(handle);
  value_set_bool(out, is_junction);
#else
  value_set_bool(out, false);
#endif
  return true;
}

bool dir_entry_stat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "DirEntry.stat() expected optional follow_symlinks";
    return false;
  }
  if (!object_get_attr(args[0], "_cached_stat", out, error)) {
    error = "invalid DirEntry object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool dir_entry_stat_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  return accept_follow_symlinks_kw(kwargs, kwargc, error) &&
         dir_entry_stat(runtime, args, argc, out, error, user_data);
}

bool dir_entry_inode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "DirEntry.inode() expected no arguments";
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(dir_entry_path(args[0]), stat, error) ||
      stat.kind == VfsNodeKind::Missing) {
    if (error.empty()) {
      error = "file not found: " + dir_entry_path(args[0]);
    }
    return raise_path_not_found(runtime, error);
  }
  out = stat.inode <= static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())
      ? Value::int64(static_cast<int64_t>(stat.inode))
      : value_bigint_from_u64(stat.inode);
  return true;
}

bool dir_entry_fspath(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || !object_get_attr(args[0], "path", out, error)) {
    error = "DirEntry.__fspath__() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool dir_entry_repr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value name;
  if (argc != 1 || !object_get_attr(args[0], "name", name, error)) {
    error = "DirEntry.__repr__() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::string("<DirEntry " + value_to_repr(name) + ">");
  return true;
}

bool native_os_type_new(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void* user_data) {
  const auto* type_name = static_cast<const char*>(user_data);
  error = "cannot create '" + std::string(type_name == nullptr ? "os object" : type_name) + "' instances";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool native_os_type_reduce(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void* user_data) {
  const auto* type_name = static_cast<const char*>(user_data);
  error = "cannot pickle '" + std::string(type_name == nullptr ? "os object" : type_name) + "' object";
  runtime.raise_class_error("TypeError", error);
  return false;
}

Value make_dir_entry_class(Runtime& runtime, OsModuleState* os_state) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("os")});
  attrs.push_back({"__new__", runtime.make_native_function("os.DirEntry.__new__", native_os_type_new, const_cast<char*>("os.DirEntry"))});
  attrs.push_back({"__fspath__", runtime.make_native_function("os.DirEntry.__fspath__", dir_entry_fspath)});
  attrs.push_back({"__repr__", runtime.make_native_function("os.DirEntry.__repr__", dir_entry_repr)});
  attrs.push_back({"__reduce__", runtime.make_native_function("os.DirEntry.__reduce__", native_os_type_reduce, const_cast<char*>("os.DirEntry"))});
  attrs.push_back({"__reduce_ex__", runtime.make_native_function("os.DirEntry.__reduce_ex__", native_os_type_reduce, const_cast<char*>("os.DirEntry"))});
  attrs.push_back({"inode", runtime.make_native_function("os.DirEntry.inode", dir_entry_inode)});
  attrs.push_back({"is_dir", runtime.make_native_function("os.DirEntry.is_dir", dir_entry_is_dir, nullptr, nullptr, nullptr, false, dir_entry_is_dir_kw)});
  attrs.push_back({"is_file", runtime.make_native_function("os.DirEntry.is_file", dir_entry_is_file, nullptr, nullptr, nullptr, false, dir_entry_is_file_kw)});
  attrs.push_back({"is_symlink", runtime.make_native_function("os.DirEntry.is_symlink", dir_entry_is_symlink)});
  attrs.push_back({"is_junction", runtime.make_native_function("os.DirEntry.is_junction", dir_entry_is_junction)});
  attrs.push_back({"stat", runtime.make_native_function("os.DirEntry.stat", dir_entry_stat, os_state, nullptr, nullptr, false, dir_entry_stat_kw)});
  return Value::class_object("DirEntry", std::move(attrs));
}

std::string join_vfs_path(const std::string& base, const std::string& name) {
  if (base.empty() || base == ".") {
    return name;
  }
  const char tail = base.back();
  if (tail == '/' || tail == '\\') {
    return base + name;
  }
#if defined(_WIN32)
  return base + "\\" + name;
#else
  return base + "/" + name;
#endif
}

Value make_dir_entry(
    const Value& klass,
    const Value& stat_result_class,
    const VfsStat& stat,
    std::string path,
    std::string name,
    bool bytes) {
  Value instance = Value::instance(klass);
  std::string ignored;
  object_set_attr(instance, "path", path_name_value(path, bytes), ignored);
  object_set_attr(instance, "name", path_name_value(name, bytes), ignored);
  object_set_attr(instance, "_cached_is_dir", Value::boolean(stat.kind == VfsNodeKind::Directory), ignored);
  object_set_attr(instance, "_cached_is_file", Value::boolean(stat.kind == VfsNodeKind::File), ignored);
  object_set_attr(instance, "_cached_is_symlink", Value::boolean(stat.is_symlink), ignored);
  object_set_attr(instance, "_cached_stat", make_stat_result(stat_result_class, stat), ignored);
  return instance;
}

ScandirState* scandir_state(const Value& self, std::string& error) {
  auto* state = static_cast<ScandirState*>(instance_get_native_data(self, kScandirIteratorNativeType));
  if (state == nullptr) {
    error = "invalid scandir iterator";
  }
  return state;
}

bool scandir_iter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ScandirIterator.__iter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool scandir_next(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ScandirIterator.__next__() expected no arguments";
    return false;
  }
  auto* state = scandir_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->closed || state->index >= state->entries.size()) {
    state->closed = true;
    runtime.raise_class_error("StopIteration", "");
    return false;
  }
  value_assign_fast(out, state->entries[state->index++]);
  return true;
}

bool scandir_close(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ScandirIterator.close() expected no arguments";
    return false;
  }
  auto* state = scandir_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->closed = true;
  state->entries.clear();
  value_set_none(out);
  return true;
}

bool scandir_enter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ScandirIterator.__enter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool scandir_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 4) {
    error = "ScandirIterator.__exit__() expected exc_type, exc, traceback";
    return false;
  }
  if (!scandir_close(runtime, args, 1, out, error, user_data)) {
    return false;
  }
  value_set_bool(out, false);
  return true;
}

Value make_scandir_iterator_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("os")});
  attrs.push_back({"__new__", runtime.make_native_function("os.ScandirIterator.__new__", native_os_type_new, const_cast<char*>("posix.ScandirIterator"))});
  attrs.push_back({"__reduce__", runtime.make_native_function("os.ScandirIterator.__reduce__", native_os_type_reduce, const_cast<char*>("posix.ScandirIterator"))});
  attrs.push_back({"__reduce_ex__", runtime.make_native_function("os.ScandirIterator.__reduce_ex__", native_os_type_reduce, const_cast<char*>("posix.ScandirIterator"))});
  attrs.push_back({"__iter__", runtime.make_native_function("os.ScandirIterator.__iter__", scandir_iter)});
  attrs.push_back({"__next__", runtime.make_native_function("os.ScandirIterator.__next__", scandir_next)});
  attrs.push_back({"close", runtime.make_native_function("os.ScandirIterator.close", scandir_close)});
  attrs.push_back({"__enter__", runtime.make_native_function("os.ScandirIterator.__enter__", scandir_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("os.ScandirIterator.__exit__", scandir_exit)});
  return Value::class_object("ScandirIterator", std::move(attrs));
}

bool os_scandir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc > 1) {
    error = "os.scandir() expected at most one argument";
    return false;
  }
  PathArg path;
  path.text = ".";
  if (argc == 1) {
    if (!get_path_arg(runtime, args[0], "os.scandir path", path, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        runtime.set_pending_exception(std::move(pending));
      } else {
        runtime.raise_class_error("TypeError", error);
      }
      return false;
    }
  }
  std::vector<std::string> names;
  if (!runtime.vfs().list_dir(path.text, names, error)) {
    return raise_path_not_found(runtime, error);
  }
  const auto* module_state = static_cast<OsModuleState*>(user_data);
  if (module_state == nullptr) {
    error = "os.scandir module state is missing";
    return false;
  }
  auto* state = new ScandirState();
  state->runtime = &runtime;
  state->entries.reserve(names.size());
  for (auto& name : names) {
    const std::string full_path = join_vfs_path(path.text, name);
    VfsStat stat;
    if (!runtime.vfs().stat(full_path, stat, error) || stat.kind == VfsNodeKind::Missing) {
      continue;
    }
    state->entries.push_back(make_dir_entry(
        module_state->dir_entry_class,
        module_state->stat_result_class,
        stat,
        full_path,
        std::move(name),
        path.bytes));
  }
  out = Value::instance(module_state->scandir_iterator_class);
  if (!instance_set_native_data(out, kScandirIteratorNativeType, state, scandir_state_cleanup, error)) {
    delete state;
    return false;
  }
  return true;
}

bool os_remove(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.remove() expected one argument";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.remove path", path, error)) {
    return false;
  }
  if (!runtime.vfs().remove(path.text, error)) {
    return raise_path_not_found(runtime, error, &args[0]);
  }
  value_set_none(out);
  return true;
}

bool os_mkdir_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (argc < 1 || argc > 2) {
    error = "os.mkdir() expected path and optional mode";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.mkdir path", path, error)) {
    return false;
  }
  int64_t mode = argc == 2 && args[1].tag == ValueTag::Int64
      ? args[1].as.i64
      : 0777;
  if (argc == 2 && args[1].tag != ValueTag::Int64) {
    error = "os.mkdir() mode must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? std::string() : std::string(kwargs[i].name);
    if (name != "mode" && name != "dir_fd") {
      error = "os.mkdir() got unexpected keyword argument '" + name + "'";
      return false;
    }
    if (name == "mode") {
      if (kwargs[i].value == nullptr || kwargs[i].value->tag != ValueTag::Int64) {
        error = "os.mkdir() mode must be an integer";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      mode = kwargs[i].value->as.i64;
    }
  }
  const std::string parent = std::filesystem::path(path.text).parent_path().string();
  if (!parent.empty()) {
    VfsStat stat;
    if (!runtime.vfs().stat(parent, stat, error)) {
      return raise_path_not_found(runtime, error);
    }
    if (stat.kind != VfsNodeKind::Directory) {
      error = "parent directory does not exist: " + parent;
      return raise_os_error_with_errno(runtime, "FileNotFoundError", 2, error);
    }
  }
  if (!runtime.vfs().make_dirs(path.text, false, error)) {
    if (error.rfind("path exists:", 0) == 0) {
      return raise_os_error_with_errno(runtime, "FileExistsError", 17, error);
    }
    return raise_path_not_found(runtime, error);
  }
#if defined(_WIN32)
  if ((mode & 0777) == 0700) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;FA;;;OW)",
            SDDL_REVISION_1,
            &descriptor,
            nullptr)) {
      error = "failed to create Windows security descriptor";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    const std::filesystem::path native_path = std::filesystem::u8path(path.text);
    const BOOL applied = SetFileSecurityW(
        native_path.c_str(),
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        descriptor);
    LocalFree(descriptor);
    if (!applied) {
      error = "failed to apply mode 0o700 to directory";
      runtime.raise_class_error("OSError", error);
      return false;
    }
  }
#endif
  value_set_none(out);
  return true;
}

bool os_mkdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_mkdir_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool os_umask(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "os.umask() expected one integer argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int mode = static_cast<int>(args[0].as.i64);
#if defined(_WIN32)
  const int previous = _umask(mode);
#else
  const int previous = static_cast<int>(::umask(static_cast<mode_t>(mode)));
#endif
  out = Value::int64(previous);
  return true;
}

bool os_mkdir_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return os_mkdir_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

bool os_rmdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.rmdir() expected one argument";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.rmdir path", path, error)) {
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(path.text, stat, error)) {
    return raise_path_not_found(runtime, error, &args[0]);
  }
  if (stat.kind == VfsNodeKind::Missing) {
    error = "file not found: " + path.text;
    return raise_path_not_found(runtime, error, &args[0]);
  }
  if (stat.kind != VfsNodeKind::Directory) {
    error = "not a directory: " + path.text;
    Value exception = runtime.make_exception("NotADirectoryError", error);
    std::string ignored;
    object_set_attr(exception, "filename", args[0], ignored);
    runtime.set_pending_exception(std::move(exception));
    return false;
  }
  if (!runtime.vfs().remove(path.text, error)) {
    return raise_path_not_found(runtime, error, &args[0]);
  }
  value_set_none(out);
  return true;
}

bool os_rename_common(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, bool replace) {
  if (argc != 2) {
    error = replace ? "os.replace() expected src and dst" : "os.rename() expected src and dst";
    return false;
  }
  PathArg src;
  PathArg dst;
  if (!get_path_arg(runtime, args[0], "src", src, error) ||
      !get_path_arg(runtime, args[1], "dst", dst, error)) {
    return false;
  }
  if (!runtime.vfs().rename(src.text, dst.text, replace, error)) {
    return raise_path_not_found(runtime, error, &args[0]);
  }
  value_set_none(out);
  return true;
}

bool os_rename(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_rename_common(runtime, args, argc, out, error, false);
}

bool os_replace(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_rename_common(runtime, args, argc, out, error, true);
}

bool os_makedirs_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (argc < 1 || argc > 2) {
    error = "os.makedirs() expected path and optional exist_ok";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "os.makedirs path", path, error)) {
    return false;
  }
  bool exist_ok = argc == 2 && value_truthy(args[1]);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "os.makedirs() got invalid keyword argument";
      return false;
    }
    const std::string name(kwargs[i].name);
    if (name == "exist_ok") {
      exist_ok = value_truthy(*kwargs[i].value);
    } else if (name != "mode") {
      error = "os.makedirs() got unexpected keyword argument '" + name + "'";
      return false;
    }
  }
  if (!runtime.vfs().make_dirs(path, exist_ok, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool os_makedirs(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_makedirs_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool os_makedirs_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return os_makedirs_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

bool os_stat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "os.stat() expected one argument";
    return false;
  }
  auto* state = static_cast<OsModuleState*>(user_data);
  if (state == nullptr) {
    error = "os.stat() missing os module state";
    return false;
  }
  if (args[0].tag == ValueTag::Int64 || args[0].tag == ValueTag::Bool) {
    Value descriptor = args[0];
    if (args[0].tag == ValueTag::Bool) {
      Value warnings;
      Value warn;
      const Value* warning_class = runtime.find_builtin("RuntimeWarning");
      if (!runtime.import_module("warnings", warnings, error) ||
          !module_get_attr(warnings, "warn", warn, error) ||
          warning_class == nullptr) {
        return false;
      }
      Value warning_args[] = {
          Value::string("bool is used as a file descriptor"),
          *warning_class,
      };
      Value ignored;
      if (!runtime_call_callable(runtime, warn, warning_args, 2, ignored, error)) {
        return false;
      }
      descriptor = Value::int64(args[0].as.b ? 1 : 0);
    }
    return os_fstat(runtime, &descriptor, 1, out, error, user_data);
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.stat path", path, error)) {
    return false;
  }
  VfsStat stat;
#if defined(_WIN32)
  if ((path.text.rfind("\\\\.\\", 0) == 0 || path.text.rfind("//./", 0) == 0) &&
      !path.text.empty() && path.text.back() == ':') {
    stat.kind = VfsNodeKind::File;
    stat.is_block_device = true;
    out = make_stat_result(state->stat_result_class, stat);
    return true;
  }
  std::string basename = std::filesystem::path(path.text).filename().string();
  std::transform(basename.begin(), basename.end(), basename.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (basename == "NUL") {
    stat.kind = VfsNodeKind::File;
    stat.is_character_device = true;
    out = make_stat_result(state->stat_result_class, stat);
    return true;
  }
#endif
  if (!runtime.vfs().stat(path.text, stat, error)) {
    return raise_path_not_found(runtime, error, &args[0]);
  }
  if (stat.kind == VfsNodeKind::Missing) {
    error = "file not found: " + path.text;
    return raise_path_not_found(runtime, error, &args[0]);
  }
  out = make_stat_result(state->stat_result_class, stat);
  return true;
}

#if defined(_WIN32)
bool windows_set_file_times(
    const std::string& path,
    const int64_t* access_time_ns,
    const int64_t* modification_time_ns) {
  const std::filesystem::path native_path = std::filesystem::u8path(path);
  HANDLE handle = CreateFileW(
      native_path.c_str(), FILE_WRITE_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  FILETIME access{};
  FILETIME modification{};
  if (access_time_ns == nullptr || modification_time_ns == nullptr) {
    GetSystemTimeAsFileTime(&access);
    modification = access;
  } else {
    const auto to_file_time = [](int64_t nanoseconds) {
      int64_t unix_ticks = nanoseconds / 100;
      if (nanoseconds < 0 && nanoseconds % 100 != 0) {
        --unix_ticks;
      }
      const uint64_t ticks = static_cast<uint64_t>(
          unix_ticks + INT64_C(116444736000000000));
      FILETIME result{};
      result.dwLowDateTime = static_cast<DWORD>(ticks);
      result.dwHighDateTime = static_cast<DWORD>(ticks >> 32u);
      return result;
    };
    access = to_file_time(*access_time_ns);
    modification = to_file_time(*modification_time_ns);
  }
  const bool ok = SetFileTime(handle, nullptr, &access, &modification) != 0;
  CloseHandle(handle);
  return ok;
}
#endif

bool os_utime(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "os.utime() expected path and optional times";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.utime path", path, error)) {
    return false;
  }
  if (argc == 1 || args[1].tag == ValueTag::None) {
#if defined(_WIN32)
    const int status = windows_set_file_times(path.text, nullptr, nullptr) ? 0 : -1;
#else
    const int status = ::utime(path.text.c_str(), nullptr);
#endif
    if (status != 0) {
      error = "file not found: " + path.text;
      return raise_path_not_found(runtime, error, &args[0]);
    }
    value_set_none(out);
    return true;
  }
  auto* times = value_as_tuple(args[1]);
  if (times == nullptr || times->items.size() != 2) {
    error = "utime: 'times' must be either a tuple of two ints or None";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if !defined(_WIN32)
  auto timestamp = [&](const Value& value, std::time_t& result) -> bool {
    if (value.tag == ValueTag::Int64) {
      result = static_cast<std::time_t>(value.as.i64);
      return true;
    }
    if (value.tag == ValueTag::Double) {
      result = static_cast<std::time_t>(value.as.f64);
      return true;
    }
    return false;
  };
  std::time_t access_time = 0;
  std::time_t modification_time = 0;
  if (!timestamp(times->items[0], access_time) || !timestamp(times->items[1], modification_time)) {
    error = "utime: times values must be int or float";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#else
  for (const auto& item : times->items) {
    if (item.tag != ValueTag::Int64 && item.tag != ValueTag::Double) {
      error = "utime: times values must be int or float";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
#endif
#if defined(_WIN32)
  auto timestamp_ns = [&](const Value& value, int64_t& result) -> bool {
    long double seconds = 0.0L;
    if (value.tag == ValueTag::Int64) {
      seconds = static_cast<long double>(value.as.i64);
    } else if (value.tag == ValueTag::Double && std::isfinite(value.as.f64)) {
      seconds = static_cast<long double>(value.as.f64);
    } else {
      return false;
    }
    const long double nanoseconds = std::floor(seconds * 1000000000.0L);
    if (nanoseconds < static_cast<long double>((std::numeric_limits<int64_t>::min)()) ||
        nanoseconds > static_cast<long double>((std::numeric_limits<int64_t>::max)())) {
      return false;
    }
    result = static_cast<int64_t>(nanoseconds);
    return true;
  };
  int64_t access_time_ns = 0;
  int64_t modification_time_ns = 0;
  if (!timestamp_ns(times->items[0], access_time_ns) ||
      !timestamp_ns(times->items[1], modification_time_ns)) {
    error = "utime: timestamp out of range";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  const int status = windows_set_file_times(
      path.text, &access_time_ns, &modification_time_ns) ? 0 : -1;
#else
  struct utimbuf native_times{};
  native_times.actime = access_time;
  native_times.modtime = modification_time;
  const int status = ::utime(path.text.c_str(), &native_times);
#endif
  if (status != 0) {
    error = "file not found: " + path.text;
    return raise_path_not_found(runtime, error, &args[0]);
  }
  value_set_none(out);
  return true;
}

bool os_closerange(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64) {
    error = "closerange() expected two integer arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int64_t first = args[0].as.i64;
  const int64_t last = args[1].as.i64;
  for (int64_t fd = first; fd < last && fd <= (std::numeric_limits<int>::max)(); ++fd) {
#if defined(_WIN32)
    safe_close(static_cast<int>(fd));
#else
    ::close(static_cast<int>(fd));
#endif
  }
  value_set_none(out);
  return true;
}

bool os_utime_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    return os_utime(runtime, args, argc, out, error, nullptr);
  }
  Value times = argc == 2 ? args[1] : Value::none();
  bool times_given = argc == 2;
  bool ns_given = false;
  Value ns;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? std::string() : kwargs[i].name;
    if (name == "times") {
      if (times_given) {
        error = "os.utime() got multiple values for argument 'times'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(times, *kwargs[i].value);
      times_given = true;
    } else if (name == "ns") {
      if (ns_given) {
        error = "os.utime() got multiple values for argument 'ns'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(ns, *kwargs[i].value);
      ns_given = true;
    } else if (name == "follow_symlinks") {
      if (!value_truthy(*kwargs[i].value)) {
        error = "utime: follow_symlinks=False is unavailable on this platform";
        runtime.raise_class_error("NotImplementedError", error);
        return false;
      }
    } else if (name == "dir_fd") {
      if (kwargs[i].value->tag != ValueTag::None) {
        error = "utime: dir_fd is unavailable on this platform";
        runtime.raise_class_error("NotImplementedError", error);
        return false;
      }
    } else {
      error = "os.utime() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (times_given && ns_given) {
    error = "utime: you may specify either 'times' or 'ns' but not both";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (ns_given) {
    auto* tuple = value_as_tuple(ns);
    if (tuple == nullptr || tuple->items.size() != 2) {
      error = "utime: 'ns' must be a tuple of two ints";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
#if defined(_WIN32)
    int64_t timestamps[2]{};
    for (size_t index = 0; index < 2; ++index) {
      if (tuple->items[index].tag != ValueTag::Int64) {
        error = "utime: ns values must be int";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      timestamps[index] = tuple->items[index].as.i64;
    }
    PathArg path;
    if (!get_path_arg(runtime, args[0], "os.utime path", path, error)) {
      return false;
    }
    if (!windows_set_file_times(path.text, &timestamps[0], &timestamps[1])) {
      error = "file not found: " + path.text;
      return raise_path_not_found(runtime, error, &args[0]);
    }
    value_set_none(out);
    return true;
#else
    std::vector<Value> seconds;
    seconds.reserve(2);
    for (const auto& item : tuple->items) {
      if (item.tag == ValueTag::Int64) {
        seconds.push_back(Value::number(static_cast<double>(item.as.i64) / 1000000000.0));
      } else if (item.tag == ValueTag::Double) {
        seconds.push_back(Value::number(item.as.f64 / 1000000000.0));
      } else {
        error = "utime: ns values must be int";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    }
    times = Value::tuple(std::move(seconds));
#endif
  }
  Value positional[2] = {args[0], times};
  return os_utime(runtime, positional, 2, out, error, nullptr);
}

bool os_chmod(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "os.chmod() expected path and integer mode";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.chmod path", path, error)) {
    return false;
  }
#if defined(_WIN32)
  const int requested_mode = static_cast<int>(args[1].as.i64);
  const int windows_mode = _S_IREAD | ((requested_mode & 0222) != 0 ? _S_IWRITE : 0);
  const int status = _wchmod(
      std::filesystem::u8path(path.text).c_str(), windows_mode);
#else
  const int status = ::chmod(path.text.c_str(), static_cast<mode_t>(args[1].as.i64));
#endif
  if (status != 0) {
    error = "chmod failed: " + path.text;
    return raise_path_not_found(runtime, error, &args[0]);
  }
  value_set_none(out);
  return true;
}

bool os_chmod_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? std::string() : std::string(kwargs[i].name);
    if (name != "dir_fd" && name != "follow_symlinks") {
      error = "os.chmod() got unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return os_chmod(runtime, args, argc, out, error, user_data);
}

bool os_lstat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "os.lstat() expected one argument";
    return false;
  }
  auto* state = static_cast<OsModuleState*>(user_data);
  if (state == nullptr) {
    error = "os.lstat() missing os module state";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.lstat path", path, error)) {
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(path.text, stat, error)) {
    return raise_path_not_found(runtime, error, &args[0]);
  }
  if (stat.kind == VfsNodeKind::Missing) {
    error = "file not found: " + path.text;
    return raise_path_not_found(runtime, error, &args[0]);
  }
#if defined(_WIN32)
  if ((stat.file_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    stat.is_symlink = true;
  }
#endif
  out = make_stat_result(state->stat_result_class, stat);
  return true;
}

bool os_stat_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 1) {
    error = "os.stat() expected one argument";
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const char* name = kwargs[i].name;
    if (name == nullptr || kwargs[i].value == nullptr) {
      error = "os.stat() keyword argument is invalid";
      return false;
    }
    if (std::string(name) != "follow_symlinks") {
      error = std::string("os.stat() got unexpected keyword argument '") + name + "'";
      return false;
    }
  }
  return os_stat(runtime, args, argc, out, error, user_data);
}

bool os_access(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "os.access() expected path and mode";
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.access path", path, error)) {
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(path.text, stat, error)) {
    return raise_path_not_found(runtime, error);
  }
  value_set_bool(out, stat.kind != VfsNodeKind::Missing);
  return true;
}

bool os_getenv(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "os.getenv() expected one or two arguments";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "os.getenv key", name, error)) {
    return false;
  }
  const char* value = std::getenv(name.c_str());
  if (value != nullptr) {
    out = Value::string(value);
  } else if (argc == 2) {
    value_assign_fast(out, args[1]);
  } else {
    value_set_none(out);
  }
  return true;
}

bool os_strerror(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "strerror() requires an integer error code";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].as.i64 < (std::numeric_limits<int>::min)() ||
      args[0].as.i64 > (std::numeric_limits<int>::max)()) {
    error = "error code does not fit a C integer";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  out = Value::string(std::strerror(static_cast<int>(args[0].as.i64)));
  return true;
}

bool os_putenv(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "putenv() expected key and value";
    return false;
  }
  PathArg key_arg, value_arg;
  if (!get_path_arg(runtime, args[0], "putenv key", key_arg, error) ||
      !get_path_arg(runtime, args[1], "putenv value", value_arg, error)) {
    return false;
  }
  const auto& name = key_arg.text;
  const auto& value = value_arg.text;
  if (name.empty() || name.find('=') != std::string::npos ||
      name.find('\0') != std::string::npos || value.find('\0') != std::string::npos) {
    error = "illegal environment variable name or value";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
#if defined(_WIN32)
  if (name.size() > 32767 || value.size() > 32767) {
    error = "environment variable is too long";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (!SetEnvironmentVariableA(name.c_str(), value.c_str())) {
    error = "putenv failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
#else
  if (::setenv(name.c_str(), value.c_str(), 1) != 0) {
    error = "putenv failed";
    return false;
  }
#endif
  value_set_none(out);
  return true;
}

bool os_unsetenv(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "unsetenv() expected key";
    return false;
  }
  PathArg key_arg;
  if (!get_path_arg(runtime, args[0], "unsetenv key", key_arg, error)) {
    return false;
  }
  const auto& name = key_arg.text;
  if (name.empty() || name.find('=') != std::string::npos || name.find('\0') != std::string::npos) {
    error = "illegal environment variable name";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
#if defined(_WIN32)
  if (name.size() > 32767) {
    error = "environment variable name is too long";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (!SetEnvironmentVariableA(name.c_str(), nullptr)) {
    error = "unsetenv failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
#else
  if (::unsetenv(name.c_str()) != 0) {
    error = "unsetenv failed";
    return false;
  }
#endif
  value_set_none(out);
  return true;
}

bool os_fspath(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.fspath() expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_string(args[0]) != nullptr || value_as_bytes(args[0]) != nullptr) {
    value_assign_fast(out, args[0]);
    return true;
  }
  std::string ignored;
  Value path_value;
  if (object_get_attr(args[0], "_path", path_value, ignored) && value_as_string(path_value) != nullptr) {
    value_assign_fast(out, path_value);
    return true;
  }
  if (object_get_attr(args[0], "__xlang3_string_value__", path_value, ignored) && value_as_string(path_value) != nullptr) {
    value_assign_fast(out, path_value);
    return true;
  }
  Value fspath;
  if (object_get_attr(args[0], "__fspath__", fspath, ignored) &&
      fspath.tag != ValueTag::None) {
    Value result;
    std::string call_error;
    if (!runtime_call_callable(runtime, fspath, nullptr, 0, result, call_error)) {
      error = call_error.empty() ? "__fspath__ failed" : call_error;
      Value pending;
      if (!runtime.take_pending_exception(pending)) {
        runtime.raise_class_error("TypeError", error);
      } else {
        runtime.set_pending_exception(std::move(pending));
      }
      return false;
    }
    if (value_as_string(result) != nullptr || value_as_bytes(result) != nullptr) {
      value_assign_fast(out, result);
      return true;
    }
    error = "expected " + std::string(value_binary_type_name(args[0])) +
        ".__fspath__() to return str or bytes, not " +
        value_binary_type_name(result);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  error = "expected str, bytes or os.PathLike object, not " +
      std::string(value_binary_type_name(args[0]));
  runtime.raise_class_error("TypeError", error);
  return false;
}

#if defined(_WIN32)
bool os_supports_virtual_terminal(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "nt._supports_virtual_terminal", error)) {
    return false;
  }
  value_set_bool(out, false);
  return true;
}

bool windows_utf8_path(
    Runtime& runtime,
    const PathArg& path,
    std::wstring& out,
    std::string& error) {
  if (path.text.empty()) {
    out.clear();
    return true;
  }
  const int size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, path.text.data(),
      static_cast<int>(path.text.size()), nullptr, 0);
  if (size <= 0) {
    if (!path.bytes) {
      out.clear();
      out.reserve(path.text.size());
      for (size_t i = 0; i < path.text.size();) {
        const unsigned char lead = static_cast<unsigned char>(path.text[i]);
        const size_t width = utf8_codepoint_width(lead);
        if (width == 0 || i + width > path.text.size()) {
          error = "path cannot be decoded using the filesystem encoding";
          runtime.raise_class_error("ValueError", error);
          return false;
        }
        uint32_t codepoint = width == 1
            ? lead
            : lead & ((1u << (7u - static_cast<uint32_t>(width))) - 1u);
        for (size_t j = 1; j < width; ++j) {
          const unsigned char continuation =
              static_cast<unsigned char>(path.text[i + j]);
          if ((continuation & 0xc0u) != 0x80u) {
            error = "path cannot be decoded using the filesystem encoding";
            runtime.raise_class_error("ValueError", error);
            return false;
          }
          codepoint = (codepoint << 6u) | (continuation & 0x3fu);
        }
        if (codepoint <= 0xffffu) {
          out.push_back(static_cast<wchar_t>(codepoint));
        } else if (codepoint <= 0x10ffffu) {
          codepoint -= 0x10000u;
          out.push_back(static_cast<wchar_t>(0xd800u + (codepoint >> 10u)));
          out.push_back(static_cast<wchar_t>(0xdc00u + (codepoint & 0x3ffu)));
        } else {
          error = "path cannot be decoded using the filesystem encoding";
          runtime.raise_class_error("ValueError", error);
          return false;
        }
        i += width;
      }
      return true;
    }
    error = "path cannot be decoded using the filesystem encoding";
    runtime.raise_class_error(path.bytes ? "UnicodeDecodeError" : "ValueError", error);
    return false;
  }
  out.resize(static_cast<size_t>(size));
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, path.text.data(),
          static_cast<int>(path.text.size()), out.data(), size) <= 0) {
    error = "path cannot be decoded using the filesystem encoding";
    runtime.raise_class_error(path.bytes ? "UnicodeDecodeError" : "ValueError", error);
    return false;
  }
  return true;
}

bool windows_wide_path_to_utf8(
    Runtime& runtime,
    const std::wstring& path,
    std::string& out,
    std::string& error) {
  if (path.empty()) {
    out.clear();
    return true;
  }
  const int size = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()),
      nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    error = "Windows path cannot be encoded using the filesystem encoding";
    runtime.raise_class_error("UnicodeEncodeError", error);
    return false;
  }
  out.resize(static_cast<size_t>(size));
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, path.data(), static_cast<int>(path.size()),
          out.data(), size, nullptr, nullptr) <= 0) {
    error = "Windows path cannot be encoded using the filesystem encoding";
    runtime.raise_class_error("UnicodeEncodeError", error);
    return false;
  }
  return true;
}

bool windows_fspath_allow_null(
    Runtime& runtime,
    const Value& value,
    PathArg& out,
    std::string& error) {
  Value path_value;
  if (!os_fspath(runtime, &value, 1, path_value, error, nullptr)) {
    return false;
  }
  if (auto* text = value_as_string(path_value)) {
    out.text = string_object_to_string(*text);
    out.bytes = false;
    return true;
  }
  if (auto* bytes = value_as_bytes(path_value)) {
    out.text = bytes_object_to_string(*bytes);
    out.bytes = true;
    std::wstring decoded;
    return windows_utf8_path(runtime, out, decoded, error);
  }
  error = "expected str, bytes or os.PathLike object";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool os_path_splitroot_ex(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1) {
    error = "nt._path_splitroot_ex() expected one path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!windows_fspath_allow_null(runtime, args[0], path, error)) {
    return false;
  }
  const std::string& original = path.text;
  std::string normalized = original;
  std::replace(normalized.begin(), normalized.end(), '/', '\\');
  const auto part = [&](size_t start, size_t count = std::string::npos) {
    return path_name_value(original.substr(start, count), path.bytes);
  };
  const Value empty = path_name_value("", path.bytes);

  if (!normalized.empty() && normalized[0] == '\\') {
    if (normalized.size() >= 2 && normalized[1] == '\\') {
      size_t start = 2;
      if (normalized.size() >= 8) {
        std::string prefix = normalized.substr(0, 8);
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](char ch) {
          return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        });
        if (prefix == "\\\\?\\UNC\\") start = 8;
      }
      const size_t first = normalized.find('\\', start);
      if (first == std::string::npos) {
        out = Value::tuple({part(0), empty, empty});
        return true;
      }
      const size_t second = normalized.find('\\', first + 1);
      if (second == std::string::npos) {
        out = Value::tuple({part(0), empty, empty});
        return true;
      }
      out = Value::tuple({part(0, second), part(second, 1), part(second + 1)});
      return true;
    }
    out = Value::tuple({empty, part(0, 1), part(1)});
    return true;
  }
  if (normalized.size() >= 2 && normalized[1] == ':') {
    if (normalized.size() >= 3 && normalized[2] == '\\') {
      out = Value::tuple({part(0, 2), part(2, 1), part(3)});
    } else {
      out = Value::tuple({part(0, 2), empty, part(2)});
    }
    return true;
  }
  out = Value::tuple({empty, empty, part(0)});
  return true;
}

bool os_getfullpathname(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "nt._getfullpathname() expected one path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "nt._getfullpathname path", path, error)) {
    Value pending_exception;
    if (!runtime.take_pending_exception(pending_exception)) {
      runtime.raise_class_error("TypeError", error);
    } else {
      runtime.set_pending_exception(std::move(pending_exception));
    }
    return false;
  }
  if (!path.text.empty() &&
      std::all_of(path.text.begin(), path.text.end(), [](char ch) { return ch == ' '; })) {
    error = "The filename, directory name, or volume label syntax is incorrect";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  std::string resolved_input = path.text;
  std::replace(resolved_input.begin(), resolved_input.end(), '/', '\\');
  std::string runtime_cwd = runtime.vfs().cwd();
  while (runtime_cwd.size() > 1 &&
         (runtime_cwd.back() == '/' || runtime_cwd.back() == '\\')) {
    if (runtime_cwd.size() == 3 && runtime_cwd[1] == ':' &&
        (runtime_cwd[2] == '/' || runtime_cwd[2] == '\\')) {
      break;
    }
    runtime_cwd.pop_back();
  }
  const bool has_drive = resolved_input.size() >= 2 && resolved_input[1] == ':';
  const bool has_root = !resolved_input.empty() && resolved_input[0] == '\\';
  const bool is_unc = resolved_input.size() >= 2 &&
      resolved_input[0] == '\\' && resolved_input[1] == '\\';
  const bool is_drive_absolute = has_drive && resolved_input.size() >= 3 &&
      resolved_input[2] == '\\';
  if (resolved_input.empty()) {
    resolved_input = runtime_cwd;
  } else if (!is_unc && !is_drive_absolute) {
    if (has_root) {
      if (runtime_cwd.size() >= 2 && runtime_cwd[1] == ':') {
        resolved_input = runtime_cwd.substr(0, 2) + resolved_input;
      }
    } else if (!has_drive) {
      resolved_input = runtime_cwd +
          ((!runtime_cwd.empty() && runtime_cwd.back() != '\\' && runtime_cwd.back() != '/') ? "\\" : "") +
          resolved_input;
    } else if (runtime_cwd.size() >= 2 && runtime_cwd[1] == ':' &&
               std::tolower(static_cast<unsigned char>(runtime_cwd[0])) ==
                   std::tolower(static_cast<unsigned char>(resolved_input[0]))) {
      const std::string drive_tail = resolved_input.substr(2);
      resolved_input = runtime_cwd;
      if (!drive_tail.empty()) {
        resolved_input +=
            (!runtime_cwd.empty() && runtime_cwd.back() != '\\' && runtime_cwd.back() != '/') ? "\\" : "";
        resolved_input += drive_tail;
      }
    }
  }
  PathArg resolved_path{resolved_input, path.bytes};
  std::wstring wide_path;
  if (!windows_utf8_path(runtime, resolved_path, wide_path, error)) {
    return false;
  }
  std::wstring buffer(32768, L'\0');
  DWORD length = GetFullPathNameW(
      wide_path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
  if (length == 0) {
    error = "Windows could not resolve the full path";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  if (length >= buffer.size()) {
    buffer.assign(static_cast<size_t>(length) + 1, L'\0');
    length = GetFullPathNameW(
        wide_path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size()) {
      error = "Windows could not resolve the full path";
      runtime.raise_class_error("OSError", error);
      return false;
    }
  }
  buffer.resize(static_cast<size_t>(length));
  std::string result;
  if (!windows_wide_path_to_utf8(runtime, buffer, result, error)) {
    return false;
  }
  out = path_name_value(result, path.bytes);
  return true;
}

bool os_getfinalpathname(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "nt._getfinalpathname() expected one path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "nt._getfinalpathname path", path, error)) {
    return false;
  }
  std::wstring wide_path;
  if (!windows_utf8_path(runtime, path, wide_path, error)) {
    return false;
  }
  HANDLE handle = CreateFileW(
      wide_path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    error = "Windows could not open the path";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  std::wstring buffer(32768, L'\0');
  DWORD length = GetFinalPathNameByHandleW(
      handle, buffer.data(), static_cast<DWORD>(buffer.size()),
      FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (length >= buffer.size()) {
    buffer.assign(static_cast<size_t>(length) + 1, L'\0');
    length = GetFinalPathNameByHandleW(
        handle, buffer.data(), static_cast<DWORD>(buffer.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  }
  CloseHandle(handle);
  if (length == 0 || length >= buffer.size()) {
    error = "Windows could not resolve the final path";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  buffer.resize(static_cast<size_t>(length));
  std::string result;
  if (!windows_wide_path_to_utf8(runtime, buffer, result, error)) {
    return false;
  }
  out = path_name_value(result, path.bytes);
  return true;
}

bool os_getvolumepathname(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "nt._getvolumepathname() expected one path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "nt._getvolumepathname path", path, error)) {
    return false;
  }
  std::wstring wide_path;
  if (!windows_utf8_path(runtime, path, wide_path, error)) {
    return false;
  }
  std::wstring buffer(32768, L'\0');
  if (GetVolumePathNameW(
          wide_path.c_str(), buffer.data(), static_cast<DWORD>(buffer.size())) == 0) {
    error = "Windows could not resolve the volume path";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  buffer.resize(std::wcslen(buffer.c_str()));
  std::string result;
  if (!windows_wide_path_to_utf8(runtime, buffer, result, error)) {
    return false;
  }
  out = path_name_value(result, path.bytes);
  return true;
}

bool os_getdiskusage(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "nt._getdiskusage() expected one path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "nt._getdiskusage path", path, error)) {
    return false;
  }
  std::wstring wide_path;
  if (!windows_utf8_path(runtime, path, wide_path, error)) {
    return false;
  }
  ULARGE_INTEGER available{};
  ULARGE_INTEGER total{};
  ULARGE_INTEGER free{};
  if (GetDiskFreeSpaceExW(wide_path.c_str(), &available, &total, &free) == 0) {
    error = "Windows could not query disk usage";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  out = Value::tuple({
      Value::int64(static_cast<int64_t>(total.QuadPart)),
      Value::int64(static_cast<int64_t>(free.QuadPart)),
  });
  return true;
}

bool os_listvolumes(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "os.listvolumes", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::wstring buffer(32768, L'\0');
  HANDLE search = FindFirstVolumeW(buffer.data(), static_cast<DWORD>(buffer.size()));
  if (search == INVALID_HANDLE_VALUE) {
    error = "Windows could not enumerate volumes";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  std::vector<Value> volumes;
  for (;;) {
    const size_t length = std::wcslen(buffer.c_str());
    std::string volume;
    if (!windows_wide_path_to_utf8(
            runtime, std::wstring(buffer.data(), length), volume, error)) {
      FindVolumeClose(search);
      return false;
    }
    volumes.push_back(Value::string(std::move(volume)));
    std::fill(buffer.begin(), buffer.end(), L'\0');
    if (FindNextVolumeW(search, buffer.data(), static_cast<DWORD>(buffer.size())) == 0) {
      const DWORD last_error = GetLastError();
      FindVolumeClose(search);
      if (last_error != ERROR_NO_MORE_FILES) {
        error = "Windows volume enumeration failed";
        runtime.raise_class_error("OSError", error);
        return false;
      }
      break;
    }
  }
  out = Value::list(std::move(volumes));
  return true;
}

bool os_listdrives(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "os.listdrives", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const DWORD required = GetLogicalDriveStringsW(0, nullptr);
  if (required == 0) {
    error = "Windows could not enumerate drives";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  std::wstring buffer(required + 1, L'\0');
  if (GetLogicalDriveStringsW(required, buffer.data()) == 0) {
    error = "Windows could not enumerate drives";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  std::vector<Value> drives;
  for (const wchar_t* current = buffer.c_str(); *current != L'\0';
       current += std::wcslen(current) + 1) {
    std::string drive;
    if (!windows_wide_path_to_utf8(runtime, current, drive, error)) {
      return false;
    }
    drives.push_back(Value::string(std::move(drive)));
  }
  out = Value::list(std::move(drives));
  return true;
}

bool os_listmounts(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.listmounts() expected one volume";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg volume;
  if (!get_path_arg(runtime, args[0], "os.listmounts volume", volume, error)) {
    return false;
  }
  std::wstring wide_volume;
  if (!windows_utf8_path(runtime, volume, wide_volume, error)) {
    return false;
  }
  DWORD required = 0;
  (void)GetVolumePathNamesForVolumeNameW(
      wide_volume.c_str(), nullptr, 0, &required);
  if (required == 0) {
    error = "Windows could not enumerate volume mount points";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  std::wstring buffer(required + 1, L'\0');
  if (!GetVolumePathNamesForVolumeNameW(
          wide_volume.c_str(), buffer.data(),
          static_cast<DWORD>(buffer.size()), &required)) {
    error = "Windows could not enumerate volume mount points";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  std::vector<Value> mounts;
  for (const wchar_t* current = buffer.c_str(); *current != L'\0';
       current += std::wcslen(current) + 1) {
    std::string mount;
    if (!windows_wide_path_to_utf8(runtime, current, mount, error)) {
      return false;
    }
    mounts.push_back(Value::string(std::move(mount)));
  }
  out = Value::list(std::move(mounts));
  return true;
}

bool os_device_encoding(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "os.device_encoding() expected a file descriptor";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const intptr_t raw_handle = safe_get_osfhandle(static_cast<int>(args[0].as.i64));
  if (raw_handle == -1 ||
      GetFileType(reinterpret_cast<HANDLE>(raw_handle)) != FILE_TYPE_CHAR) {
    value_set_none(out);
    return true;
  }
  out = Value::string("utf-8");
  return true;
}

bool os_path_isdevdrive(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "nt._path_isdevdrive() expected one path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "nt._path_isdevdrive path", path, error)) {
    return false;
  }
  std::wstring wide_path;
  if (!windows_utf8_path(runtime, path, wide_path, error)) {
    return false;
  }
  std::wstring volume_path(32768, L'\0');
  if (GetVolumePathNameW(
          wide_path.c_str(), volume_path.data(),
          static_cast<DWORD>(volume_path.size())) == 0) {
    error = "Windows could not resolve the volume";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_bool(out, false);
  return true;
}

enum class WindowsPathQuery {
  Exists,
  IsDirectory,
  IsFile,
  IsLink,
  IsJunction,
};

bool os_windows_path_query(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    WindowsPathQuery query) {
  if (argc != 1) {
    error = "Windows path query expected one path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t fd_value = 0;
  if (args[0].tag == ValueTag::Bool) {
    Value warnings;
    Value warn;
    const Value* warning_class = runtime.find_builtin("RuntimeWarning");
    if (!runtime.import_module("warnings", warnings, error) ||
        !module_get_attr(warnings, "warn", warn, error) ||
        warning_class == nullptr) {
      return false;
    }
    Value warning_args[] = {
        Value::string("bool is used as a file descriptor"),
        *warning_class,
    };
    Value ignored;
    if (!runtime_call_callable(runtime, warn, warning_args, 2, ignored, error)) {
      return false;
    }
    fd_value = args[0].as.b ? 1 : 0;
  } else if (args[0].tag == ValueTag::Int64) {
    fd_value = args[0].as.i64;
  }
  if (args[0].tag == ValueTag::Int64 || args[0].tag == ValueTag::Bool) {
    if (fd_value < 0 || fd_value > (std::numeric_limits<int>::max)()) {
      value_set_bool(out, false);
      return true;
    }
    const intptr_t raw_handle = safe_get_osfhandle(static_cast<int>(fd_value));
    if (raw_handle == -1) {
      value_set_bool(out, false);
      return true;
    }
    const DWORD file_type = GetFileType(reinterpret_cast<HANDLE>(raw_handle));
    if (query == WindowsPathQuery::Exists) {
      value_set_bool(out, file_type != FILE_TYPE_UNKNOWN || GetLastError() == NO_ERROR);
      return true;
    }
    if (query == WindowsPathQuery::IsFile) {
      value_set_bool(out, file_type == FILE_TYPE_DISK);
      return true;
    }
    if (query == WindowsPathQuery::IsDirectory && file_type == FILE_TYPE_DISK) {
      BY_HANDLE_FILE_INFORMATION info{};
      value_set_bool(
          out,
          GetFileInformationByHandle(reinterpret_cast<HANDLE>(raw_handle), &info) != 0 &&
              (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
      return true;
    }
    value_set_bool(out, false);
    return true;
  }
  PathArg path;
  if (!get_path_arg(runtime, args[0], "Windows path query", path, error)) {
    Value ignored_exception;
    if (!runtime.take_pending_exception(ignored_exception)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_set_bool(out, false);
    return true;
  }
  std::string narrow_normalized = path.text;
  std::replace(narrow_normalized.begin(), narrow_normalized.end(), '/', '\\');
  std::transform(narrow_normalized.begin(), narrow_normalized.end(), narrow_normalized.begin(),
                 [](char ch) { return static_cast<char>(std::toupper(static_cast<unsigned char>(ch))); });
  std::string device_name = narrow_normalized.substr(narrow_normalized.find_last_of('\\') + 1);
  const size_t device_suffix = device_name.find_first_of(".:");
  if (device_suffix != std::string::npos) {
    device_name.resize(device_suffix);
  }
  while (!device_name.empty() && device_name.back() == ' ') {
    device_name.pop_back();
  }
  const bool is_dos_device =
      device_name == "NUL" || device_name == "CON" || device_name == "CONIN$" ||
      device_name == "CONOUT$" || device_name == "PRN" || device_name == "AUX" ||
      (device_name.size() == 4 &&
       (device_name.rfind("COM", 0) == 0 || device_name.rfind("LPT", 0) == 0) &&
       device_name[3] >= '1' && device_name[3] <= '9');
  if (is_dos_device) {
    value_set_bool(out, query == WindowsPathQuery::Exists);
    return true;
  }
  if (query == WindowsPathQuery::IsFile &&
      (narrow_normalized.rfind("\\\\.\\PIPE\\", 0) == 0 ||
       narrow_normalized.rfind("\\\\?\\PIPE\\", 0) == 0)) {
    value_set_bool(out, false);
    return true;
  }
  VfsStat vfs_stat;
  std::string vfs_error;
  if (runtime.vfs().stat(path.text, vfs_stat, vfs_error) &&
      vfs_stat.kind != VfsNodeKind::Missing) {
    if (query == WindowsPathQuery::Exists) {
      value_set_bool(out, true);
      return true;
    }
    if (query == WindowsPathQuery::IsDirectory) {
      value_set_bool(out, vfs_stat.kind == VfsNodeKind::Directory);
      return true;
    }
    if (query == WindowsPathQuery::IsFile) {
      value_set_bool(out, vfs_stat.kind == VfsNodeKind::File && !vfs_stat.is_character_device);
      return true;
    }
  }
  std::wstring wide_path;
  if (!windows_utf8_path(runtime, path, wide_path, error)) {
    Value ignored_exception;
    if (!runtime.take_pending_exception(ignored_exception)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    value_set_bool(out, false);
    return true;
  }

  std::wstring normalized_path = wide_path;
  std::replace(normalized_path.begin(), normalized_path.end(), L'/', L'\\');
  std::wstring uppercase_path = normalized_path;
  std::transform(uppercase_path.begin(), uppercase_path.end(), uppercase_path.begin(),
                 [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
  if (query == WindowsPathQuery::IsFile &&
      (uppercase_path.rfind(L"\\\\.\\PIPE\\", 0) == 0 ||
       uppercase_path.rfind(L"\\\\?\\PIPE\\", 0) == 0)) {
    value_set_bool(out, false);
    return true;
  }

  const DWORD attributes = GetFileAttributesW(wide_path.c_str());
  if (attributes != INVALID_FILE_ATTRIBUTES) {
    switch (query) {
      case WindowsPathQuery::Exists:
        value_set_bool(out, true);
        return true;
      case WindowsPathQuery::IsDirectory:
        value_set_bool(out, (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
        return true;
      case WindowsPathQuery::IsFile:
        value_set_bool(out, (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0);
        return true;
      case WindowsPathQuery::IsLink:
      case WindowsPathQuery::IsJunction:
        break;
    }
  } else if (query != WindowsPathQuery::Exists) {
    value_set_bool(out, false);
    return true;
  }

  if (query == WindowsPathQuery::Exists) {
    std::wstring normalized = wide_path;
    std::replace(normalized.begin(), normalized.end(), L'/', L'\\');
    const size_t separator = normalized.find_last_of(L'\\');
    std::wstring basename = normalized.substr(
        separator == std::wstring::npos ? 0 : separator + 1);
    const size_t suffix = basename.find_first_of(L".:");
    if (suffix != std::wstring::npos) {
      basename.resize(suffix);
    }
    while (!basename.empty() && basename.back() == L' ') {
      basename.pop_back();
    }
    std::transform(basename.begin(), basename.end(), basename.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    if (basename == L"CON" || basename == L"CONIN$" || basename == L"CONOUT$") {
      value_set_bool(out, true);
      return true;
    }
  }

  const DWORD flags = FILE_FLAG_BACKUP_SEMANTICS |
      ((query == WindowsPathQuery::IsLink || query == WindowsPathQuery::IsJunction)
           ? FILE_FLAG_OPEN_REPARSE_POINT
           : 0);
  HANDLE handle = CreateFileW(
      wide_path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    value_set_bool(out, false);
    return true;
  }
  if (query == WindowsPathQuery::Exists) {
    CloseHandle(handle);
    value_set_bool(out, true);
    return true;
  }
  FILE_ATTRIBUTE_TAG_INFO info{};
  const bool has_tag = GetFileInformationByHandleEx(
      handle, FileAttributeTagInfo, &info, sizeof(info)) != 0;
  CloseHandle(handle);
  const bool is_reparse = has_tag &&
      (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
  value_set_bool(
      out,
      query == WindowsPathQuery::IsJunction
          ? is_reparse && info.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT
          : is_reparse && info.ReparseTag == IO_REPARSE_TAG_SYMLINK);
  return true;
}

bool os_path_exists(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_windows_path_query(runtime, args, argc, out, error, WindowsPathQuery::Exists);
}

bool os_path_isdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_windows_path_query(runtime, args, argc, out, error, WindowsPathQuery::IsDirectory);
}

bool os_path_isfile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_windows_path_query(runtime, args, argc, out, error, WindowsPathQuery::IsFile);
}

bool os_path_islink(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_windows_path_query(runtime, args, argc, out, error, WindowsPathQuery::IsLink);
}

bool os_path_isjunction(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_windows_path_query(runtime, args, argc, out, error, WindowsPathQuery::IsJunction);
}

bool os_windows_path_query_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    WindowsPathQuery query) {
  if (kwargc == 0) {
    return os_windows_path_query(runtime, args, argc, out, error, query);
  }
  if (argc != 0 || kwargc != 1 || kwargs[0].name == nullptr ||
      kwargs[0].value == nullptr || std::string(kwargs[0].name) != "path") {
    error = "Windows path query expected one path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value path = *kwargs[0].value;
  return os_windows_path_query(runtime, &path, 1, out, error, query);
}

#define XLANG3_WINDOWS_PATH_QUERY_KW(name, query_kind)                         \
  bool name(                                                                  \
      Runtime& runtime, const Value* args, uint32_t argc,                     \
      const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out,            \
      std::string& error, void*) {                                             \
    return os_windows_path_query_kw(                                           \
        runtime, args, argc, kwargs, kwargc, out, error, query_kind);          \
  }

XLANG3_WINDOWS_PATH_QUERY_KW(os_path_exists_kw, WindowsPathQuery::Exists)
XLANG3_WINDOWS_PATH_QUERY_KW(os_path_isdir_kw, WindowsPathQuery::IsDirectory)
XLANG3_WINDOWS_PATH_QUERY_KW(os_path_isfile_kw, WindowsPathQuery::IsFile)
XLANG3_WINDOWS_PATH_QUERY_KW(os_path_islink_kw, WindowsPathQuery::IsLink)
XLANG3_WINDOWS_PATH_QUERY_KW(os_path_isjunction_kw, WindowsPathQuery::IsJunction)

#undef XLANG3_WINDOWS_PATH_QUERY_KW
#endif

} // namespace

void register_os_module(Runtime& runtime) {
  Value env_dict = make_process_environ_dict();
  auto* os_state = new OsModuleState();
  os_state->stat_result_class = make_stat_result_class(runtime);
  os_state->terminal_size_class = make_terminal_size_class(runtime);
  os_state->times_result_class = make_times_result_class(runtime);
  os_state->dir_entry_class = make_dir_entry_class(runtime, os_state);
  os_state->scandir_iterator_class = make_scandir_iterator_class(runtime);
  runtime.register_native_package_cleanup(os_state, os_module_state_cleanup);
  Value stat_function = runtime.make_native_function(
      "os.stat", os_stat, os_state, nullptr, nullptr, false, os_stat_kw);
  builtin_method_set_text_signature(
      stat_function, "(path, *, dir_fd=None, follow_symlinks=True)");

#if defined(_WIN32)
  NativeModuleBuilder builder(runtime, "nt");
#else
  NativeModuleBuilder builder(runtime, "posix");
#endif
  builder.function("getcwd", os_getcwd)
      .function("readlink", os_readlink)
      .function("getcwdb", os_getcwdb)
      .function("chdir", os_chdir)
      .function("fsencode", os_fsencode)
      .function("fsdecode", os_fsdecode)
      .function("urandom", os_urandom)
      .function("open", os_open, nullptr, false, os_open_kw)
      .function("close", os_close)
      .function("closerange", os_closerange)
      .function("read", os_read)
      .function("readinto", os_readinto)
      .function("write", os_write)
      .function("lseek", os_lseek)
      .value("fstat", runtime.make_native_function("os.fstat", os_fstat, os_state))
      .function("dup", os_dup)
      .function("dup2", os_dup2, nullptr, false, os_dup2_kw)
      .function("pipe", os_pipe)
      .function("isatty", os_isatty)
      .function("get_inheritable", os_get_inheritable)
      .function("set_inheritable", os_set_inheritable)
      .function("getpid", os_getpid)
      .function("strerror", os_strerror)
      .function("getppid", os_getppid)
#if defined(_WIN32)
      .function("spawnv", os_spawnv)
      .function("spawnve", os_spawnve)
      .function("waitpid", os_waitpid)
      .function("waitstatus_to_exitcode", os_waitstatus_to_exitcode)
#endif
      .function("cpu_count", os_cpu_count)
      .value("times", runtime.make_native_function("os.times", os_times, os_state))
      .value("times_result", os_state->times_result_class)
      .value("get_terminal_size", runtime.make_native_function("os.get_terminal_size", os_get_terminal_size, os_state))
      .function("_exit", os_exit)
      .function("listdir", os_listdir)
      .value("scandir", runtime.make_native_function("os.scandir", os_scandir, os_state))
      .value("DirEntry", os_state->dir_entry_class)
      .function("mkdir", os_mkdir, nullptr, false, os_mkdir_kw)
      .function("umask", os_umask)
      .function("makedirs", os_makedirs, nullptr, false, os_makedirs_kw)
      .function("remove", os_remove)
      .function("unlink", os_remove)
      .function("rmdir", os_rmdir)
      .function("rename", os_rename)
      .function("replace", os_replace)
      .value("utime", runtime.make_native_function("os.utime", os_utime, nullptr, nullptr, nullptr, false, os_utime_kw))
      .value("chmod", runtime.make_native_function(
          "os.chmod", os_chmod, nullptr, nullptr, nullptr, false, os_chmod_kw))
      .value("stat", std::move(stat_function))
      .value("lstat", runtime.make_native_function("os.lstat", os_lstat, os_state))
      .value("stat_result", os_state->stat_result_class)
      .value("terminal_size", os_state->terminal_size_class)
      .function("access", os_access)
      .function("getenv", os_getenv)
      .function("putenv", os_putenv)
      .function("unsetenv", os_unsetenv)
      .function("_create_environ", os_create_environ)
      .function("fspath", os_fspath)
#if defined(_WIN32)
      .value("P_WAIT", Value::int64(_P_WAIT))
      .value("P_NOWAIT", Value::int64(_P_NOWAIT))
      .value("P_NOWAITO", Value::int64(_P_NOWAITO))
      .value("P_OVERLAY", Value::int64(_P_OVERLAY))
      .value("P_DETACH", Value::int64(_P_DETACH))
      .function("_supports_virtual_terminal", os_supports_virtual_terminal)
      .function("_getfullpathname", os_getfullpathname)
      .function("_getfinalpathname", os_getfinalpathname)
      .function("_getvolumepathname", os_getvolumepathname)
      .function("_getdiskusage", os_getdiskusage)
      .function("listvolumes", os_listvolumes)
      .function("listdrives", os_listdrives)
      .function("listmounts", os_listmounts)
      .function("device_encoding", os_device_encoding)
      .function("_path_splitroot_ex", os_path_splitroot_ex)
      .function("_path_isdevdrive", os_path_isdevdrive)
      .function("_path_isdir", os_path_isdir, nullptr, false, os_path_isdir_kw)
      .function("_path_isfile", os_path_isfile, nullptr, false, os_path_isfile_kw)
      .function("_path_islink", os_path_islink, nullptr, false, os_path_islink_kw)
      .function("_path_isjunction", os_path_isjunction, nullptr, false, os_path_isjunction_kw)
      .function("_path_exists", os_path_exists, nullptr, false, os_path_exists_kw)
      .function("_path_lexists", os_path_exists, nullptr, false, os_path_exists_kw)
#endif
      .value("environ", env_dict)
      .value("F_OK", Value::int64(0))
      .value("R_OK", Value::int64(4))
      .value("W_OK", Value::int64(2))
      .value("X_OK", Value::int64(1))
      .value("SEEK_SET", Value::int64(SEEK_SET))
      .value("SEEK_CUR", Value::int64(SEEK_CUR))
      .value("SEEK_END", Value::int64(SEEK_END))
#if defined(_WIN32)
      .value("O_RDONLY", Value::int64(_O_RDONLY))
      .value("O_WRONLY", Value::int64(_O_WRONLY))
      .value("O_RDWR", Value::int64(_O_RDWR))
      .value("O_APPEND", Value::int64(_O_APPEND))
      .value("O_CREAT", Value::int64(_O_CREAT))
      .value("O_TRUNC", Value::int64(_O_TRUNC))
      .value("O_EXCL", Value::int64(_O_EXCL))
      .value("O_TEXT", Value::int64(_O_TEXT))
      .value("O_BINARY", Value::int64(_O_BINARY))
      .value("O_NOINHERIT", Value::int64(_O_NOINHERIT))
#ifdef _O_TEMPORARY
      .value("O_TEMPORARY", Value::int64(_O_TEMPORARY))
#endif
#ifdef _O_SHORT_LIVED
      .value("O_SHORT_LIVED", Value::int64(_O_SHORT_LIVED))
#endif
      .value("O_NONBLOCK", Value::int64(0))
#else
      .value("O_RDONLY", Value::int64(O_RDONLY))
      .value("O_WRONLY", Value::int64(O_WRONLY))
      .value("O_RDWR", Value::int64(O_RDWR))
      .value("O_APPEND", Value::int64(O_APPEND))
      .value("O_CREAT", Value::int64(O_CREAT))
      .value("O_TRUNC", Value::int64(O_TRUNC))
      .value("O_EXCL", Value::int64(O_EXCL))
      .value("O_NONBLOCK", Value::int64(O_NONBLOCK))
#endif
      .value("supports_dir_fd", Value::frozenset({}))
      .value("supports_effective_ids", Value::frozenset({}))
      .value("supports_fd", Value::frozenset({}))
      .value("supports_follow_symlinks", Value::frozenset({}))
#if defined(_WIN32)
      .value("name", Value::string("nt"))
      .value("sep", Value::string("\\"))
      .value("altsep", Value::string("/"))
      .value("pathsep", Value::string(";"))
      .value("devnull", Value::string("NUL"))
      .value("curdir", Value::string("."))
      .value("pardir", Value::string(".."));
#else
      .value("name", Value::string("posix"))
      .value("sep", Value::string("/"))
      .value("altsep", Value::none())
      .value("pathsep", Value::string(":"))
      .value("devnull", Value::string("/dev/null"))
      .value("curdir", Value::string("."))
      .value("pardir", Value::string(".."));
#endif
  auto module = builder.finish();
#if defined(_WIN32)
  runtime.register_module("nt", std::move(module));
#else
  runtime.register_module("posix", std::move(module));
#endif
}

} // namespace xlang3
