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
#include "xlang3/sequence.h"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace xlang3 {

namespace {

const std::string* get_string_ref(const Value& value, const char* name, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = std::string(name) + " must be a string";
    return nullptr;
  }
  return &reinterpret_cast<StringObject*>(value.as.obj)->value;
}

bool string_upper_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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

bool string_lower_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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

bool string_strip_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.strip", error)) {
    return false;
  }
  const auto* text = get_string_ref(args[0], "str.strip target", error);
  if (text == nullptr) {
    return false;
  }
  size_t first = 0;
  while (first < text->size() && std::isspace(static_cast<unsigned char>((*text)[first]))) {
    ++first;
  }
  size_t last = text->size();
  while (last > first && std::isspace(static_cast<unsigned char>((*text)[last - 1]))) {
    --last;
  }
  out = Value::string(text->substr(first, last - first));
  return true;
}

bool string_startswith_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.startswith", error)) {
    return false;
  }
  const auto* text = get_string_ref(args[0], "str.startswith target", error);
  if (text == nullptr) {
    return false;
  }
  const auto* prefix = get_string_ref(args[1], "str.startswith prefix", error);
  if (prefix == nullptr) {
    return false;
  }
  value_set_bool(out, text->rfind(*prefix, 0) == 0);
  return true;
}

bool string_endswith_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.endswith", error)) {
    return false;
  }
  const auto* text = get_string_ref(args[0], "str.endswith target", error);
  if (text == nullptr) {
    return false;
  }
  const auto* suffix = get_string_ref(args[1], "str.endswith suffix", error);
  if (suffix == nullptr) {
    return false;
  }
  value_set_bool(
      out,
      suffix->size() <= text->size() &&
          text->compare(text->size() - suffix->size(), suffix->size(), *suffix) == 0);
  return true;
}

bool string_find_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.find", error)) {
    return false;
  }
  const auto* text = get_string_ref(args[0], "str.find target", error);
  if (text == nullptr) {
    return false;
  }
  const auto* needle = get_string_ref(args[1], "str.find substring", error);
  if (needle == nullptr) {
    return false;
  }
  const auto pos = text->find(*needle);
  value_set_int64(out, pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
  return true;
}

bool string_count_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.count", error)) {
    return false;
  }
  const auto* text = get_string_ref(args[0], "str.count target", error);
  if (text == nullptr) {
    return false;
  }
  const auto* needle = get_string_ref(args[1], "str.count substring", error);
  if (needle == nullptr) {
    return false;
  }
  if (needle->empty()) {
    value_set_int64(out, static_cast<int64_t>(text->size() + 1));
    return true;
  }
  int64_t count = 0;
  size_t start = 0;
  while (true) {
    const auto pos = text->find(*needle, start);
    if (pos == std::string::npos) {
      break;
    }
    ++count;
    start = pos + needle->size();
  }
  value_set_int64(out, count);
  return true;
}

bool string_replace_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3 && argc != 4) {
    error = "str.replace expected 3 or 4 arguments, got " + std::to_string(argc);
    return false;
  }
  const auto* text = get_string_ref(args[0], "str.replace target", error);
  if (text == nullptr) {
    return false;
  }
  const auto* old_text = get_string_ref(args[1], "str.replace old", error);
  if (old_text == nullptr) {
    return false;
  }
  const auto* new_text = get_string_ref(args[2], "str.replace new", error);
  if (new_text == nullptr) {
    return false;
  }
  int64_t max_count = -1;
  if (argc == 4) {
    if (args[3].tag != ValueTag::Int64) {
      error = "str.replace count must be int";
      return false;
    }
    max_count = args[3].as.i64;
  }
  if (old_text->empty()) {
    out = Value::string(*text);
    return true;
  }
  if (old_text->size() == 1 && new_text->size() == 1 && max_count < 0) {
    std::string result = *text;
    const char old_ch = (*old_text)[0];
    const char new_ch = (*new_text)[0];
    for (auto& ch : result) {
      if (ch == old_ch) {
        ch = new_ch;
      }
    }
    out = Value::string(std::move(result));
    return true;
  }
  std::string result;
  result.reserve(text->size());
  size_t start = 0;
  int64_t count = 0;
  while (max_count < 0 || count < max_count) {
    const auto pos = text->find(*old_text, start);
    if (pos == std::string::npos) {
      break;
    }
    result.append(*text, start, pos - start);
    result += *new_text;
    start = pos + old_text->size();
    ++count;
  }
  result.append(*text, start, std::string::npos);
  out = Value::string(std::move(result));
  return true;
}

bool string_join_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.join", error)) {
    return false;
  }
  const auto* sep = get_string_ref(args[0], "str.join separator", error);
  if (sep == nullptr) {
    return false;
  }
  std::string result;
  if (auto* list = value_as_list(args[1])) {
    size_t total_size = 0;
    for (const auto& value : list->items) {
      const auto* item = get_string_ref(value, "str.join item", error);
      if (item == nullptr) return false;
      total_size += item->size();
    }
    if (!list->items.empty()) total_size += sep->size() * (list->items.size() - 1);
    result.reserve(total_size);
    for (size_t i = 0; i < list->items.size(); ++i) {
      const auto* item = get_string_ref(list->items[i], "str.join item", error);
      if (item == nullptr) {
        return false;
      }
      if (i != 0) {
        result += *sep;
      }
      result += *item;
    }
    out = Value::string(std::move(result));
    return true;
  }
  if (args[1].tag == ValueTag::Object && args[1].as.obj != nullptr && args[1].as.obj->kind == ObjectKind::Tuple) {
    auto* tuple = reinterpret_cast<TupleObject*>(args[1].as.obj);
    size_t total_size = 0;
    for (const auto& value : tuple->items) {
      const auto* item = get_string_ref(value, "str.join item", error);
      if (item == nullptr) return false;
      total_size += item->size();
    }
    if (!tuple->items.empty()) total_size += sep->size() * (tuple->items.size() - 1);
    result.reserve(total_size);
    for (size_t i = 0; i < tuple->items.size(); ++i) {
      const auto* item = get_string_ref(tuple->items[i], "str.join item", error);
      if (item == nullptr) {
        return false;
      }
      if (i != 0) {
        result += *sep;
      }
      result += *item;
    }
    out = Value::string(std::move(result));
    return true;
  }
  error = "str.join argument must be a list or tuple";
  return false;
}

bool string_format_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "str.format expected at least 1 argument";
    return false;
  }
  const auto* format = get_string_ref(args[0], "str.format target", error);
  if (format == nullptr) {
    return false;
  }
  std::string result;
  uint32_t next_arg = 1;
  for (size_t i = 0; i < format->size();) {
    if ((*format)[i] == '{' && i + 1 < format->size() && (*format)[i + 1] == '{') {
      result.push_back('{');
      i += 2;
      continue;
    }
    if ((*format)[i] == '}' && i + 1 < format->size() && (*format)[i + 1] == '}') {
      result.push_back('}');
      i += 2;
      continue;
    }
    if ((*format)[i] == '{') {
      const auto close = format->find('}', i + 1);
      if (close == std::string::npos) {
        error = "str.format unmatched '{'";
        return false;
      }
      uint32_t arg_index = next_arg++;
      const auto field = format->substr(i + 1, close - i - 1);
      if (!field.empty()) {
        char* end = nullptr;
        const auto parsed = std::strtoul(field.c_str(), &end, 10);
        if (end != field.c_str()) {
          arg_index = static_cast<uint32_t>(parsed + 1);
        }
      }
      if (arg_index >= argc) {
        error = "str.format replacement index out of range";
        return false;
      }
      result += value_to_string(args[arg_index]);
      i = close + 1;
      continue;
    }
    result.push_back((*format)[i++]);
  }
  out = Value::string(std::move(result));
  return true;
}

bool string_encode_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.encode expected 1 or 2 arguments, got " + std::to_string(argc);
    return false;
  }
  const auto* text = get_string_ref(args[0], "str.encode target", error);
  if (text == nullptr) {
    return false;
  }
  if (argc == 2) {
    const auto* encoding_ref = get_string_ref(args[1], "str.encode encoding", error);
    if (encoding_ref == nullptr) {
      return false;
    }
    std::string encoding = *encoding_ref;
    for (auto& ch : encoding) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (encoding != "utf-8" && encoding != "utf8") {
      error = "only utf-8 encoding is supported";
      return false;
    }
  }
  out = Value::bytes(*text);
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
  if (sep.size() == 1) {
    const char sep_ch = sep[0];
    size_t count = 1;
    for (const auto ch : text) {
      if (ch == sep_ch) {
        ++count;
      }
    }
    out.reserve(count);
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == sep_ch) {
        out.push_back(Value::string(text.substr(start, i - start)));
        start = i + 1;
      }
    }
    out.push_back(Value::string(text.substr(start)));
    return true;
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

bool string_split_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.split expected 1 or 2 arguments, got " + std::to_string(argc);
    return false;
  }
  const auto* text = get_string_ref(args[0], "str.split target", error);
  if (text == nullptr) {
    return false;
  }
  std::vector<Value> parts;
  if (argc == 1) {
    split_whitespace(*text, parts);
  } else {
    const auto* sep = get_string_ref(args[1], "str.split separator", error);
    if (sep == nullptr) {
      return false;
    }
    if (!split_separator(*text, *sep, parts, error)) {
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
      {"count", "str.count", string_count_method},
      {"encode", "str.encode", string_encode_method},
      {"endswith", "str.endswith", string_endswith_method},
      {"find", "str.find", string_find_method},
      {"format", "str.format", string_format_method},
      {"join", "str.join", string_join_method},
      {"lower", "str.lower", string_lower_method},
      {"replace", "str.replace", string_replace_method},
      {"split", "str.split", string_split_method},
      {"startswith", "str.startswith", string_startswith_method},
      {"strip", "str.strip", string_strip_method},
      {"upper", "str.upper", string_upper_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

bool string_get_method_callback(const Value& object, const std::string& name, NativeFunctionCallback& callback) {
  if (object.tag != ValueTag::Object || object.as.obj == nullptr || object.as.obj->kind != ObjectKind::String) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"count", "str.count", string_count_method},
      {"encode", "str.encode", string_encode_method},
      {"endswith", "str.endswith", string_endswith_method},
      {"find", "str.find", string_find_method},
      {"format", "str.format", string_format_method},
      {"join", "str.join", string_join_method},
      {"lower", "str.lower", string_lower_method},
      {"replace", "str.replace", string_replace_method},
      {"split", "str.split", string_split_method},
      {"startswith", "str.startswith", string_startswith_method},
      {"strip", "str.strip", string_strip_method},
      {"upper", "str.upper", string_upper_method},
  };
  for (const auto& method : methods) {
    if (name == method.name) {
      callback = method.callback;
      return true;
    }
  }
  return false;
}

} // namespace xlang3
