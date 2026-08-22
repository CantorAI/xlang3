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
#include "xlang3/sequence.h"

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

bool pattern_has_magic(std::string_view pattern) {
  return pattern.find_first_of("*?[") != std::string_view::npos;
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
  auto* list = value_as_list(args[0]);
  if (list == nullptr) {
    error = "filter() names must be list";
    return false;
  }
  std::string pattern;
  if (!get_string_arg(args[1], "pattern", pattern, error)) {
    return false;
  }
  const bool invert = user_data != nullptr;
  std::vector<Value> result;
  for (const auto& item : list->items) {
    std::string name;
    if (!get_string_arg(item, "name", name, error)) {
      return false;
    }
    if (fnmatch_impl(name, pattern) != invert) {
      result.push_back(item);
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
  glob.function("has_magic", has_magic_entry);
  runtime.register_module("glob", glob.finish());
}

} // namespace xlang3
