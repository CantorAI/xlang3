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
#include "xlang3/functional_iterators.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <vector>

namespace xlang3 {

namespace {

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = std::string(name) + " must be a string";
    return false;
  }
  out = string_object_to_string(*reinterpret_cast<StringObject*>(value.as.obj));
  return true;
}

bool get_bytes_like_view(const Value& value, const char* name, std::string_view& out, std::string& error);

std::string canonical_encoding(std::string name) {
  for (char& ch : name) {
    if (ch == '-' || ch == ' ' || ch == '.') {
      ch = '_';
    } else {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
  }
  if (name == "utf8" || name == "u8" || name == "cp65001") {
    return "utf_8";
  }
  if (name == "latin1" || name == "latin_1" || name == "iso8859_1" || name == "iso_8859_1" || name == "8859") {
    return "latin_1";
  }
  if (name == "us_ascii" || name == "646") {
    return "ascii";
  }
  return name;
}

bool append_utf8(uint32_t codepoint, std::string& out) {
  if (codepoint <= 0x7f) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
  return true;
}

std::string latin1_decode_text(std::string_view text) {
  std::string decoded;
  decoded.reserve(text.size() * 2);
  for (unsigned char ch : text) {
    append_utf8(ch, decoded);
  }
  return decoded;
}

bool bytes_decode_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "bytes.decode expected 0 to 2 arguments, got " + std::to_string(argc - 1);
    return false;
  }
  std::string_view text;
  if (!get_bytes_like_view(args[0], "bytes.decode target", text, error)) {
    return false;
  }
  std::string encoding = "utf-8";
  if (argc >= 2) {
    if (!get_string_arg(args[1], "bytes.decode encoding", encoding, error)) {
      return false;
    }
    for (auto& ch : encoding) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    encoding = canonical_encoding(std::move(encoding));
    if (encoding != "utf_8" && encoding != "utf_8_sig" && encoding != "ascii" && encoding != "latin_1") {
      error = "only utf-8/ascii/latin-1 encoding is supported";
      return false;
    }
  }
  std::string errors = "strict";
  if (argc == 3) {
    if (!get_string_arg(args[2], "bytes.decode errors", errors, error)) {
      return false;
    }
    for (auto& ch : errors) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
  }
  if (encoding == "ascii") {
    std::string decoded;
    decoded.reserve(text.size());
    for (unsigned char ch : text) {
      if (ch < 128) {
        decoded.push_back(static_cast<char>(ch));
      } else if (errors == "ignore") {
        continue;
      } else if (errors == "replace") {
        decoded += "\xef\xbf\xbd";
      } else {
        error = "ascii codec can't decode byte";
        runtime.raise_class_error("UnicodeDecodeError", error);
        return false;
      }
    }
    out = Value::string(std::move(decoded));
    return true;
  }
  if (encoding == "latin_1") {
    out = Value::string(latin1_decode_text(text));
    return true;
  }
  if (encoding == "utf_8_sig") {
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb &&
        static_cast<unsigned char>(text[2]) == 0xbf) {
      text.remove_prefix(3);
    }
  }
  out = Value::string(std::string(text));
  return true;
}

bool get_bytes_like_view(const Value& value, const char* name, std::string_view& out, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_view(*bytes);
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = std::string_view(bytearray->value.data(), bytearray->value.size());
    return true;
  }
  error = std::string(name) + " must be bytes-like";
  return false;
}

Value make_binary_like_result(const Value& receiver, std::string text) {
  return value_as_bytearray(receiver) != nullptr ? Value::bytearray(std::move(text)) : Value::bytes(std::move(text));
}

bool normalize_bytes_bounds(size_t size, const Value* args, uint32_t argc, size_t& start, size_t& end, std::string& error) {
  start = 0;
  end = size;
  if (argc >= 3 && args[2].tag != ValueTag::None) {
    if (args[2].tag != ValueTag::Int64) {
      error = "slice index must be int";
      return false;
    }
    int64_t value = args[2].as.i64;
    if (value < 0) value += static_cast<int64_t>(size);
    if (value < 0) value = 0;
    if (value > static_cast<int64_t>(size)) value = static_cast<int64_t>(size);
    start = static_cast<size_t>(value);
  }
  if (argc >= 4 && args[3].tag != ValueTag::None) {
    if (args[3].tag != ValueTag::Int64) {
      error = "slice index must be int";
      return false;
    }
    int64_t value = args[3].as.i64;
    if (value < 0) value += static_cast<int64_t>(size);
    if (value < 0) value = 0;
    if (value > static_cast<int64_t>(size)) value = static_cast<int64_t>(size);
    end = static_cast<size_t>(value);
  }
  if (end < start) {
    end = start;
  }
  return true;
}

bool bytes_count_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "bytes.count expected sub and optional start/end";
    return false;
  }
  std::string_view text;
  std::string_view needle;
  if (!get_bytes_like_view(args[0], "bytes.count target", text, error) ||
      !get_bytes_like_view(args[1], "bytes.count sub", needle, error)) {
    return false;
  }
  size_t start = 0;
  size_t end = text.size();
  if (!normalize_bytes_bounds(text.size(), args, argc, start, end, error)) {
    return false;
  }
  if (needle.empty()) {
    value_set_int64(out, static_cast<int64_t>(end - start + 1));
    return true;
  }
  int64_t count = 0;
  size_t pos = start;
  while (pos <= end) {
    const size_t found = text.find(needle, pos);
    if (found == std::string_view::npos || found + needle.size() > end) {
      break;
    }
    ++count;
    pos = found + needle.size();
  }
  value_set_int64(out, count);
  return true;
}

bool bytes_find_common(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    bool reverse,
    bool raise_on_miss) {
  if (argc < 2 || argc > 4) {
    error = "bytes.find expected sub and optional start/end";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string_view text;
  std::string_view needle;
  if (!get_bytes_like_view(args[0], "bytes.find target", text, error) ||
      !get_bytes_like_view(args[1], "bytes.find sub", needle, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  size_t start = 0;
  size_t end = text.size();
  if (!normalize_bytes_bounds(text.size(), args, argc, start, end, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto haystack = text.substr(start, end - start);
  const size_t found = reverse ? haystack.rfind(needle) : haystack.find(needle);
  if (found == std::string_view::npos) {
    if (raise_on_miss) {
      error = "subsection not found";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    value_set_int64(out, -1);
    return true;
  }
  value_set_int64(out, static_cast<int64_t>(start + found));
  return true;
}

bool bytes_find_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return bytes_find_common(runtime, args, argc, out, error, false, false);
}

bool bytes_rfind_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return bytes_find_common(runtime, args, argc, out, error, true, false);
}

bool bytes_index_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return bytes_find_common(runtime, args, argc, out, error, false, true);
}

bool bytes_rindex_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return bytes_find_common(runtime, args, argc, out, error, true, true);
}

bool bytes_replace_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 4) {
    error = "bytes.replace expected old, new, and optional count";
    return false;
  }
  std::string_view text;
  std::string_view old_text;
  std::string_view new_text;
  if (!get_bytes_like_view(args[0], "bytes.replace target", text, error) ||
      !get_bytes_like_view(args[1], "bytes.replace old", old_text, error) ||
      !get_bytes_like_view(args[2], "bytes.replace new", new_text, error)) {
    return false;
  }
  int64_t max_count = -1;
  if (argc == 4) {
    if (args[3].tag != ValueTag::Int64) {
      error = "bytes.replace count must be int";
      return false;
    }
    max_count = args[3].as.i64;
  }
  if (old_text.empty() || max_count == 0) {
    out = make_binary_like_result(args[0], std::string(text));
    return true;
  }
  std::string result;
  size_t pos = 0;
  int64_t count = 0;
  while (pos <= text.size()) {
    const size_t found = text.find(old_text, pos);
    if (found == std::string_view::npos || (max_count >= 0 && count >= max_count)) {
      result.append(text.substr(pos));
      break;
    }
    result.append(text.substr(pos, found - pos));
    result.append(new_text);
    pos = found + old_text.size();
    ++count;
  }
  out = make_binary_like_result(args[0], std::move(result));
  return true;
}

bool bytes_hex_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "bytes.hex expected no arguments";
    return false;
  }
  std::string_view text;
  if (!get_bytes_like_view(args[0], "bytes.hex target", text, error)) {
    return false;
  }
  static constexpr char digits[] = "0123456789abcdef";
  std::string result;
  result.resize(text.size() * 2);
  for (size_t i = 0; i < text.size(); ++i) {
    const auto byte = static_cast<unsigned char>(text[i]);
    result[i * 2] = digits[byte >> 4u];
    result[i * 2 + 1] = digits[byte & 0x0fu];
  }
  out = Value::string(std::move(result));
  return true;
}

bool bytes_startswith_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "bytes.startswith", error)) {
    return false;
  }
  std::string_view text;
  if (!get_bytes_like_view(args[0], "bytes.startswith target", text, error)) {
    return false;
  }
  if (auto* tuple = value_as_tuple(args[1])) {
    for (const auto& item : tuple->items) {
      std::string_view prefix;
      if (!get_bytes_like_view(item, "bytes.startswith prefix", prefix, error)) {
        return false;
      }
      if (prefix.size() <= text.size() &&
          (prefix.empty() || std::memcmp(text.data(), prefix.data(), prefix.size()) == 0)) {
        value_set_bool(out, true);
        return true;
      }
    }
    value_set_bool(out, false);
    return true;
  }
  std::string_view prefix;
  if (!get_bytes_like_view(args[1], "bytes.startswith prefix", prefix, error)) {
    return false;
  }
  value_set_bool(out, prefix.size() <= text.size() &&
                          (prefix.empty() || std::memcmp(text.data(), prefix.data(), prefix.size()) == 0));
  return true;
}

bool bytes_endswith_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "bytes.endswith", error)) {
    return false;
  }
  std::string_view text;
  if (!get_bytes_like_view(args[0], "bytes.endswith target", text, error)) {
    return false;
  }
  if (auto* tuple = value_as_tuple(args[1])) {
    for (const auto& item : tuple->items) {
      std::string_view suffix;
      if (!get_bytes_like_view(item, "bytes.endswith suffix", suffix, error)) {
        return false;
      }
      if (suffix.size() <= text.size() &&
          (suffix.empty() ||
           std::memcmp(text.data() + (text.size() - suffix.size()), suffix.data(), suffix.size()) == 0)) {
        value_set_bool(out, true);
        return true;
      }
    }
    value_set_bool(out, false);
    return true;
  }
  std::string_view suffix;
  if (!get_bytes_like_view(args[1], "bytes.endswith suffix", suffix, error)) {
    return false;
  }
  value_set_bool(out, suffix.size() <= text.size() &&
                          (suffix.empty() ||
                           std::memcmp(text.data() + (text.size() - suffix.size()), suffix.data(), suffix.size()) == 0));
  return true;
}

bool bytes_partition_common(const Value* args, uint32_t argc, Value& out, std::string& error, bool reverse) {
  if (!method_check_argc(argc, 2, reverse ? "bytes.rpartition" : "bytes.partition", error)) {
    return false;
  }
  std::string_view text;
  std::string_view sep;
  if (!get_bytes_like_view(args[0], reverse ? "bytes.rpartition target" : "bytes.partition target", text, error) ||
      !get_bytes_like_view(args[1], reverse ? "bytes.rpartition separator" : "bytes.partition separator", sep, error)) {
    return false;
  }
  if (sep.empty()) {
    error = "empty separator";
    return false;
  }
  const size_t pos = reverse ? text.rfind(sep) : text.find(sep);
  if (pos == std::string_view::npos) {
    if (reverse) {
      out = Value::tuple({
          make_binary_like_result(args[0], {}),
          make_binary_like_result(args[0], {}),
          make_binary_like_result(args[0], std::string(text))});
      return true;
    }
    out = Value::tuple({
        make_binary_like_result(args[0], std::string(text)),
        make_binary_like_result(args[0], {}),
        make_binary_like_result(args[0], {})});
    return true;
  }
  out = Value::tuple({
      make_binary_like_result(args[0], std::string(text.substr(0, pos))),
      make_binary_like_result(args[0], std::string(sep)),
      make_binary_like_result(args[0], std::string(text.substr(pos + sep.size())))});
  return true;
}

bool bytes_partition_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return bytes_partition_common(args, argc, out, error, false);
}

bool bytes_rpartition_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return bytes_partition_common(args, argc, out, error, true);
}

bool bytes_strip_common(const Value* args, uint32_t argc, Value& out, std::string& error, bool left, bool right, const char* name) {
  if (argc > 2) {
    error = std::string(name) + " expected optional bytes";
    return false;
  }
  std::string_view text;
  if (!get_bytes_like_view(args[0], name, text, error)) {
    return false;
  }
  std::string_view chars;
  static constexpr char whitespace[] = " \t\n\r\v\f";
  if (argc == 2 && args[1].tag != ValueTag::None) {
    if (!get_bytes_like_view(args[1], name, chars, error)) {
      return false;
    }
  } else {
    chars = std::string_view(whitespace, sizeof(whitespace) - 1);
  }
  auto contains = [&](unsigned char ch) {
    return chars.find(static_cast<char>(ch)) != std::string_view::npos;
  };
  size_t start = 0;
  size_t end = text.size();
  if (left) {
    while (start < end && contains(static_cast<unsigned char>(text[start]))) {
      ++start;
    }
  }
  if (right) {
    while (end > start && contains(static_cast<unsigned char>(text[end - 1]))) {
      --end;
    }
  }
  out = make_binary_like_result(args[0], std::string(text.substr(start, end - start)));
  return true;
}

bool bytes_strip_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return bytes_strip_common(args, argc, out, error, true, true, "bytes.strip");
}

bool bytes_lstrip_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return bytes_strip_common(args, argc, out, error, true, false, "bytes.lstrip");
}

bool bytes_rstrip_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return bytes_strip_common(args, argc, out, error, false, true, "bytes.rstrip");
}

bool bytes_split_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "bytes.split expected optional separator";
    return false;
  }
  std::string_view text;
  if (!get_bytes_like_view(args[0], "bytes.split target", text, error)) {
    return false;
  }
  std::vector<Value> parts;
  if (argc == 1 || args[1].tag == ValueTag::None) {
    size_t i = 0;
    while (i < text.size()) {
      while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
      }
      const size_t start = i;
      while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
      }
      if (start != i) {
        parts.push_back(make_binary_like_result(args[0], std::string(text.substr(start, i - start))));
      }
    }
    out = Value::list(std::move(parts));
    return true;
  }
  std::string_view sep;
  if (!get_bytes_like_view(args[1], "bytes.split separator", sep, error)) {
    return false;
  }
  if (sep.empty()) {
    error = "empty separator";
    return false;
  }
  size_t start = 0;
  while (start <= text.size()) {
    const size_t pos = text.find(sep, start);
    if (pos == std::string_view::npos) {
      parts.push_back(make_binary_like_result(args[0], std::string(text.substr(start))));
      break;
    }
    parts.push_back(make_binary_like_result(args[0], std::string(text.substr(start, pos - start))));
    start = pos + sep.size();
  }
  out = Value::list(std::move(parts));
  return true;
}

bool bytes_join_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "bytes.join", error)) {
    return false;
  }
  std::string_view sep;
  if (!get_bytes_like_view(args[0], "bytes.join separator", sep, error)) {
    return false;
  }
  Value iterator;
  if (!runtime_get_iter(runtime, args[1], iterator, error)) {
    return false;
  }

  std::vector<Value> items;
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    items.push_back(item);
  }

  std::vector<std::string_view> views;
  views.reserve(items.size());
  size_t total = sep.size() * (items.empty() ? 0 : items.size() - 1);
  for (const auto& item : items) {
    std::string_view view;
    if (!get_bytes_like_view(item, "bytes.join item", view, error)) {
      return false;
    }
    total += view.size();
    views.push_back(view);
  }
  std::string result;
  result.reserve(total);
  for (size_t i = 0; i < views.size(); ++i) {
    if (i != 0) {
      result.append(sep.data(), sep.size());
    }
    result.append(views[i].data(), views[i].size());
  }
  out = make_binary_like_result(args[0], std::move(result));
  return true;
}

bool int_to_byte_arg(const Value& value, unsigned char& out, std::string& error) {
  if (value.tag != ValueTag::Int64 || value.as.i64 < 0 || value.as.i64 > 255) {
    error = "byte must be in range(0, 256)";
    return false;
  }
  out = static_cast<unsigned char>(value.as.i64);
  return true;
}

bool append_bytes_from_value(std::string& target, const Value& value, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    target.append(view.data(), view.size());
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    target += bytearray->value;
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    for (size_t i = 0; i < view->size; ++i) {
      Value item;
      if (!sequence_get_item(value, Value::int64(static_cast<int64_t>(i)), item, error)) {
        return false;
      }
      target.push_back(static_cast<char>(item.as.i64));
    }
    return true;
  }
  error = "expected a bytes-like object";
  return false;
}

bool bytearray_append_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "bytearray.append", error)) {
    return false;
  }
  auto* bytearray = value_as_bytearray(args[0]);
  if (bytearray == nullptr) {
    error = "bytearray.append target is not bytearray";
    return false;
  }
  unsigned char byte = 0;
  if (!int_to_byte_arg(args[1], byte, error)) {
    return false;
  }
  bytearray->value.push_back(static_cast<char>(byte));
  value_set_none(out);
  return true;
}

bool bytearray_extend_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "bytearray.extend", error)) {
    return false;
  }
  auto* bytearray = value_as_bytearray(args[0]);
  if (bytearray == nullptr) {
    error = "bytearray.extend target is not bytearray";
    return false;
  }
  if (!append_bytes_from_value(bytearray->value, args[1], error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool bytearray_clear_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "bytearray.clear", error)) {
    return false;
  }
  auto* bytearray = value_as_bytearray(args[0]);
  if (bytearray == nullptr) {
    error = "bytearray.clear target is not bytearray";
    return false;
  }
  bytearray->value.clear();
  value_set_none(out);
  return true;
}

bool bytearray_copy_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "bytearray.copy", error)) {
    return false;
  }
  auto* bytearray = value_as_bytearray(args[0]);
  if (bytearray == nullptr) {
    error = "bytearray.copy target is not bytearray";
    return false;
  }
  out = Value::bytearray(bytearray->value);
  return true;
}

bool bytearray_pop_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "bytearray.pop expected optional index";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* bytearray = value_as_bytearray(args[0]);
  if (bytearray == nullptr) {
    error = "bytearray.pop target is not bytearray";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (bytearray->value.empty()) {
    error = "pop from empty bytearray";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  int64_t index = argc == 2 ? args[1].as.i64 : -1;
  if (argc == 2 && args[1].tag != ValueTag::Int64) {
    error = "bytearray index must be int";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (index < 0) index += static_cast<int64_t>(bytearray->value.size());
  if (index < 0 || index >= static_cast<int64_t>(bytearray->value.size())) {
    error = "bytearray index out of range";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  const auto pos = static_cast<size_t>(index);
  value_set_int64(out, static_cast<unsigned char>(bytearray->value[pos]));
  bytearray->value.erase(bytearray->value.begin() + static_cast<std::ptrdiff_t>(pos));
  return true;
}

bool bytearray_remove_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "bytearray.remove", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* bytearray = value_as_bytearray(args[0]);
  if (bytearray == nullptr) {
    error = "bytearray.remove target is not bytearray";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  unsigned char byte = 0;
  if (!int_to_byte_arg(args[1], byte, error)) {
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  const auto it = std::find(bytearray->value.begin(), bytearray->value.end(), static_cast<char>(byte));
  if (it == bytearray->value.end()) {
    error = "value not found in bytearray";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  bytearray->value.erase(it);
  value_set_none(out);
  return true;
}

bool bytearray_reverse_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "bytearray.reverse", error)) {
    return false;
  }
  auto* bytearray = value_as_bytearray(args[0]);
  if (bytearray == nullptr) {
    error = "bytearray.reverse target is not bytearray";
    return false;
  }
  std::reverse(bytearray->value.begin(), bytearray->value.end());
  value_set_none(out);
  return true;
}

bool memoryview_tobytes_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "memoryview.tobytes expected optional order";
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr) {
    error = "memoryview.tobytes target is not memoryview";
    return false;
  }
  if (view->released) {
    error = "operation forbidden on released memoryview object";
    return false;
  }
  if (argc == 2 && args[1].tag != ValueTag::None) {
    std::string order;
    if (!get_string_arg(args[1], "memoryview.tobytes order", order, error)) {
      return false;
    }
    if (order != "C" && order != "F" && order != "A") {
      error = "memoryview.tobytes order must be 'C', 'F', or 'A'";
      return false;
    }
  }
  std::string bytes;
  if (!append_bytes_from_value(bytes, args[0], error)) {
    return false;
  }
  out = Value::bytes(std::move(bytes));
  return true;
}

bool memoryview_tolist_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "memoryview.tolist", error)) {
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr) {
    error = "memoryview.tolist target is not memoryview";
    return false;
  }
  if (view->released) {
    error = "operation forbidden on released memoryview object";
    return false;
  }
  std::vector<Value> items;
  items.reserve(view->size);
  for (size_t i = 0; i < view->size; ++i) {
    Value item;
    if (!sequence_get_item(args[0], Value::int64(static_cast<int64_t>(i)), item, error)) {
      return false;
    }
    items.push_back(item);
  }
  out = Value::list(std::move(items));
  return true;
}

bool append_hex_string(std::string_view bytes, std::string_view sep, int64_t bytes_per_sep, std::string& result, std::string& error) {
  if (bytes_per_sep == 0) {
    error = "bytes_per_sep must not be zero";
    return false;
  }
  static constexpr char digits[] = "0123456789abcdef";
  result.clear();
  result.reserve(bytes.size() * 2 + (sep.empty() ? 0 : bytes.size() / static_cast<size_t>(std::llabs(bytes_per_sep)) * sep.size()));
  const auto group = static_cast<size_t>(std::llabs(bytes_per_sep));
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (!sep.empty() && i != 0) {
      const bool insert =
          bytes_per_sep > 0 ? ((bytes.size() - i) % group == 0) : (i % group == 0);
      if (insert) {
        result.append(sep.data(), sep.size());
      }
    }
    const auto byte = static_cast<unsigned char>(bytes[i]);
    result.push_back(digits[byte >> 4u]);
    result.push_back(digits[byte & 0x0fu]);
  }
  return true;
}

bool memoryview_hex_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "memoryview.hex expected optional sep and bytes_per_sep";
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr) {
    error = "memoryview.hex target is not memoryview";
    return false;
  }
  if (view->released) {
    error = "operation forbidden on released memoryview object";
    return false;
  }
  std::string bytes;
  if (!append_bytes_from_value(bytes, args[0], error)) {
    return false;
  }
  std::string sep;
  int64_t bytes_per_sep = 1;
  if (argc >= 2) {
    if (!get_string_arg(args[1], "memoryview.hex sep", sep, error)) {
      return false;
    }
  }
  if (argc == 3) {
    if (args[2].tag != ValueTag::Int64) {
      error = "memoryview.hex bytes_per_sep must be int";
      return false;
    }
    bytes_per_sep = args[2].as.i64;
  }
  std::string result;
  if (!append_hex_string(bytes, sep, bytes_per_sep, result, error)) {
    return false;
  }
  out = Value::string(std::move(result));
  return true;
}

bool memoryview_element_arg(const Value& value, int64_t& out, std::string& error) {
  if (value.tag == ValueTag::Int64) {
    if (value.as.i64 < 0 || value.as.i64 > 255) {
      error = "memoryview element must be in range(0, 256)";
      return false;
    }
    out = value.as.i64;
    return true;
  }
  std::string bytes;
  if (!append_bytes_from_value(bytes, value, error)) {
    error = "memoryview element must be int or single-byte bytes-like object";
    return false;
  }
  if (bytes.size() != 1) {
    error = "memoryview element bytes-like object must have length 1";
    return false;
  }
  out = static_cast<unsigned char>(bytes[0]);
  return true;
}

bool normalize_memoryview_search_bounds(size_t size, const Value* args, uint32_t argc, size_t& start, size_t& stop, std::string& error) {
  start = 0;
  stop = size;
  if (argc >= 3) {
    if (args[2].tag != ValueTag::Int64) {
      error = "memoryview.index start must be int";
      return false;
    }
    int64_t value = args[2].as.i64;
    if (value < 0) value += static_cast<int64_t>(size);
    if (value < 0) value = 0;
    if (value > static_cast<int64_t>(size)) value = static_cast<int64_t>(size);
    start = static_cast<size_t>(value);
  }
  if (argc >= 4) {
    if (args[3].tag != ValueTag::Int64) {
      error = "memoryview.index stop must be int";
      return false;
    }
    int64_t value = args[3].as.i64;
    if (value < 0) value += static_cast<int64_t>(size);
    if (value < 0) value = 0;
    if (value > static_cast<int64_t>(size)) value = static_cast<int64_t>(size);
    stop = static_cast<size_t>(value);
  }
  if (stop < start) {
    stop = start;
  }
  return true;
}

bool memoryview_count_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "memoryview.count", error)) {
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr) {
    error = "memoryview.count target is not memoryview";
    return false;
  }
  if (view->released) {
    error = "operation forbidden on released memoryview object";
    return false;
  }
  int64_t needle = 0;
  if (!memoryview_element_arg(args[1], needle, error)) {
    return false;
  }
  std::string bytes;
  if (!append_bytes_from_value(bytes, args[0], error)) {
    return false;
  }
  int64_t count = 0;
  for (unsigned char byte : bytes) {
    if (byte == static_cast<unsigned char>(needle)) {
      ++count;
    }
  }
  value_set_int64(out, count);
  return true;
}

bool memoryview_index_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "memoryview.index expected value, start, and stop";
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr) {
    error = "memoryview.index target is not memoryview";
    return false;
  }
  if (view->released) {
    error = "operation forbidden on released memoryview object";
    return false;
  }
  int64_t needle = 0;
  if (!memoryview_element_arg(args[1], needle, error)) {
    return false;
  }
  size_t start = 0;
  size_t stop = 0;
  if (!normalize_memoryview_search_bounds(view->size, args, argc, start, stop, error)) {
    return false;
  }
  std::string bytes;
  if (!append_bytes_from_value(bytes, args[0], error)) {
    return false;
  }
  for (size_t i = start; i < stop && i < bytes.size(); ++i) {
    if (static_cast<unsigned char>(bytes[i]) == static_cast<unsigned char>(needle)) {
      value_set_int64(out, static_cast<int64_t>(i));
      return true;
    }
  }
  error = "memoryview.index(x): x not in view";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool memoryview_cast_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "memoryview.cast expected format and optional shape";
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr) {
    error = "memoryview.cast target is not memoryview";
    return false;
  }
  if (view->released) {
    error = "operation forbidden on released memoryview object";
    return false;
  }
  std::string format;
  if (!get_string_arg(args[1], "memoryview.cast format", format, error)) {
    return false;
  }
  if (format != "B" && format != "b" && format != "c") {
    error = "memoryview.cast only supports byte-sized formats";
    return false;
  }
  if (argc == 3 && args[2].tag != ValueTag::None) {
    const TupleObject* shape_tuple = value_as_tuple(args[2]);
    const ListObject* shape_list = value_as_list(args[2]);
    const auto valid_tuple = shape_tuple != nullptr && shape_tuple->items.size() == 1 &&
                             shape_tuple->items[0].tag == ValueTag::Int64 &&
                             shape_tuple->items[0].as.i64 == static_cast<int64_t>(view->size);
    const auto valid_list = shape_list != nullptr && shape_list->items.size() == 1 &&
                            shape_list->items[0].tag == ValueTag::Int64 &&
                            shape_list->items[0].as.i64 == static_cast<int64_t>(view->size);
    if (!valid_tuple && !valid_list) {
      error = "memoryview.cast only supports one-dimensional byte shape";
      return false;
    }
  }
  out = Value::memoryview(view->owner, view->offset, view->size, view->readonly);
  if (auto* cast_view = value_as_memoryview(out)) {
    cast_view->format = std::move(format);
  }
  return true;
}

bool memoryview_toreadonly_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "memoryview.toreadonly", error)) {
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr) {
    error = "memoryview.toreadonly target is not memoryview";
    return false;
  }
  if (view->released) {
    error = "operation forbidden on released memoryview object";
    return false;
  }
  out = Value::memoryview(view->owner, view->offset, view->size, true);
  if (auto* readonly = value_as_memoryview(out)) {
    readonly->format = view->format;
  }
  return true;
}

bool memoryview_release_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "memoryview.release", error)) {
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr) {
    error = "memoryview.release target is not memoryview";
    return false;
  }
  view->released = true;
  value_set_none(out);
  return true;
}

bool memoryview_enter_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "memoryview.__enter__", error)) {
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr || view->released) {
    error = "operation forbidden on released memoryview object";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool memoryview_exit_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 4) {
    error = "memoryview.__exit__ expected exception details";
    return false;
  }
  Value release_out;
  if (!memoryview_release_method(runtime, args, 1, release_out, error, user_data)) {
    return false;
  }
  value_set_bool(out, false);
  return true;
}

} // namespace

bool bytes_get_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag != ValueTag::Object || object.as.obj == nullptr || object.as.obj->kind != ObjectKind::Bytes) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"count", "bytes.count", bytes_count_method},
      {"decode", "bytes.decode", bytes_decode_method},
      {"endswith", "bytes.endswith", bytes_endswith_method},
      {"find", "bytes.find", bytes_find_method},
      {"hex", "bytes.hex", bytes_hex_method},
      {"index", "bytes.index", bytes_index_method},
      {"join", "bytes.join", bytes_join_method},
      {"lstrip", "bytes.lstrip", bytes_lstrip_method},
      {"partition", "bytes.partition", bytes_partition_method},
      {"replace", "bytes.replace", bytes_replace_method},
      {"rfind", "bytes.rfind", bytes_rfind_method},
      {"rindex", "bytes.rindex", bytes_rindex_method},
      {"rpartition", "bytes.rpartition", bytes_rpartition_method},
      {"rstrip", "bytes.rstrip", bytes_rstrip_method},
      {"split", "bytes.split", bytes_split_method},
      {"startswith", "bytes.startswith", bytes_startswith_method},
      {"strip", "bytes.strip", bytes_strip_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

bool bytearray_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_bytearray(object) == nullptr) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"append", "bytearray.append", bytearray_append_method},
      {"clear", "bytearray.clear", bytearray_clear_method},
      {"copy", "bytearray.copy", bytearray_copy_method},
      {"count", "bytearray.count", bytes_count_method},
      {"decode", "bytearray.decode", bytes_decode_method},
      {"endswith", "bytearray.endswith", bytes_endswith_method},
      {"extend", "bytearray.extend", bytearray_extend_method},
      {"find", "bytearray.find", bytes_find_method},
      {"hex", "bytearray.hex", bytes_hex_method},
      {"index", "bytearray.index", bytes_index_method},
      {"join", "bytearray.join", bytes_join_method},
      {"lstrip", "bytearray.lstrip", bytes_lstrip_method},
      {"pop", "bytearray.pop", bytearray_pop_method},
      {"partition", "bytearray.partition", bytes_partition_method},
      {"remove", "bytearray.remove", bytearray_remove_method},
      {"replace", "bytearray.replace", bytes_replace_method},
      {"reverse", "bytearray.reverse", bytearray_reverse_method},
      {"rfind", "bytearray.rfind", bytes_rfind_method},
      {"rindex", "bytearray.rindex", bytes_rindex_method},
      {"rpartition", "bytearray.rpartition", bytes_rpartition_method},
      {"rstrip", "bytearray.rstrip", bytes_rstrip_method},
      {"split", "bytearray.split", bytes_split_method},
      {"startswith", "bytearray.startswith", bytes_startswith_method},
      {"strip", "bytearray.strip", bytes_strip_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

bool memoryview_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_memoryview(object) == nullptr) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"__enter__", "memoryview.__enter__", memoryview_enter_method},
      {"__exit__", "memoryview.__exit__", memoryview_exit_method},
      {"cast", "memoryview.cast", memoryview_cast_method},
      {"count", "memoryview.count", memoryview_count_method},
      {"hex", "memoryview.hex", memoryview_hex_method},
      {"index", "memoryview.index", memoryview_index_method},
      {"release", "memoryview.release", memoryview_release_method},
      {"tobytes", "memoryview.tobytes", memoryview_tobytes_method},
      {"tolist", "memoryview.tolist", memoryview_tolist_method},
      {"toreadonly", "memoryview.toreadonly", memoryview_toreadonly_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
