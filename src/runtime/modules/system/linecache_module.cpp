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
#include "xlang3/vfs.h"

#include <mutex>
#include <unordered_map>

namespace xlang3 {

namespace {

struct LineCacheEntry {
  std::vector<std::string> lines;
};

std::mutex& linecache_mutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<std::string, LineCacheEntry>& linecache_entries() {
  static std::unordered_map<std::string, LineCacheEntry> entries;
  return entries;
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

std::vector<std::string> split_source_lines(const std::vector<uint8_t>& bytes) {
  std::vector<std::string> lines;
  std::string current;
  current.reserve(128);
  for (uint8_t byte : bytes) {
    current.push_back(static_cast<char>(byte));
    if (byte == '\n') {
      lines.push_back(std::move(current));
      current.clear();
    }
  }
  if (!current.empty()) {
    lines.push_back(std::move(current));
  }
  return lines;
}

bool read_lines(Runtime& runtime, const std::string& filename, std::vector<std::string>& lines) {
  {
    std::lock_guard<std::mutex> lock(linecache_mutex());
    auto it = linecache_entries().find(filename);
    if (it != linecache_entries().end()) {
      lines = it->second.lines;
      return true;
    }
  }

  std::vector<uint8_t> bytes;
  std::string ignored;
  if (!runtime.vfs().read_file(filename, bytes, ignored)) {
    return false;
  }
  lines = split_source_lines(bytes);

  {
    std::lock_guard<std::mutex> lock(linecache_mutex());
    linecache_entries()[filename] = LineCacheEntry{lines};
  }
  return true;
}

Value lines_to_list(const std::vector<std::string>& lines) {
  std::vector<Value> values;
  values.reserve(lines.size());
  for (const auto& line : lines) {
    values.push_back(Value::string(line));
  }
  return Value::list(std::move(values));
}

bool linecache_getline(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "linecache.getline() expected filename, lineno, and optional module_globals";
    return false;
  }
  std::string filename;
  if (!get_string_arg(args[0], "linecache.getline filename", filename, error)) {
    return false;
  }
  if (args[1].tag != ValueTag::Int64) {
    error = "linecache.getline lineno must be int";
    return false;
  }

  const int64_t lineno = args[1].as.i64;
  if (lineno <= 0) {
    out = Value::string("");
    return true;
  }

  std::vector<std::string> lines;
  if (!read_lines(runtime, filename, lines) || static_cast<size_t>(lineno) > lines.size()) {
    out = Value::string("");
    return true;
  }
  out = Value::string(lines[static_cast<size_t>(lineno - 1)]);
  return true;
}

bool linecache_getlines(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "linecache.getlines() expected filename and optional module_globals";
    return false;
  }
  std::string filename;
  if (!get_string_arg(args[0], "linecache.getlines filename", filename, error)) {
    return false;
  }
  std::vector<std::string> lines;
  if (!read_lines(runtime, filename, lines)) {
    out = Value::list({});
    return true;
  }
  out = lines_to_list(lines);
  return true;
}

bool linecache_updatecache(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "linecache.updatecache() expected filename and optional module_globals";
    return false;
  }
  std::string filename;
  if (!get_string_arg(args[0], "linecache.updatecache filename", filename, error)) {
    return false;
  }

  std::vector<uint8_t> bytes;
  std::string ignored;
  if (!runtime.vfs().read_file(filename, bytes, ignored)) {
    std::lock_guard<std::mutex> lock(linecache_mutex());
    linecache_entries().erase(filename);
    out = Value::list({});
    return true;
  }

  std::vector<std::string> lines = split_source_lines(bytes);
  {
    std::lock_guard<std::mutex> lock(linecache_mutex());
    linecache_entries()[filename] = LineCacheEntry{lines};
  }
  out = lines_to_list(lines);
  return true;
}

bool linecache_clearcache(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "linecache.clearcache() expected no arguments";
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(linecache_mutex());
    linecache_entries().clear();
  }
  value_set_none(out);
  return true;
}

bool linecache_checkcache(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "linecache.checkcache() expected optional filename";
    return false;
  }
  value_set_none(out);
  return true;
}

bool linecache_lazycache(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "linecache.lazycache() expected filename and module_globals";
    return false;
  }
  std::string filename;
  if (!get_string_arg(args[0], "linecache.lazycache filename", filename, error)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(linecache_mutex());
  out = Value::boolean(linecache_entries().find(filename) != linecache_entries().end());
  return true;
}

} // namespace

void register_linecache_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "linecache");
  builder.function("getline", linecache_getline)
      .function("getlines", linecache_getlines)
      .function("updatecache", linecache_updatecache)
      .function("clearcache", linecache_clearcache)
      .function("checkcache", linecache_checkcache)
      .function("lazycache", linecache_lazycache)
      .value("cache", Value::dict({}))
      .value("_cache", Value::dict({}));
  runtime.register_module("linecache", builder.finish());
}

} // namespace xlang3
