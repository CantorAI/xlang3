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
#include "xlang3/builtin_methods.h"

#include <cctype>
#include <vector>

namespace xlang3 {

namespace {

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = std::string(name) + " must be a string";
    return false;
  }
  out = reinterpret_cast<StringObject*>(value.as.obj)->value;
  return true;
}

bool string_upper_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!method_check_argc(argc, 1, "str.upper", error)) {
    return false;
  }
  if (args[0].tag != ValueTag::Object || args[0].as.obj == nullptr || args[0].as.obj->kind != ObjectKind::String) {
    error = "str.upper target is not a string";
    return false;
  }
  auto* string = reinterpret_cast<StringObject*>(args[0].as.obj);
  std::string text = string->value;
  for (auto& ch : text) {
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  }
  out = Value::string(std::move(text));
  return true;
}

bool string_lower_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!method_check_argc(argc, 1, "str.lower", error)) {
    return false;
  }
  if (args[0].tag != ValueTag::Object || args[0].as.obj == nullptr || args[0].as.obj->kind != ObjectKind::String) {
    error = "str.lower target is not a string";
    return false;
  }
  auto* string = reinterpret_cast<StringObject*>(args[0].as.obj);
  std::string text = string->value;
  for (auto& ch : text) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  out = Value::string(std::move(text));
  return true;
}

bool string_strip_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!method_check_argc(argc, 1, "str.strip", error)) {
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "str.strip target", text, error)) {
    return false;
  }
  size_t first = 0;
  while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
    ++first;
  }
  size_t last = text.size();
  while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
    --last;
  }
  out = Value::string(text.substr(first, last - first));
  return true;
}

bool string_startswith_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (!method_check_argc(argc, 2, "str.startswith", error)) {
    return false;
  }
  std::string text;
  std::string prefix;
  if (!get_string_arg(args[0], "str.startswith target", text, error) ||
      !get_string_arg(args[1], "str.startswith prefix", prefix, error)) {
    return false;
  }
  out = Value::boolean(text.rfind(prefix, 0) == 0);
  return true;
}

void split_whitespace(const std::string& text, std::vector<Value>& out) {
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
    const size_t start = i;
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
      ++i;
    }
    if (i > start) {
      out.push_back(Value::string(text.substr(start, i - start)));
    }
  }
}

bool split_separator(const std::string& text, const std::string& sep, std::vector<Value>& out, std::string& error) {
  if (sep.empty()) {
    error = "empty separator";
    return false;
  }
  size_t start = 0;
  while (true) {
    const size_t pos = text.find(sep, start);
    if (pos == std::string::npos) {
      out.push_back(Value::string(text.substr(start)));
      return true;
    }
    out.push_back(Value::string(text.substr(start, pos - start)));
    start = pos + sep.size();
  }
}

bool string_split_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error) {
  if (argc != 1 && argc != 2) {
    error = "str.split expected 1 or 2 arguments, got " + std::to_string(argc);
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "str.split target", text, error)) {
    return false;
  }
  std::vector<Value> parts;
  if (argc == 1) {
    split_whitespace(text, parts);
  } else {
    std::string sep;
    if (!get_string_arg(args[1], "str.split separator", sep, error)) {
      return false;
    }
    if (!split_separator(text, sep, parts, error)) {
      return false;
    }
  }
  out = Value::list(std::move(parts));
  return true;
}

} // namespace

bool string_get_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag != ValueTag::Object || object.as.obj == nullptr || object.as.obj->kind != ObjectKind::String) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"lower", "str.lower", string_lower_method},
      {"split", "str.split", string_split_method},
      {"startswith", "str.startswith", string_startswith_method},
      {"strip", "str.strip", string_strip_method},
      {"upper", "str.upper", string_upper_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
