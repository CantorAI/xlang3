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

constexpr const char* kScandirIteratorNativeType = "os.ScandirIterator";

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
};

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

Value make_stat_result(const VfsStat& stat) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"st_ino", Value::int64(static_cast<int64_t>(stat.inode))});
  attrs.push_back({"st_size", Value::int64(static_cast<int64_t>(stat.size))});
  attrs.push_back({"st_mtime_ns", Value::int64(0)});
  attrs.push_back({"st_mtime", Value::number(0.0)});
  Value klass = Value::class_object("stat_result", std::move(attrs));
  return Value::instance(std::move(klass));
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

bool dir_entry_stat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "DirEntry.stat() expected optional follow_symlinks";
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(dir_entry_path(args[0]), stat, error)) {
    return false;
  }
  out = make_stat_result(stat);
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

Value make_dir_entry_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("os")});
  attrs.push_back({"inode", runtime.make_native_function("os.DirEntry.inode", dir_entry_inode)});
  attrs.push_back({"is_dir", runtime.make_native_function("os.DirEntry.is_dir", dir_entry_is_dir, nullptr, nullptr, nullptr, false, dir_entry_is_dir_kw)});
  attrs.push_back({"is_file", runtime.make_native_function("os.DirEntry.is_file", dir_entry_is_file, nullptr, nullptr, nullptr, false, dir_entry_is_file_kw)});
  attrs.push_back({"is_symlink", runtime.make_native_function("os.DirEntry.is_symlink", dir_entry_is_symlink)});
  attrs.push_back({"stat", runtime.make_native_function("os.DirEntry.stat", dir_entry_stat, nullptr, nullptr, nullptr, false, dir_entry_stat_kw)});
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

bool os_stat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.stat() expected one argument";
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
  const int64_t mode = stat.kind == VfsNodeKind::Directory ? 0040000 : stat.kind == VfsNodeKind::File ? 0100000 : 0;
  out = Value::tuple({
      Value::int64(mode),
      Value::int64(static_cast<int64_t>(stat.inode)),
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

bool path_unary(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
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
  if (std::string(op) == "abspath") {
    if (path.empty()) {
      out = Value::string(runtime.vfs().cwd());
      return true;
    }
    ResolvedPath resolved;
    if (!runtime.vfs().resolve(path, resolved, error)) {
      return false;
    }
    out = Value::string(std::move(resolved.path));
  } else if (std::string(op) == "realpath") {
    if (path.empty()) {
      out = Value::string(runtime.vfs().cwd());
      return true;
    }
    ResolvedPath resolved;
    if (!runtime.vfs().resolve(path, resolved, error)) {
      return false;
    }
    out = Value::string(std::move(resolved.path));
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
    VfsStat stat;
    if (!runtime.vfs().stat(path, stat, error)) {
      return false;
    }
    out = Value::boolean(stat.kind != VfsNodeKind::Missing);
  } else if (std::string(op) == "lexists") {
    VfsStat stat;
    if (!runtime.vfs().stat(path, stat, error)) {
      return false;
    }
    out = Value::boolean(stat.kind != VfsNodeKind::Missing);
  } else if (std::string(op) == "isdir") {
    VfsStat stat;
    if (!runtime.vfs().stat(path, stat, error)) {
      return false;
    }
    out = Value::boolean(stat.kind == VfsNodeKind::Directory);
  } else if (std::string(op) == "isfile") {
    VfsStat stat;
    if (!runtime.vfs().stat(path, stat, error)) {
      return false;
    }
    out = Value::boolean(stat.kind == VfsNodeKind::File);
  } else if (std::string(op) == "isabs") {
    out = Value::boolean(fs_path.is_absolute());
  } else {
    value_assign_fast(out, args[0]);
  }
  return true;
}

bool path_getsize(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "getsize() expected one path";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "path", path, error)) {
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(path, stat, error)) {
    return false;
  }
  if (stat.kind == VfsNodeKind::Missing) {
    error = "No such file or directory: " + path;
    return false;
  }
  out = Value::int64(static_cast<int64_t>(stat.size));
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

bool path_split(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "split() expected one path";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "path", path, error)) {
    return false;
  }

  const size_t sep = last_path_separator(path);
  if (sep == std::string::npos) {
    out = Value::tuple({Value::string(""), Value::string(path)});
    return true;
  }
  size_t head_end = sep;
  while (head_end > 0 && (path[head_end - 1] == '/' || path[head_end - 1] == '\\')) {
    --head_end;
  }
  const std::string head = sep == 0 ? path.substr(0, 1) : path.substr(0, head_end);
  out = Value::tuple({Value::string(head), Value::string(path.substr(sep + 1))});
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

bool collect_path_sequence(const Value& value, std::vector<std::string>& paths, std::string& error) {
  if (auto* list = value_as_list(value)) {
    paths.reserve(list->items.size());
    for (const auto& item : list->items) {
      std::string path;
      if (!get_string_arg(item, "path", path, error)) {
        return false;
      }
      paths.push_back(std::move(path));
    }
    return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    paths.reserve(tuple->items.size());
    for (const auto& item : tuple->items) {
      std::string path;
      if (!get_string_arg(item, "path", path, error)) {
        return false;
      }
      paths.push_back(std::move(path));
    }
    return true;
  }
  error = "expected sequence of path strings";
  return false;
}

bool path_commonpath(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "commonpath() expected iterable";
    return false;
  }
  std::vector<std::string> paths;
  if (!collect_path_sequence(args[0], paths, error)) {
    return false;
  }
  if (paths.empty()) {
    error = "commonpath() arg is an empty sequence";
    return false;
  }

  std::vector<std::filesystem::path> normalized;
  normalized.reserve(paths.size());
  const bool first_absolute = std::filesystem::path(paths[0]).is_absolute();
  for (const auto& path : paths) {
    std::filesystem::path fs_path(path);
    if (fs_path.is_absolute() != first_absolute) {
      error = "Can't mix absolute and relative paths";
      return false;
    }
    normalized.push_back(fs_path.lexically_normal());
  }

  std::vector<std::filesystem::path> prefix;
  for (const auto& part : normalized[0]) {
    prefix.push_back(part);
  }
  for (size_t i = 1; i < normalized.size(); ++i) {
    std::vector<std::filesystem::path> parts;
    for (const auto& part : normalized[i]) {
      parts.push_back(part);
    }
    size_t keep = 0;
    const size_t limit = std::min(prefix.size(), parts.size());
    while (keep < limit && prefix[keep] == parts[keep]) {
      ++keep;
    }
    prefix.resize(keep);
  }

  std::filesystem::path result;
  for (const auto& part : prefix) {
    result /= part;
  }
  out = Value::string(result.empty() ? std::string(".") : result.string());
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

bool path_expandvars(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "expandvars() expected one path";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "path", path, error)) {
    return false;
  }
  std::string expanded;
  expanded.reserve(path.size());
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '$') {
      size_t start = i + 1;
      size_t end = start;
      if (start < path.size() && path[start] == '{') {
        start += 1;
        end = path.find('}', start);
        if (end == std::string::npos) {
          expanded.push_back(path[i]);
          continue;
        }
      } else {
        while (end < path.size() && (std::isalnum(static_cast<unsigned char>(path[end])) || path[end] == '_')) {
          ++end;
        }
      }
      if (end > start) {
        const std::string name = path.substr(start, end - start);
        const char* value = std::getenv(name.c_str());
        if (value != nullptr) {
          expanded += value;
        } else {
          expanded += path.substr(i, end - i + (start > i + 1 ? 1 : 0));
        }
        i = start > i + 1 ? end : end - 1;
        continue;
      }
    }
#if defined(_WIN32)
    if (path[i] == '%') {
      const size_t end = path.find('%', i + 1);
      if (end != std::string::npos && end > i + 1) {
        const std::string name = path.substr(i + 1, end - i - 1);
        const char* value = std::getenv(name.c_str());
        expanded += value != nullptr ? value : path.substr(i, end - i + 1);
        i = end;
        continue;
      }
    }
#endif
    expanded.push_back(path[i]);
  }
  out = Value::string(std::move(expanded));
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

bool path_relpath(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "relpath() expected path and optional start";
    return false;
  }
  std::string path;
  std::string start = ".";
  if (!get_string_arg(args[0], "path", path, error)) {
    return false;
  }
  if (argc == 2 && !get_string_arg(args[1], "start", start, error)) {
    return false;
  }
  ResolvedPath resolved_path;
  ResolvedPath resolved_start;
  if (!runtime.vfs().resolve(path, resolved_path, error) || !runtime.vfs().resolve(start, resolved_start, error)) {
    return false;
  }
  std::error_code ec;
  auto relative = std::filesystem::relative(resolved_path.path, resolved_start.path, ec);
  out = Value::string(ec ? resolved_path.path : relative.string());
  return true;
}

bool path_commonprefix(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "commonprefix() expected iterable";
    return false;
  }
  std::vector<std::string> paths;
  if (auto* list = value_as_list(args[0])) {
    paths.reserve(list->items.size());
    for (const auto& item : list->items) {
      std::string path;
      if (!get_string_arg(item, "path", path, error)) {
        return false;
      }
      paths.push_back(std::move(path));
    }
  } else if (auto* tuple = value_as_tuple(args[0])) {
    paths.reserve(tuple->items.size());
    for (const auto& item : tuple->items) {
      std::string path;
      if (!get_string_arg(item, "path", path, error)) {
        return false;
      }
      paths.push_back(std::move(path));
    }
  } else {
    error = "commonprefix() expected sequence";
    return false;
  }
  if (paths.empty()) {
    out = Value::string("");
    return true;
  }
  std::string prefix = paths[0];
  for (size_t i = 1; i < paths.size(); ++i) {
    size_t keep = 0;
    const size_t limit = std::min(prefix.size(), paths[i].size());
    while (keep < limit && prefix[keep] == paths[i][keep]) {
      ++keep;
    }
    prefix.resize(keep);
  }
  out = Value::string(std::move(prefix));
  return true;
}

bool path_samefile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "samefile() expected two paths";
    return false;
  }
  std::string left;
  std::string right;
  if (!get_string_arg(args[0], "path", left, error) || !get_string_arg(args[1], "path", right, error)) {
    return false;
  }
  ResolvedPath left_resolved;
  ResolvedPath right_resolved;
  if (!runtime.vfs().resolve(left, left_resolved, error) || !runtime.vfs().resolve(right, right_resolved, error)) {
    return false;
  }
  value_set_bool(out, left_resolved.fs == right_resolved.fs && left_resolved.path == right_resolved.path);
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
      .value("lexists", runtime.make_native_function("os.path.lexists", path_unary, const_cast<char*>("lexists")))
      .value("isdir", runtime.make_native_function("os.path.isdir", path_unary, const_cast<char*>("isdir")))
      .value("isfile", runtime.make_native_function("os.path.isfile", path_unary, const_cast<char*>("isfile")))
      .value("isabs", runtime.make_native_function("os.path.isabs", path_unary, const_cast<char*>("isabs")))
      .function("getsize", path_getsize)
      .function("join", path_join)
      .function("relpath", path_relpath)
      .function("commonprefix", path_commonprefix)
      .function("commonpath", path_commonpath)
      .function("samefile", path_samefile)
      .function("split", path_split)
      .function("splitext", path_splitext)
      .function("splitdrive", path_splitdrive)
      .function("expanduser", path_expanduser)
      .function("expandvars", path_expandvars)
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
  auto* os_state = new OsModuleState();
  os_state->dir_entry_class = make_dir_entry_class(runtime);
  os_state->scandir_iterator_class = make_scandir_iterator_class(runtime);
  runtime.register_native_package_cleanup(os_state, os_module_state_cleanup);

  NativeModuleBuilder builder(runtime, "os");
  builder.function("getcwd", os_getcwd)
      .function("getcwdb", os_getcwdb)
      .function("chdir", os_chdir)
      .function("fsencode", os_fsencode)
      .function("fsdecode", os_fsdecode)
      .function("urandom", os_urandom)
      .function("getpid", os_getpid)
      .function("getppid", os_getppid)
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
      .function("stat", os_stat)
      .function("access", os_access)
      .function("getenv", os_getenv)
      .function("fspath", os_fspath)
      .value("path", path_module)
      .value("environ", env_dict)
      .value("F_OK", Value::int64(0))
      .value("R_OK", Value::int64(4))
      .value("W_OK", Value::int64(2))
      .value("X_OK", Value::int64(1))
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
