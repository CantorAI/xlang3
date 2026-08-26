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
#include <string_view>

namespace xlang3 {

namespace {

Value make_path_instance(const Value& klass, const std::string& path, std::string& error);

std::string to_path_text(const Value& value) {
  if (auto* str = value_as_string(value)) {
    return string_object_to_string(*str);
  }
  std::string ignored;
  Value path_value;
  if (object_get_attr(value, "_path", path_value, ignored) && value_as_string(path_value) != nullptr) {
    return string_object_to_string(*value_as_string(path_value));
  }
  return value_to_string(value);
}

std::string normalize_slashes(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

std::string join_paths(std::string lhs, const std::string& rhs) {
  if (lhs.empty()) {
    return rhs;
  }
  if (rhs.empty()) {
    return lhs;
  }
  const char last = lhs.back();
  if (last == '/' || last == '\\') {
    return lhs + rhs;
  }
  return lhs + "/" + rhs;
}

std::string path_name(const std::string& path) {
  const std::string text = normalize_slashes(path);
  const auto pos = text.find_last_of('/');
  return pos == std::string::npos ? text : text.substr(pos + 1);
}

std::string path_parent(const std::string& path) {
  const std::string text = normalize_slashes(path);
  const auto pos = text.find_last_of('/');
  if (pos == std::string::npos) {
    return ".";
  }
  if (pos == 0) {
    return "/";
  }
  return text.substr(0, pos);
}

bool path_is_absolute_text(const std::string& path) {
  return (!path.empty() && (path[0] == '/' || path[0] == '\\')) || (path.size() >= 2 && path[1] == ':');
}

std::vector<std::string> split_path_parts(const std::string& path) {
  std::vector<std::string> values;
  std::string normalized = normalize_slashes(path);
  if (!normalized.empty() && normalized.front() == '/') {
    values.push_back("/");
  }
  size_t start = (!normalized.empty() && normalized.front() == '/') ? 1 : 0;
  while (start < normalized.size()) {
    const size_t slash = normalized.find('/', start);
    std::string part = slash == std::string::npos ? normalized.substr(start) : normalized.substr(start, slash - start);
    if (!part.empty()) {
      values.push_back(std::move(part));
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  return values;
}

std::string path_suffix(const std::string& path) {
  const std::string name = path_name(path);
  const auto pos = name.find_last_of('.');
  if (pos == std::string::npos || pos == 0) {
    return "";
  }
  return name.substr(pos);
}

std::string path_stem(const std::string& path) {
  const std::string name = path_name(path);
  const auto pos = name.find_last_of('.');
  if (pos == std::string::npos || pos == 0) {
    return name;
  }
  return name.substr(0, pos);
}

std::vector<std::string> path_suffixes(const std::string& path) {
  std::vector<std::string> suffixes;
  const std::string name = path_name(path);
  size_t pos = 0;
  while ((pos = name.find('.', pos)) != std::string::npos) {
    if (pos != 0 && pos + 1 < name.size()) {
      suffixes.push_back(name.substr(pos));
    }
    ++pos;
  }
  return suffixes;
}

bool path_match_pattern(std::string_view text, std::string_view pattern) {
  size_t ti = 0;
  size_t pi = 0;
  size_t star = std::string_view::npos;
  size_t match = 0;
  while (ti < text.size()) {
    if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
      ++ti;
      ++pi;
    } else if (pi < pattern.size() && pattern[pi] == '*') {
      star = pi++;
      match = ti;
    } else if (star != std::string_view::npos) {
      pi = star + 1;
      ti = ++match;
    } else {
      return false;
    }
  }
  while (pi < pattern.size() && pattern[pi] == '*') {
    ++pi;
  }
  return pi == pattern.size();
}

bool path_collect_glob(
    Runtime& runtime,
    const Value& klass,
    const std::string& base,
    const std::vector<std::string>& segments,
    size_t index,
    std::vector<Value>& out,
    std::string& error) {
  if (index >= segments.size()) {
    VfsStat stat;
    if (!runtime.vfs().stat(base, stat, error)) {
      return false;
    }
    if (stat.kind != VfsNodeKind::Missing) {
      out.push_back(make_path_instance(klass, base, error));
      return error.empty();
    }
    return true;
  }

  const std::string& segment = segments[index];
  if (segment == "**") {
    if (!path_collect_glob(runtime, klass, base, segments, index + 1, out, error)) {
      return false;
    }
    std::vector<std::string> names;
    VfsStat stat;
    if (!runtime.vfs().stat(base, stat, error)) {
      return false;
    }
    if (stat.kind != VfsNodeKind::Directory) {
      return true;
    }
    if (!runtime.vfs().list_dir(base, names, error)) {
      return false;
    }
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
      if (!name.empty() && name.front() == '.') {
        continue;
      }
      const std::string child = join_paths(base, name);
      if (!path_collect_glob(runtime, klass, child, segments, index, out, error)) {
        return false;
      }
    }
    return true;
  }

  const bool has_wildcard = segment.find('*') != std::string::npos || segment.find('?') != std::string::npos;
  if (!has_wildcard) {
    return path_collect_glob(runtime, klass, join_paths(base, segment), segments, index + 1, out, error);
  }

  VfsStat stat;
  if (!runtime.vfs().stat(base, stat, error)) {
    return false;
  }
  if (stat.kind != VfsNodeKind::Directory) {
    return true;
  }
  std::vector<std::string> names;
  if (!runtime.vfs().list_dir(base, names, error)) {
    return false;
  }
  std::sort(names.begin(), names.end());
  for (const auto& name : names) {
    if (!name.empty() && name.front() == '.') {
      continue;
    }
    if (path_match_pattern(name, segment)) {
      if (!path_collect_glob(runtime, klass, join_paths(base, name), segments, index + 1, out, error)) {
        return false;
      }
    }
  }
  return true;
}

bool set_path_attrs(Value& instance, const std::string& path, std::string& error) {
  const std::string display = normalize_slashes(path);
  return object_set_attr(instance, "_path", Value::string(path), error) &&
         object_set_attr(instance, "__xlang3_string_value__", Value::string(display), error);
}

Value make_path_instance(const Value& klass, const std::string& path, std::string& error) {
  Value instance = Value::instance(klass);
  set_path_attrs(instance, path, error);
  return instance;
}

bool path_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "Path.__init__ expected self";
    return false;
  }
  std::string path = ".";
  if (argc > 1) {
    path = to_path_text(args[1]);
    for (uint32_t i = 2; i < argc; ++i) {
      path = join_paths(path, to_path_text(args[i]));
    }
  }
  Value self = args[0];
  if (!set_path_attrs(self, path, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool path_as_posix(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.as_posix() expected no arguments";
    return false;
  }
  out = Value::string(normalize_slashes(to_path_text(args[0])));
  return true;
}

bool path_string(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.__str__() expected no arguments";
    return false;
  }
  out = Value::string(to_path_text(args[0]));
  return true;
}

bool path_repr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.__repr__() expected no arguments";
    return false;
  }
  out = Value::string("Path('" + normalize_slashes(to_path_text(args[0])) + "')");
  return true;
}

bool path_fspath(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.__fspath__() expected no arguments";
    return false;
  }
  out = Value::string(to_path_text(args[0]));
  return true;
}

bool path_joinpath(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "Path.joinpath() expected at least one argument";
    return false;
  }
  std::string path = to_path_text(args[0]);
  for (uint32_t i = 1; i < argc; ++i) {
    path = join_paths(path, to_path_text(args[i]));
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "Path.joinpath() self is invalid";
    return false;
  }
  out = make_path_instance(instance->klass, path, error);
  return error.empty();
}

bool path_truediv(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return path_joinpath(runtime, args, argc, out, error, user_data);
}

bool path_property_value(const Value* args, uint32_t argc, Value& out, std::string& error, const char* name) {
  if (argc != 1) {
    error = std::string("Path.") + name + " expected no arguments";
    return false;
  }
  const std::string path = to_path_text(args[0]);
  if (std::string(name) == "name") {
    out = Value::string(path_name(path));
  } else if (std::string(name) == "suffix") {
    out = Value::string(path_suffix(path));
  } else if (std::string(name) == "stem") {
    out = Value::string(path_stem(path));
  } else if (std::string(name) == "suffixes") {
    std::vector<Value> values;
    for (auto& suffix : path_suffixes(path)) {
      values.push_back(Value::string(std::move(suffix)));
    }
    out = Value::list(std::move(values));
  } else if (std::string(name) == "parts") {
    std::vector<Value> values;
    for (auto& part : split_path_parts(path)) {
      values.push_back(Value::string(std::move(part)));
    }
    out = Value::tuple(std::move(values));
  } else {
    auto* instance = value_as_instance(args[0]);
    if (instance == nullptr) {
      error = "Path parent self is invalid";
      return false;
    }
    out = make_path_instance(instance->klass, path_parent(path), error);
    return error.empty();
  }
  return true;
}

bool path_name_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return path_property_value(args, argc, out, error, "name");
}

bool path_suffix_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return path_property_value(args, argc, out, error, "suffix");
}

bool path_parent_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return path_property_value(args, argc, out, error, "parent");
}

bool path_stem_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return path_property_value(args, argc, out, error, "stem");
}

bool path_suffixes_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return path_property_value(args, argc, out, error, "suffixes");
}

bool path_parts_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return path_property_value(args, argc, out, error, "parts");
}

bool path_exists(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.exists() expected no arguments";
    return false;
  }
  VfsStat stat;
  std::string stat_error;
  const bool ok = runtime.vfs().stat(to_path_text(args[0]), stat, stat_error);
  value_set_bool(out, ok && stat.kind != VfsNodeKind::Missing);
  return true;
}

bool path_is_file(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.is_file() expected no arguments";
    return false;
  }
  VfsStat stat;
  std::string stat_error;
  const bool ok = runtime.vfs().stat(to_path_text(args[0]), stat, stat_error);
  value_set_bool(out, ok && stat.kind == VfsNodeKind::File);
  return true;
}

bool path_is_dir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.is_dir() expected no arguments";
    return false;
  }
  VfsStat stat;
  std::string stat_error;
  const bool ok = runtime.vfs().stat(to_path_text(args[0]), stat, stat_error);
  value_set_bool(out, ok && stat.kind == VfsNodeKind::Directory);
  return true;
}

bool path_absolute(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.absolute() expected no arguments";
    return false;
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "Path.absolute() self is invalid";
    return false;
  }
  ResolvedPath resolved;
  if (!runtime.vfs().resolve(to_path_text(args[0]), resolved, error)) {
    return false;
  }
  out = make_path_instance(instance->klass, resolved.path, error);
  return error.empty();
}

bool path_resolve(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "Path.resolve() expected optional strict";
    return false;
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "Path.resolve() self is invalid";
    return false;
  }
  ResolvedPath resolved;
  if (!runtime.vfs().resolve(to_path_text(args[0]), resolved, error)) {
    return false;
  }
  if (argc == 2 && value_truthy(args[1])) {
    VfsStat stat;
    if (!runtime.vfs().stat(resolved.path, stat, error)) {
      return false;
    }
    if (stat.kind == VfsNodeKind::Missing) {
      error = "No such file or directory: " + to_path_text(args[0]);
      return false;
    }
  }
  out = make_path_instance(instance->klass, resolved.path, error);
  return error.empty();
}

bool path_read_text(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.read_text() expected no arguments";
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!runtime.vfs().read_file(to_path_text(args[0]), bytes, error)) {
    return false;
  }
  out = Value::string(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  return true;
}

bool path_read_bytes(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.read_bytes() expected no arguments";
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!runtime.vfs().read_file(to_path_text(args[0]), bytes, error)) {
    return false;
  }
  out = Value::bytes(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  return true;
}

bool path_write_text(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Path.write_text() expected text";
    return false;
  }
  std::string text = to_path_text(args[1]);
  if (!runtime.vfs().write_file(to_path_text(args[0]), reinterpret_cast<const uint8_t*>(text.data()), text.size(), error)) {
    return false;
  }
  out = Value::int64(static_cast<int64_t>(text.size()));
  return true;
}

bool path_write_bytes(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Path.write_bytes() expected bytes";
    return false;
  }
  std::string data;
  if (auto* bytes = value_as_bytes(args[1])) {
    const auto view = bytes_object_view(*bytes);
    data.assign(view.data(), view.size());
  } else if (auto* bytearray = value_as_bytearray(args[1])) {
    data = bytearray->value;
  } else {
    error = "Path.write_bytes() argument must be bytes-like";
    return false;
  }
  if (!runtime.vfs().write_file(to_path_text(args[0]), reinterpret_cast<const uint8_t*>(data.data()), data.size(), error)) {
    return false;
  }
  out = Value::int64(static_cast<int64_t>(data.size()));
  return true;
}

bool path_mkdir_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (argc < 1 || argc > 4) {
    error = "Path.mkdir() expected optional mode, parents, exist_ok";
    return false;
  }
  bool parents = argc >= 3 && value_truthy(args[2]);
  bool exist_ok = argc >= 4 && value_truthy(args[3]);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "Path.mkdir() got invalid keyword argument";
      return false;
    }
    const std::string name(kwargs[i].name);
    if (name == "parents") {
      parents = value_truthy(*kwargs[i].value);
    } else if (name == "exist_ok") {
      exist_ok = value_truthy(*kwargs[i].value);
    } else if (name != "mode") {
      error = "Path.mkdir() got unexpected keyword argument '" + name + "'";
      return false;
    }
  }
  if (parents) {
    if (!runtime.vfs().make_dirs(to_path_text(args[0]), exist_ok, error)) {
      return false;
    }
  } else {
    const std::string parent = path_parent(to_path_text(args[0]));
    if (parent != "." && parent != "/" && parent != to_path_text(args[0])) {
      VfsStat parent_stat;
      if (!runtime.vfs().stat(parent, parent_stat, error)) {
        return false;
      }
      if (parent_stat.kind != VfsNodeKind::Directory) {
        error = "parent directory does not exist: " + parent;
        return false;
      }
    }
    if (!runtime.vfs().make_dirs(to_path_text(args[0]), exist_ok, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool path_mkdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return path_mkdir_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool path_mkdir_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return path_mkdir_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

bool path_unlink_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (argc < 1 || argc > 2) {
    error = "Path.unlink() expected optional missing_ok";
    return false;
  }
  bool missing_ok = argc == 2 && value_truthy(args[1]);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "Path.unlink() got invalid keyword argument";
      return false;
    }
    const std::string name(kwargs[i].name);
    if (name == "missing_ok") {
      missing_ok = value_truthy(*kwargs[i].value);
    } else {
      error = "Path.unlink() got unexpected keyword argument '" + name + "'";
      return false;
    }
  }
  VfsStat stat;
  if (!runtime.vfs().stat(to_path_text(args[0]), stat, error)) {
    return false;
  }
  if (stat.kind == VfsNodeKind::Missing) {
    if (missing_ok) {
      value_set_none(out);
      return true;
    }
    error = "No such file or directory: " + to_path_text(args[0]);
    return false;
  }
  if (!runtime.vfs().remove(to_path_text(args[0]), error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool path_unlink(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return path_unlink_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool path_unlink_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return path_unlink_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

bool path_iterdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.iterdir() expected no arguments";
    return false;
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "Path.iterdir() self is invalid";
    return false;
  }
  std::vector<std::string> names;
  if (!runtime.vfs().list_dir(to_path_text(args[0]), names, error)) {
    return false;
  }
  std::sort(names.begin(), names.end());
  std::vector<Value> values;
  values.reserve(names.size());
  for (const auto& name : names) {
    values.push_back(make_path_instance(instance->klass, join_paths(to_path_text(args[0]), name), error));
    if (!error.empty()) {
      return false;
    }
  }
  out = Value::list(std::move(values));
  return true;
}

bool path_glob(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Path.glob() expected pattern";
    return false;
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "Path.glob() self is invalid";
    return false;
  }
  const std::vector<std::string> segments = split_path_parts(to_path_text(args[1]));
  std::vector<Value> matches;
  if (!path_collect_glob(runtime, instance->klass, to_path_text(args[0]), segments, 0, matches, error)) {
    return false;
  }
  out = Value::list(std::move(matches));
  return true;
}

bool path_rglob(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Path.rglob() expected pattern";
    return false;
  }
  Value recursive_pattern = Value::string(join_paths("**", to_path_text(args[1])));
  Value call_args[2] = {args[0], recursive_pattern};
  return path_glob(runtime, call_args, 2, out, error, nullptr);
}

bool path_match(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Path.match() expected pattern";
    return false;
  }
  const std::string path = normalize_slashes(to_path_text(args[0]));
  const std::string pattern = normalize_slashes(to_path_text(args[1]));
  const bool anchored = pattern.find('/') != std::string::npos;
  value_set_bool(out, path_match_pattern(anchored ? path : path_name(path), pattern));
  return true;
}

bool path_with_name(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Path.with_name() expected name";
    return false;
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "Path.with_name() self is invalid";
    return false;
  }
  const std::string path = join_paths(path_parent(to_path_text(args[0])), to_path_text(args[1]));
  out = make_path_instance(instance->klass, path, error);
  return error.empty();
}

bool path_with_suffix(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Path.with_suffix() expected suffix";
    return false;
  }
  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    error = "Path.with_suffix() self is invalid";
    return false;
  }
  const std::string suffix = to_path_text(args[1]);
  std::string parent = path_parent(to_path_text(args[0]));
  std::string name = path_stem(to_path_text(args[0])) + suffix;
  out = make_path_instance(instance->klass, join_paths(parent, name), error);
  return error.empty();
}

bool path_is_absolute(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.is_absolute() expected no arguments";
    return false;
  }
  const std::string path = to_path_text(args[0]);
  value_set_bool(out, path_is_absolute_text(path));
  return true;
}

Value make_path_class(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("pathlib.Path.__init__", path_init)});
  attrs.push_back({"__str__", runtime.make_native_function("pathlib.Path.__str__", path_string)});
  attrs.push_back({"__repr__", runtime.make_native_function("pathlib.Path.__repr__", path_repr)});
  attrs.push_back({"__fspath__", runtime.make_native_function("pathlib.Path.__fspath__", path_fspath)});
  attrs.push_back({"as_posix", runtime.make_native_function("pathlib.Path.as_posix", path_as_posix)});
  attrs.push_back({"joinpath", runtime.make_native_function("pathlib.Path.joinpath", path_joinpath)});
  attrs.push_back({"__truediv__", runtime.make_native_function("pathlib.Path.__truediv__", path_truediv)});
  attrs.push_back({"with_name", runtime.make_native_function("pathlib.Path.with_name", path_with_name)});
  attrs.push_back({"with_suffix", runtime.make_native_function("pathlib.Path.with_suffix", path_with_suffix)});
  attrs.push_back({"name", Value::property(
                               runtime.make_native_function("pathlib.Path.name", path_name_getter),
                               Value::none(),
                               Value::none(),
                               Value::none())});
  attrs.push_back({"suffix", Value::property(
                                 runtime.make_native_function("pathlib.Path.suffix", path_suffix_getter),
                                 Value::none(),
                                 Value::none(),
                                 Value::none())});
  attrs.push_back({"parent", Value::property(
                                 runtime.make_native_function("pathlib.Path.parent", path_parent_getter),
                                 Value::none(),
                                 Value::none(),
                                 Value::none())});
  attrs.push_back({"stem", Value::property(
                               runtime.make_native_function("pathlib.Path.stem", path_stem_getter),
                               Value::none(),
                               Value::none(),
                               Value::none())});
  attrs.push_back({"suffixes", Value::property(
                                   runtime.make_native_function("pathlib.Path.suffixes", path_suffixes_getter),
                                   Value::none(),
                                   Value::none(),
                                   Value::none())});
  attrs.push_back({"parts", Value::property(
                                runtime.make_native_function("pathlib.Path.parts", path_parts_getter),
                                Value::none(),
                                Value::none(),
                                Value::none())});
  attrs.push_back({"exists", runtime.make_native_function("pathlib.Path.exists", path_exists)});
  attrs.push_back({"is_file", runtime.make_native_function("pathlib.Path.is_file", path_is_file)});
  attrs.push_back({"is_dir", runtime.make_native_function("pathlib.Path.is_dir", path_is_dir)});
  attrs.push_back({"is_absolute", runtime.make_native_function("pathlib.Path.is_absolute", path_is_absolute)});
  attrs.push_back({"absolute", runtime.make_native_function("pathlib.Path.absolute", path_absolute)});
  attrs.push_back({"resolve", runtime.make_native_function("pathlib.Path.resolve", path_resolve)});
  attrs.push_back({"read_text", runtime.make_native_function("pathlib.Path.read_text", path_read_text)});
  attrs.push_back({"write_text", runtime.make_native_function("pathlib.Path.write_text", path_write_text)});
  attrs.push_back({"read_bytes", runtime.make_native_function("pathlib.Path.read_bytes", path_read_bytes)});
  attrs.push_back({"write_bytes", runtime.make_native_function("pathlib.Path.write_bytes", path_write_bytes)});
  attrs.push_back({"mkdir", runtime.make_native_function("pathlib.Path.mkdir", path_mkdir, nullptr, nullptr, nullptr, false, path_mkdir_kw)});
  attrs.push_back({"unlink", runtime.make_native_function("pathlib.Path.unlink", path_unlink, nullptr, nullptr, nullptr, false, path_unlink_kw)});
  attrs.push_back({"iterdir", runtime.make_native_function("pathlib.Path.iterdir", path_iterdir)});
  attrs.push_back({"glob", runtime.make_native_function("pathlib.Path.glob", path_glob)});
  attrs.push_back({"rglob", runtime.make_native_function("pathlib.Path.rglob", path_rglob)});
  attrs.push_back({"match", runtime.make_native_function("pathlib.Path.match", path_match)});
  return Value::class_object(name, std::move(attrs));
}

} // namespace

void register_pathlib_module(Runtime& runtime) {
  Value pure_path = make_path_class(runtime, "PurePath");
  Value path = make_path_class(runtime, "Path");
  NativeModuleBuilder builder(runtime, "pathlib");
  builder.value("PurePath", pure_path)
      .value("Path", path)
      .value("PosixPath", path)
      .value("WindowsPath", path)
      .value("PurePosixPath", pure_path)
      .value("PureWindowsPath", pure_path);
  runtime.register_module("pathlib", builder.finish());
}

} // namespace xlang3
