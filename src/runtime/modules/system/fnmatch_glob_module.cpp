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

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include <algorithm>
#include <regex>
#include <string>

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

struct PathArg {
  std::string text;
  bool bytes = false;
};

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
  if (attribute_get(value, "__fspath__", fspath, ignored)) {
    Value result;
    std::string call_error;
    if (!runtime_call_callable(runtime, fspath, nullptr, 0, result, call_error)) {
      error = call_error.empty() ? std::string(name) + " __fspath__ failed" : call_error;
      return false;
    }
    return get_path_arg(runtime, result, name, out, error);
  }

  error = std::string(name) + " must be str, bytes, or os.PathLike";
  return false;
}

bool pattern_has_magic(std::string_view pattern) {
  return pattern.find_first_of("*?[") != std::string_view::npos;
}

bool segment_matches_hidden(const std::string& name, const std::string& pattern, bool include_hidden) {
  return include_hidden || name.empty() || name[0] != '.' || (!pattern.empty() && pattern[0] == '.');
}

std::string wildcard_to_regex(std::string_view pattern) {
  std::string out = "^";
  out.reserve(pattern.size() * 2 + 2);
  for (size_t i = 0; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    switch (ch) {
      case '*':
        out += ".*";
        break;
      case '?':
        out += ".";
        break;
      case '[': {
        size_t close = pattern.find(']', i + 1);
        if (close == std::string_view::npos) {
          out += "\\[";
          break;
        }
        out.push_back('[');
        size_t start = i + 1;
        if (start < close && (pattern[start] == '!' || pattern[start] == '^')) {
          out.push_back('^');
          ++start;
        }
        for (size_t j = start; j < close; ++j) {
          const char item = pattern[j];
          if (item == '\\') {
            out += "\\\\";
          } else {
            out.push_back(item);
          }
        }
        out.push_back(']');
        i = close;
        break;
      }
      case '\\':
      case '.':
      case '+':
      case '(':
      case ')':
      case '{':
      case '}':
      case '|':
      case '^':
      case '$':
        out.push_back('\\');
        out.push_back(ch);
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  out.push_back('$');
  return out;
}

bool fnmatch_impl(const std::string& name, const std::string& pattern) {
  try {
    return std::regex_match(name, std::regex(wildcard_to_regex(pattern)));
  } catch (const std::regex_error&) {
    return false;
  }
}

std::string normalize_glob_path(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

std::string join_path(const std::string& base, const std::string& name) {
  if (base.empty() || base == ".") {
    return name;
  }
  if (base == "/") {
    return "/" + name;
  }
  if (!base.empty() && base.back() == '/') {
    return base + name;
  }
  return base + "/" + name;
}

std::vector<std::string> split_path_segments(const std::string& path) {
  std::vector<std::string> segments;
  std::string current;
  for (char ch : path) {
    if (ch == '/') {
      if (!current.empty()) {
        segments.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) {
    segments.push_back(current);
  }
  return segments;
}

std::string glob_basename(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool glob_collect(
    Runtime& runtime,
    const std::string& base,
    const std::vector<std::string>& segments,
    size_t index,
    bool recursive,
    bool include_hidden,
    std::vector<std::string>& out,
    std::string& error) {
  if (index >= segments.size()) {
    VfsStat stat;
    std::string stat_error;
    if (runtime.vfs().stat(base.empty() ? "." : base, stat, stat_error) && stat.kind != VfsNodeKind::Missing) {
      out.push_back(base.empty() ? "." : base);
    }
    return true;
  }

  const std::string& segment = segments[index];
  if (recursive && segment == "**") {
    if (!glob_collect(runtime, base, segments, index + 1, recursive, include_hidden, out, error)) {
      return false;
    }
    std::vector<std::string> names;
    std::string list_error;
    const std::string dir = base.empty() ? "." : base;
    if (!runtime.vfs().list_dir(dir, names, list_error)) {
      return true;
    }
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
      if (!segment_matches_hidden(name, segment, include_hidden)) {
        continue;
      }
      const std::string child = join_path(base, name);
      VfsStat stat;
      std::string stat_error;
      if (runtime.vfs().stat(child, stat, stat_error) && stat.kind == VfsNodeKind::Directory) {
        if (!glob_collect(runtime, child, segments, index, recursive, include_hidden, out, error)) {
          return false;
        }
      }
    }
    return true;
  }

  if (!pattern_has_magic(segment)) {
    const std::string child = join_path(base, segment);
    VfsStat stat;
    std::string stat_error;
    if (!runtime.vfs().stat(child, stat, stat_error) || stat.kind == VfsNodeKind::Missing) {
      return true;
    }
    if (index + 1 == segments.size()) {
      out.push_back(child);
    } else if (stat.kind == VfsNodeKind::Directory) {
      return glob_collect(runtime, child, segments, index + 1, recursive, include_hidden, out, error);
    }
    return true;
  }

  std::vector<std::string> names;
  std::string list_error;
  const std::string dir = base.empty() ? "." : base;
  if (!runtime.vfs().list_dir(dir, names, list_error)) {
    return true;
  }
  std::sort(names.begin(), names.end());
  for (const auto& name : names) {
    if (!segment_matches_hidden(name, segment, include_hidden) || !fnmatch_impl(name, segment)) {
      continue;
    }
    const std::string child = join_path(base, name);
    VfsStat stat;
    std::string stat_error;
    if (!runtime.vfs().stat(child, stat, stat_error) || stat.kind == VfsNodeKind::Missing) {
      continue;
    }
    if (index + 1 == segments.size()) {
      out.push_back(child);
    } else if (stat.kind == VfsNodeKind::Directory) {
      if (!glob_collect(runtime, child, segments, index + 1, recursive, include_hidden, out, error)) {
        return false;
      }
    }
  }
  return true;
}

bool glob_text(
    Runtime& runtime,
    const std::string& pattern,
    bool recursive,
    bool include_hidden,
    std::vector<std::string>& matches,
    std::string& error) {
  const std::string normalized = normalize_glob_path(pattern);
  if (normalized.empty()) {
    return true;
  }
  const bool absolute = !normalized.empty() && normalized[0] == '/';
  const std::string base = absolute ? "/" : "";
  auto segments = split_path_segments(absolute ? normalized.substr(1) : normalized);
  if (segments.empty()) {
    VfsStat stat;
    std::string stat_error;
    if (runtime.vfs().stat(normalized, stat, stat_error) && stat.kind != VfsNodeKind::Missing) {
      matches.push_back(normalized);
    }
    return true;
  }
  return glob_collect(runtime, base, segments, 0, recursive, include_hidden, matches, error);
}

Value strings_to_list(std::vector<std::string> values, bool bytes) {
  std::vector<Value> items;
  items.reserve(values.size());
  for (auto& value : values) {
    items.push_back(bytes ? Value::bytes(std::move(value)) : Value::string(std::move(value)));
  }
  return Value::list(std::move(items));
}

std::string root_relative_match(const std::string& root_dir, const std::string& match) {
  if (root_dir.empty()) {
    return match;
  }
  std::string root = normalize_glob_path(root_dir);
  std::string normalized = normalize_glob_path(match);
  while (!root.empty() && root.back() == '/') {
    root.pop_back();
  }
  if (normalized == root) {
    return ".";
  }
  if (!root.empty() && normalized.rfind(root + "/", 0) == 0) {
    return normalized.substr(root.size() + 1);
  }
  return normalized;
}

bool value_to_string_vector(const Value& value, std::vector<std::string>& out, std::string& error) {
  Value iterator;
  if (!sequence_get_iter(value, iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    std::string text;
    if (!get_string_arg(item, "name", text, error)) {
      return false;
    }
    out.push_back(std::move(text));
  }
}

bool fnmatch_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "fnmatch() expected name and pattern";
    return false;
  }
  std::string name;
  std::string pattern;
  if (!get_string_arg(args[0], "name", name, error) || !get_string_arg(args[1], "pattern", pattern, error)) {
    return false;
  }
  out = Value::boolean(fnmatch_impl(name, pattern));
  return true;
}

bool filter_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "filter() expected names and pattern";
    return false;
  }
  std::vector<std::string> names;
  if (!value_to_string_vector(args[0], names, error)) {
    error = "filter() names must be iterable";
    return false;
  }
  std::string pattern;
  if (!get_string_arg(args[1], "pattern", pattern, error)) {
    return false;
  }
  const bool invert = user_data != nullptr;
  std::vector<Value> result;
  for (const auto& name : names) {
    if (fnmatch_impl(name, pattern) != invert) {
      result.push_back(Value::string(name));
    }
  }
  out = Value::list(std::move(result));
  return true;
}

bool translate_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "translate() expected pattern";
    return false;
  }
  std::string pattern;
  if (!get_string_arg(args[0], "pattern", pattern, error)) {
    return false;
  }
  out = Value::string(wildcard_to_regex(pattern));
  return true;
}

bool has_magic_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "has_magic() expected pattern";
    return false;
  }
  std::string pattern;
  if (!get_string_arg(args[0], "pattern", pattern, error)) {
    return false;
  }
  out = Value::boolean(pattern_has_magic(pattern));
  return true;
}

bool escape_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "glob.escape() expected pathname";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "pathname", path, error)) {
    return false;
  }
  std::string escaped;
  for (char ch : path) {
    if (ch == '*' || ch == '?' || ch == '[') {
      escaped.push_back('[');
      escaped.push_back(ch);
      escaped.push_back(']');
    } else {
      escaped.push_back(ch);
    }
  }
  out = Value::string(std::move(escaped));
  return true;
}

struct GlobOptions {
  PathArg pathname;
  bool recursive = false;
  bool include_hidden = false;
  std::string root_dir;
  bool has_root_dir = false;
};

bool parse_glob_options(
    Runtime& runtime,
    const char* function_name,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    GlobOptions& options,
    std::string& error) {
  if (argc < 1 || argc > 2) {
    error = std::string(function_name) + "() expected pathname and optional recursive";
    return false;
  }
  if (!get_path_arg(runtime, args[0], "pathname", options.pathname, error)) {
    return false;
  }
  options.recursive = argc >= 2 && value_truthy(args[1]);

  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = std::string(function_name) + "() got invalid keyword argument";
      return false;
    }
    const std::string name(kwargs[i].name);
    if (name == "recursive") {
      options.recursive = value_truthy(*kwargs[i].value);
    } else if (name == "include_hidden") {
      options.include_hidden = value_truthy(*kwargs[i].value);
    } else if (name == "root_dir") {
      if (kwargs[i].value->tag != ValueTag::None) {
        PathArg root;
        if (!get_path_arg(runtime, *kwargs[i].value, "root_dir", root, error)) {
          return false;
        }
        options.root_dir = root.text;
        options.has_root_dir = true;
        options.pathname.bytes = options.pathname.bytes || root.bytes;
      }
    } else if (name == "dir_fd") {
      if (kwargs[i].value->tag != ValueTag::None) {
        error = std::string(function_name) + "() dir_fd is not supported by the XLang3 VFS";
        return false;
      }
    } else {
      error = std::string(function_name) + "() got unexpected keyword argument '" + name + "'";
      return false;
    }
  }
  return true;
}

bool glob_impl(Runtime& runtime, const GlobOptions& options, Value& out, std::string& error) {
  std::string pattern = options.pathname.text;
  if (options.has_root_dir && !pattern.empty() && pattern[0] != '/' && pattern.find(':') == std::string::npos) {
    pattern = join_path(options.root_dir, pattern);
  }
  std::vector<std::string> matches;
  if (!glob_text(runtime, pattern, options.recursive, options.include_hidden, matches, error)) {
    return false;
  }
  if (options.has_root_dir) {
    for (auto& match : matches) {
      match = root_relative_match(options.root_dir, match);
    }
  }
  out = strings_to_list(std::move(matches), options.pathname.bytes);
  return true;
}

bool iglob_impl(Runtime& runtime, const GlobOptions& options, Value& out, std::string& error) {
  Value items;
  if (!glob_impl(runtime, options, items, error)) {
    return false;
  }
  return runtime_get_iter(runtime, items, out, error);
}

bool glob_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  GlobOptions options;
  if (!parse_glob_options(runtime, "glob.glob", args, argc, nullptr, 0, options, error)) {
    return false;
  }
  return glob_impl(runtime, options, out, error);
}

bool glob_entry_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  GlobOptions options;
  if (!parse_glob_options(runtime, "glob.glob", args, argc, kwargs, kwargc, options, error)) {
    return false;
  }
  return glob_impl(runtime, options, out, error);
}

bool iglob_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  GlobOptions options;
  if (!parse_glob_options(runtime, "glob.iglob", args, argc, nullptr, 0, options, error)) {
    return false;
  }
  return iglob_impl(runtime, options, out, error);
}

bool iglob_entry_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  GlobOptions options;
  if (!parse_glob_options(runtime, "glob.iglob", args, argc, kwargs, kwargc, options, error)) {
    return false;
  }
  return iglob_impl(runtime, options, out, error);
}

} // namespace

void register_fnmatch_glob_modules(Runtime& runtime) {
  NativeModuleBuilder fnmatch(runtime, "fnmatch");
  fnmatch.function("fnmatch", fnmatch_entry)
      .function("fnmatchcase", fnmatch_entry)
      .function("filter", filter_entry)
      .value("filterfalse", runtime.make_native_function("fnmatch.filterfalse", filter_entry, reinterpret_cast<void*>(1)))
      .function("translate", translate_entry);
  runtime.register_module("fnmatch", fnmatch.finish());

  NativeModuleBuilder glob(runtime, "glob");
  glob.function("glob", glob_entry, nullptr, false, glob_entry_kw)
      .function("iglob", iglob_entry, nullptr, false, iglob_entry_kw)
      .function("escape", escape_entry)
      .function("has_magic", has_magic_entry);
  runtime.register_module("glob", glob.finish());
}

} // namespace xlang3
