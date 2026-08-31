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

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace xlang3 {

namespace {

constexpr const char* kScandirIteratorNativeType = "os.ScandirIterator";

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
  extern char** environ;
  if (environ != nullptr) {
    for (char** current = environ; *current != nullptr; ++current) {
      std::string_view item(*current);
      const size_t equals = item.find('=');
      if (equals == std::string_view::npos) {
        continue;
      }
      entries.push_back({
          Value::string(std::string(item.substr(0, equals))),
          Value::string(std::string(item.substr(equals + 1)))});
    }
  }
#endif
  return Value::dict(std::move(entries));
}

struct PathArg {
  std::string text;
  bool bytes = false;
};

struct ScandirState {
  std::vector<Value> entries;
  size_t index = 0;
  bool closed = false;
};

struct OsModuleState {
  Value dir_entry_class;
  Value scandir_iterator_class;
  Value stat_result_class;
  Value terminal_size_class;
};

Value make_terminal_size(const Value& klass, int64_t columns, int64_t lines);

void scandir_state_cleanup(void* data) {
  delete static_cast<ScandirState*>(data);
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
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    out.text = bytes_object_to_string(*bytes);
    out.bytes = true;
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out.text = bytearray->value;
    out.bytes = true;
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
  if (object_get_attr(value, "__fspath__", fspath, ignored)) {
    Value result;
    std::string call_error;
    if (!runtime_call_callable(runtime, fspath, nullptr, 0, result, call_error)) {
      error = call_error.empty() ? std::string(name) + " __fspath__ failed" : call_error;
      return false;
    }
    if (get_path_arg(runtime, result, name, out, error)) {
      return true;
    }
  }

  error = std::string(name) + " must be str, bytes, or os.PathLike";
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

bool os_getcwd(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "os.getcwd", error)) {
    return false;
  }
  out = Value::string(runtime.vfs().cwd());
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
    return false;
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
  value_set_int64(out, 0);
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
  const int fd = _open(path.text.c_str(), static_cast<int>(args[1].as.i64), mode);
#else
  const int fd = ::open(path.text.c_str(), static_cast<int>(args[1].as.i64), static_cast<mode_t>(mode));
#endif
  if (fd < 0) {
    error = "open failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_int64(out, fd);
  return true;
}

bool os_close(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "close() expected fd";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const int rc = _close(static_cast<int>(args[0].as.i64));
#else
  const int rc = ::close(static_cast<int>(args[0].as.i64));
#endif
  if (rc != 0) {
    error = "close failed";
    runtime.raise_class_error("OSError", error);
    return false;
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
  const int count = _read(static_cast<int>(args[0].as.i64), buffer.data(), static_cast<unsigned int>(buffer.size()));
#else
  const ssize_t count = ::read(static_cast<int>(args[0].as.i64), buffer.data(), buffer.size());
#endif
  if (count < 0) {
    error = "read failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  buffer.resize(static_cast<size_t>(count));
  out = Value::bytes(std::move(buffer));
  return true;
}

bool os_write(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64) {
    error = "write() expected fd and bytes-like data";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string owned;
  std::string_view data;
  if (auto* bytes = value_as_bytes(args[1])) {
    data = bytes_object_view(*bytes);
  } else if (auto* bytearray = value_as_bytearray(args[1])) {
    data = std::string_view(bytearray->value.data(), bytearray->value.size());
  } else {
    error = "write data must be bytes-like";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const int count = _write(static_cast<int>(args[0].as.i64), data.data(), static_cast<unsigned int>(data.size()));
#else
  const ssize_t count = ::write(static_cast<int>(args[0].as.i64), data.data(), data.size());
#endif
  if (count < 0) {
    error = "write failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(count));
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
    error = "could not query terminal size";
    runtime.raise_class_error("OSError", error);
    return false;
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
    return false;
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

Value make_stat_result_class(Runtime& runtime) {
  const Value* tuple_base = runtime.find_builtin("tuple");
  return Value::class_object(
      "stat_result",
      {
          {"__module__", Value::string("os")},
          {"__qualname__", Value::string("stat_result")},
          {"__repr__", runtime.make_native_function("os.stat_result.__repr__", os_stat_result_repr, nullptr, nullptr, nullptr, false, os_stat_result_repr_kw)},
          {"__str__", runtime.make_native_function("os.stat_result.__str__", os_stat_result_repr, nullptr, nullptr, nullptr, false, os_stat_result_repr_kw)},
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
}

Value make_stat_result(const Value& klass, const VfsStat& stat) {
  const int64_t mode = stat.kind == VfsNodeKind::Directory ? 0040000 : stat.kind == VfsNodeKind::File ? 0100000 : 0;
  const double atime = static_cast<double>(stat.atime_ns) / 1000000000.0;
  const double mtime = static_cast<double>(stat.mtime_ns) / 1000000000.0;
  const double ctime = static_cast<double>(stat.ctime_ns) / 1000000000.0;
  std::vector<Value> tuple_items = {
      Value::int64(mode),
      Value::int64(static_cast<int64_t>(stat.inode)),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(static_cast<int64_t>(stat.size)),
      Value::number(atime),
      Value::number(mtime),
      Value::number(ctime),
  };

  Value instance = Value::instance(klass);
  std::string ignored;
  object_set_attr(instance, "_tuple", Value::tuple(tuple_items), ignored);
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
  return Value::class_object(
      "terminal_size",
      {
          {"__module__", Value::string("os")},
          {"__qualname__", Value::string("terminal_size")},
          {"n_sequence_fields", Value::int64(2)},
          {"n_fields", Value::int64(2)},
          {"n_unnamed_fields", Value::int64(0)},
          {"columns", slot_descriptor("os.terminal_size", "columns", 0)},
          {"lines", slot_descriptor("os.terminal_size", "lines", 1)},
          {"__match_args__", terminal_size_match_args()},
      },
      tuple_base != nullptr ? *tuple_base : Value::invalid());
}

Value make_terminal_size(const Value& klass, int64_t columns, int64_t lines) {
  Value instance = Value::instance(klass);
  std::string ignored;
  object_set_attr(instance, "_tuple", Value::tuple({Value::int64(columns), Value::int64(lines)}), ignored);
  return instance;
}

bool dir_entry_is_dir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "DirEntry.is_dir() expected optional follow_symlinks";
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(dir_entry_path(args[0]), stat, error)) {
    return false;
  }
  out = Value::boolean(stat.kind == VfsNodeKind::Directory);
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
  VfsStat stat;
  if (!runtime.vfs().stat(dir_entry_path(args[0]), stat, error)) {
    return false;
  }
  out = Value::boolean(stat.kind == VfsNodeKind::File);
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
  VfsStat stat;
  if (!runtime.vfs().stat(dir_entry_path(args[0]), stat, error)) {
    return false;
  }
  value_set_bool(out, stat.is_symlink);
  return true;
}

bool dir_entry_stat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "DirEntry.stat() expected optional follow_symlinks";
    return false;
  }
  auto* state = static_cast<OsModuleState*>(user_data);
  if (state == nullptr) {
    error = "DirEntry.stat() missing os module state";
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(dir_entry_path(args[0]), stat, error)) {
    return false;
  }
  out = make_stat_result(state->stat_result_class, stat);
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
  if (!runtime.vfs().stat(dir_entry_path(args[0]), stat, error)) {
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(stat.inode));
  return true;
}

Value make_dir_entry_class(Runtime& runtime, OsModuleState* os_state) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("os")});
  attrs.push_back({"inode", runtime.make_native_function("os.DirEntry.inode", dir_entry_inode)});
  attrs.push_back({"is_dir", runtime.make_native_function("os.DirEntry.is_dir", dir_entry_is_dir, nullptr, nullptr, nullptr, false, dir_entry_is_dir_kw)});
  attrs.push_back({"is_file", runtime.make_native_function("os.DirEntry.is_file", dir_entry_is_file, nullptr, nullptr, nullptr, false, dir_entry_is_file_kw)});
  attrs.push_back({"is_symlink", runtime.make_native_function("os.DirEntry.is_symlink", dir_entry_is_symlink)});
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
  return base + "/" + name;
}

Value make_dir_entry(const Value& klass, std::string path, std::string name, bool bytes) {
  Value instance = Value::instance(klass);
  std::string ignored;
  object_set_attr(instance, "path", path_name_value(path, bytes), ignored);
  object_set_attr(instance, "name", path_name_value(name, bytes), ignored);
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
      return false;
    }
  }
  std::vector<std::string> names;
  if (!runtime.vfs().list_dir(path.text, names, error)) {
    return false;
  }
  const auto* module_state = static_cast<OsModuleState*>(user_data);
  if (module_state == nullptr) {
    error = "os.scandir module state is missing";
    return false;
  }
  auto* state = new ScandirState();
  state->entries.reserve(names.size());
  for (auto& name : names) {
    const std::string full_path = join_vfs_path(path.text, name);
    state->entries.push_back(make_dir_entry(module_state->dir_entry_class, full_path, std::move(name), path.bytes));
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
    return false;
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
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? std::string() : std::string(kwargs[i].name);
    if (name != "mode" && name != "dir_fd") {
      error = "os.mkdir() got unexpected keyword argument '" + name + "'";
      return false;
    }
  }
  const std::string parent = std::filesystem::path(path.text).parent_path().string();
  if (!parent.empty()) {
    VfsStat stat;
    if (!runtime.vfs().stat(parent, stat, error)) {
      return false;
    }
    if (stat.kind != VfsNodeKind::Directory) {
      error = "parent directory does not exist: " + parent;
      return false;
    }
  }
  if (!runtime.vfs().make_dirs(path.text, false, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool os_mkdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return os_mkdir_impl(runtime, args, argc, nullptr, 0, out, error);
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
    return false;
  }
  if (stat.kind != VfsNodeKind::Directory) {
    error = "not a directory: " + path.text;
    return false;
  }
  if (!runtime.vfs().remove(path.text, error)) {
    return false;
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
    return false;
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
  PathArg path;
  if (!get_path_arg(runtime, args[0], "os.stat path", path, error)) {
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(path.text, stat, error)) {
    return false;
  }
  if (stat.kind == VfsNodeKind::Missing) {
    error = "file not found: " + path.text;
    runtime.raise_class_error("FileNotFoundError", error);
    return false;
  }
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
    return false;
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

bool os_fspath(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.fspath() expected one argument";
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
  if (object_get_attr(args[0], "__fspath__", fspath, ignored)) {
    Value result;
    std::string call_error;
    if (!runtime_call_callable(runtime, fspath, nullptr, 0, result, call_error)) {
      error = call_error.empty() ? "__fspath__ failed" : call_error;
      return false;
    }
    if (value_as_string(result) != nullptr || value_as_bytes(result) != nullptr) {
      value_assign_fast(out, result);
      return true;
    }
    error = "__fspath__() must return str or bytes";
    return false;
  }
  error = "expected str, bytes or os.PathLike object";
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
#endif

} // namespace

void register_os_module(Runtime& runtime) {
  Value env_dict = make_process_environ_dict();
  auto* os_state = new OsModuleState();
  os_state->stat_result_class = make_stat_result_class(runtime);
  os_state->terminal_size_class = make_terminal_size_class(runtime);
  os_state->dir_entry_class = make_dir_entry_class(runtime, os_state);
  os_state->scandir_iterator_class = make_scandir_iterator_class(runtime);
  runtime.register_native_package_cleanup(os_state, os_module_state_cleanup);

#if defined(_WIN32)
  NativeModuleBuilder builder(runtime, "nt");
#else
  NativeModuleBuilder builder(runtime, "posix");
#endif
  builder.function("getcwd", os_getcwd)
      .function("getcwdb", os_getcwdb)
      .function("chdir", os_chdir)
      .function("fsencode", os_fsencode)
      .function("fsdecode", os_fsdecode)
      .function("urandom", os_urandom)
      .function("open", os_open)
      .function("close", os_close)
      .function("read", os_read)
      .function("write", os_write)
      .function("getpid", os_getpid)
      .function("getppid", os_getppid)
      .function("cpu_count", os_cpu_count)
      .value("get_terminal_size", runtime.make_native_function("os.get_terminal_size", os_get_terminal_size, os_state))
      .function("_exit", os_exit)
      .function("listdir", os_listdir)
      .value("scandir", runtime.make_native_function("os.scandir", os_scandir, os_state))
      .value("DirEntry", os_state->dir_entry_class)
      .function("mkdir", os_mkdir, nullptr, false, os_mkdir_kw)
      .function("makedirs", os_makedirs, nullptr, false, os_makedirs_kw)
      .function("remove", os_remove)
      .function("unlink", os_remove)
      .function("rmdir", os_rmdir)
      .function("rename", os_rename)
      .function("replace", os_replace)
      .value("stat", runtime.make_native_function("os.stat", os_stat, os_state, nullptr, nullptr, false, os_stat_kw))
      .value("stat_result", os_state->stat_result_class)
      .value("terminal_size", os_state->terminal_size_class)
      .function("access", os_access)
      .function("getenv", os_getenv)
      .function("fspath", os_fspath)
#if defined(_WIN32)
      .function("_supports_virtual_terminal", os_supports_virtual_terminal)
#endif
      .value("environ", env_dict)
      .value("F_OK", Value::int64(0))
      .value("R_OK", Value::int64(4))
      .value("W_OK", Value::int64(2))
      .value("X_OK", Value::int64(1))
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
      .value("altsep", Value())
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
