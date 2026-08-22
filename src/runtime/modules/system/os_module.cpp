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

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/vfs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <system_error>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace xlang3 {

namespace {

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
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

bool os_chdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.chdir() expected one argument";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "os.chdir path", path, error)) {
    return false;
  }
  if (!runtime.vfs().chdir(path, error)) {
    return false;
  }
  value_set_none(out);
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
  std::string path = ".";
  if (argc == 1 && !get_string_arg(args[0], "os.listdir path", path, error)) {
    return false;
  }
  std::vector<std::string> names;
  if (!runtime.vfs().list_dir(path, names, error)) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(names.size());
  for (auto& name : names) {
    values.push_back(Value::string(std::move(name)));
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
  }
  return {};
}

Value make_stat_result(const std::filesystem::directory_entry& entry) {
  std::error_code ec;
  const uintmax_t size = entry.is_regular_file(ec) ? entry.file_size(ec) : 0;
  auto write_time = entry.last_write_time(ec);
  int64_t mtime_ns = 0;
  if (!ec) {
    mtime_ns = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(write_time.time_since_epoch()).count());
  }

  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"st_size", Value::int64(static_cast<int64_t>(size))});
  attrs.push_back({"st_mtime_ns", Value::int64(mtime_ns)});
  attrs.push_back({"st_mtime", Value::number(static_cast<double>(mtime_ns) / 1000000000.0)});
  Value klass = Value::class_object("stat_result", std::move(attrs));
  return Value::instance(std::move(klass));
}

bool dir_entry_is_dir(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "DirEntry.is_dir() expected optional follow_symlinks";
    return false;
  }
  std::error_code ec;
  out = Value::boolean(std::filesystem::is_directory(dir_entry_path(args[0]), ec));
  return true;
}

bool dir_entry_is_file(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "DirEntry.is_file() expected optional follow_symlinks";
    return false;
  }
  std::error_code ec;
  out = Value::boolean(std::filesystem::is_regular_file(dir_entry_path(args[0]), ec));
  return true;
}

bool dir_entry_stat(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "DirEntry.stat() expected optional follow_symlinks";
    return false;
  }
  const std::filesystem::directory_entry entry(dir_entry_path(args[0]));
  out = make_stat_result(entry);
  return true;
}

Value make_dir_entry(Runtime& runtime, const std::filesystem::directory_entry& entry) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("os")});
  attrs.push_back({"is_dir", runtime.make_native_function("os.DirEntry.is_dir", dir_entry_is_dir)});
  attrs.push_back({"is_file", runtime.make_native_function("os.DirEntry.is_file", dir_entry_is_file)});
  attrs.push_back({"stat", runtime.make_native_function("os.DirEntry.stat", dir_entry_stat)});
  Value klass = Value::class_object("DirEntry", std::move(attrs));
  Value instance = Value::instance(klass);

  const auto path = entry.path().string();
  const auto name = entry.path().filename().string();
  std::string ignored;
  object_set_attr(instance, "path", Value::string(path), ignored);
  object_set_attr(instance, "name", Value::string(name), ignored);
  return instance;
}

bool os_scandir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "os.scandir() expected at most one argument";
    return false;
  }
  std::string path = ".";
  if (argc == 1 && !get_string_arg(args[0], "os.scandir path", path, error)) {
    return false;
  }
  std::error_code ec;
  std::filesystem::directory_iterator it(path, ec);
  if (ec) {
    error = ec.message();
    return false;
  }
  std::vector<Value> entries;
  for (const auto& entry : it) {
    entries.push_back(make_dir_entry(runtime, entry));
  }
  out = Value::list(std::move(entries));
  return true;
}

bool os_remove(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.remove() expected one argument";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "os.remove path", path, error)) {
    return false;
  }
  if (!runtime.vfs().remove(path, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool os_makedirs(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "os.makedirs() expected path and optional exist_ok";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "os.makedirs path", path, error)) {
    return false;
  }
  const bool exist_ok = argc == 2 && value_truthy(args[1]);
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    if (exist_ok || std::filesystem::is_directory(path, ec)) {
      value_set_none(out);
      return true;
    }
    error = "path exists and is not a directory";
    return false;
  }
  if (!std::filesystem::create_directories(path, ec) && ec) {
    error = ec.message();
    return false;
  }
  value_set_none(out);
  return true;
}

bool os_stat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.stat() expected one argument";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "os.stat path", path, error)) {
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(path, stat, error)) {
    return false;
  }
  const int64_t mode = stat.kind == VfsNodeKind::Directory ? 0040000 : stat.kind == VfsNodeKind::File ? 0100000 : 0;
  out = Value::tuple({
      Value::int64(mode),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(static_cast<int64_t>(stat.size)),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
  });
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

bool os_fspath(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.fspath() expected one argument";
    return false;
  }
  if (value_as_string(args[0]) != nullptr) {
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
  error = "expected str, bytes or os.PathLike object";
  return false;
}

bool path_unary(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "os.path function expected one argument";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "path", path, error)) {
    return false;
  }
  const char* op = static_cast<const char*>(user_data);
  std::filesystem::path fs_path(path);
  std::error_code ec;
  if (std::string(op) == "abspath") {
    out = Value::string(std::filesystem::absolute(fs_path, ec).lexically_normal().string());
  } else if (std::string(op) == "realpath") {
    out = Value::string(std::filesystem::absolute(fs_path, ec).lexically_normal().string());
  } else if (std::string(op) == "normpath") {
    out = Value::string(fs_path.lexically_normal().string());
  } else if (std::string(op) == "normcase") {
    auto normalized = fs_path.lexically_normal().string();
#if defined(_WIN32)
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
#endif
    out = Value::string(std::move(normalized));
  } else if (std::string(op) == "dirname") {
    out = Value::string(fs_path.parent_path().string());
  } else if (std::string(op) == "basename") {
    out = Value::string(fs_path.filename().string());
  } else if (std::string(op) == "exists") {
    out = Value::boolean(std::filesystem::exists(fs_path, ec));
  } else if (std::string(op) == "isdir") {
    out = Value::boolean(std::filesystem::is_directory(fs_path, ec));
  } else if (std::string(op) == "isfile") {
    out = Value::boolean(std::filesystem::is_regular_file(fs_path, ec));
  } else if (std::string(op) == "isabs") {
    out = Value::boolean(fs_path.is_absolute());
  } else {
    value_assign_fast(out, args[0]);
  }
  return true;
}

size_t last_path_separator(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t backslash = path.find_last_of('\\');
  if (slash == std::string::npos) {
    return backslash;
  }
  if (backslash == std::string::npos) {
    return slash;
  }
  return std::max(slash, backslash);
}

bool path_splitext(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "splitext() expected one path";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "path", path, error)) {
    return false;
  }

  const size_t sep = last_path_separator(path);
  const size_t filename_start = sep == std::string::npos ? 0 : sep + 1;
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos || dot < filename_start || dot == filename_start) {
    out = Value::tuple({Value::string(path), Value::string("")});
    return true;
  }
  out = Value::tuple({Value::string(path.substr(0, dot)), Value::string(path.substr(dot))});
  return true;
}

bool path_splitdrive(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "splitdrive() expected one path";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "path", path, error)) {
    return false;
  }

#if defined(_WIN32)
  if (path.size() >= 2 && path[1] == ':' && std::isalpha(static_cast<unsigned char>(path[0]))) {
    out = Value::tuple({Value::string(path.substr(0, 2)), Value::string(path.substr(2))});
    return true;
  }
  if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
    size_t server_end = path.find('\\', 2);
    if (server_end != std::string::npos) {
      size_t share_end = path.find('\\', server_end + 1);
      if (share_end != std::string::npos) {
        out = Value::tuple({Value::string(path.substr(0, share_end)), Value::string(path.substr(share_end))});
        return true;
      }
    }
  }
#endif
  out = Value::tuple({Value::string(""), Value::string(path)});
  return true;
}

bool path_expanduser(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "expanduser() expected one path";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "path", path, error)) {
    return false;
  }
  if (path.empty() || path[0] != '~') {
    out = Value::string(std::move(path));
    return true;
  }
  const char* home = std::getenv("USERPROFILE");
  if (home == nullptr) {
    home = std::getenv("HOME");
  }
  if (home == nullptr) {
    out = Value::string(std::move(path));
    return true;
  }
  out = Value::string(std::string(home) + path.substr(1));
  return true;
}

bool path_join(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    error = "join() expected at least one path";
    return false;
  }
  std::filesystem::path joined;
  for (uint32_t i = 0; i < argc; ++i) {
    std::string part;
    if (!get_string_arg(args[i], "path", part, error)) {
      return false;
    }
    if (i == 0) {
      joined = std::filesystem::path(part);
    } else {
      joined /= std::filesystem::path(part);
    }
  }
  out = Value::string(joined.string());
  return true;
}

Value make_os_path_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "os.path");
  builder.value("abspath", runtime.make_native_function("os.path.abspath", path_unary, const_cast<char*>("abspath")))
      .value("realpath", runtime.make_native_function("os.path.realpath", path_unary, const_cast<char*>("realpath")))
      .value("normpath", runtime.make_native_function("os.path.normpath", path_unary, const_cast<char*>("normpath")))
      .value("normcase", runtime.make_native_function("os.path.normcase", path_unary, const_cast<char*>("normcase")))
      .value("dirname", runtime.make_native_function("os.path.dirname", path_unary, const_cast<char*>("dirname")))
      .value("basename", runtime.make_native_function("os.path.basename", path_unary, const_cast<char*>("basename")))
      .value("exists", runtime.make_native_function("os.path.exists", path_unary, const_cast<char*>("exists")))
      .value("isdir", runtime.make_native_function("os.path.isdir", path_unary, const_cast<char*>("isdir")))
      .value("isfile", runtime.make_native_function("os.path.isfile", path_unary, const_cast<char*>("isfile")))
      .value("isabs", runtime.make_native_function("os.path.isabs", path_unary, const_cast<char*>("isabs")))
      .function("join", path_join)
      .function("splitext", path_splitext)
      .function("splitdrive", path_splitdrive)
      .function("expanduser", path_expanduser)
#if defined(_WIN32)
      .value("sep", Value::string("\\"))
      .value("altsep", Value::string("/"))
      .value("pathsep", Value::string(";"))
#else
      .value("sep", Value::string("/"))
      .value("altsep", Value())
      .value("pathsep", Value::string(":"))
#endif
      .value("extsep", Value::string("."))
      .value("curdir", Value::string("."))
      .value("pardir", Value::string(".."));
  return builder.finish();
}

} // namespace

void register_os_module(Runtime& runtime) {
  Value path_module = make_os_path_module(runtime);
  Value env_dict = Value::dict({});
  NativeModuleBuilder builder(runtime, "os");
  builder.function("getcwd", os_getcwd)
      .function("chdir", os_chdir)
      .function("urandom", os_urandom)
      .function("getpid", os_getpid)
      .function("getppid", os_getppid)
      .function("_exit", os_exit)
      .function("listdir", os_listdir)
      .function("scandir", os_scandir)
      .function("makedirs", os_makedirs)
      .function("remove", os_remove)
      .function("unlink", os_remove)
      .function("stat", os_stat)
      .function("getenv", os_getenv)
      .function("fspath", os_fspath)
      .value("path", path_module)
      .value("environ", env_dict)
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
  runtime.register_module("os", module);
  runtime.register_module("os.path", path_module);
#if defined(_WIN32)
  runtime.register_module("ntpath", path_module);
  runtime.register_module("nt", std::move(module));
#else
  runtime.register_module("posixpath", path_module);
  runtime.register_module("posix", std::move(module));
#endif
}

} // namespace xlang3
