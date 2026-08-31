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
#include "xlang3/cp437_codec.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include "runtime/memory/x3_string_ref.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

memory::X3StringView get_string_view(const Value& value, const char* name, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = std::string(name) + " must be a string";
    return {};
  }
  const auto* text = reinterpret_cast<StringObject*>(value.as.obj);
  return memory::x3_string_view(string_object_view(*text));
}

bool get_string_view_checked(const Value& value, const char* name, memory::X3StringView& out, std::string& error) {
  out = get_string_view(value, name, error);
  return out.data != nullptr || out.size == 0 && error.empty();
}

XLANG3_HOT_INLINE bool string_ascii_isspace(unsigned char ch) {
  return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

std::string_view as_view(memory::X3StringView value) {
  return memory::x3_to_string_view(value);
}

Value make_string_from_view(memory::X3StringView text) {
  return Value::string_view(std::string_view(text.data == nullptr ? "" : text.data, text.size));
}

Value make_string_range(memory::X3StringView text, size_t start, size_t size) {
  auto text_view = as_view(text);
  if (start > text_view.size()) {
    start = text_view.size();
  }
  if (size > text_view.size() - start) {
    size = text_view.size() - start;
  }
  return Value::string_view(std::string_view(text_view.data() + start, size));
}

Value make_string_range_unchecked(memory::X3StringView text, size_t start, size_t size) {
  return Value::string_view(std::string_view(text.data + start, size));
}

Value make_uninitialized_string_value(size_t size, char*& data) {
  Value out = Value::string_uninitialized(size);
  auto* string = value_as_string(out);
  data = string == nullptr ? nullptr : string_object_mutable_data(*string);
  if (data == nullptr) {
    return Value::invalid();
  }
  return out;
}

uint32_t decode_utf8_codepoint(std::string_view text, size_t width) {
  if (width == 1) {
    return static_cast<unsigned char>(text[0]);
  }
  uint32_t codepoint = static_cast<unsigned char>(text[0]) & ((1u << (7 - width)) - 1u);
  for (size_t i = 1; i < width; ++i) {
    codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i]) & 0x3fu);
  }
  return codepoint;
}

void append_ascii_backslash_escape(uint32_t codepoint, std::string& out) {
  static constexpr char digits[] = "0123456789abcdef";
  if (codepoint <= 0xff) {
    out += "\\x";
    out.push_back(digits[(codepoint >> 4) & 0x0f]);
    out.push_back(digits[codepoint & 0x0f]);
  } else if (codepoint <= 0xffff) {
    out += "\\u";
    for (int shift = 12; shift >= 0; shift -= 4) {
      out.push_back(digits[(codepoint >> shift) & 0x0f]);
    }
  } else {
    out += "\\U";
    for (int shift = 28; shift >= 0; shift -= 4) {
      out.push_back(digits[(codepoint >> shift) & 0x0f]);
    }
  }
}

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
  if (name == "437" || name == "cp437" || name == "ibm437") {
    return "cp437";
  }
  return name;
}

std::string latin1_encode_text(Runtime& runtime, std::string_view text, const std::string& errors, std::string& error) {
  std::string encoded;
  encoded.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    const size_t width = utf8_codepoint_width(ch);
    const uint32_t codepoint = width == 0 || i + width > text.size() ? ch : decode_utf8_codepoint(text.substr(i), width);
    const size_t advance = width == 0 ? 1 : width;
    if (codepoint <= 0xff) {
      encoded.push_back(static_cast<char>(codepoint));
      i += advance;
    } else if (errors == "ignore") {
      i += advance;
    } else if (errors == "replace") {
      encoded.push_back('?');
      i += advance;
    } else if (errors == "backslashreplace") {
      append_ascii_backslash_escape(codepoint, encoded);
      i += advance;
    } else {
      error = "latin-1 codec can't encode character";
      runtime.raise_class_error("UnicodeEncodeError", error);
      return {};
    }
  }
  return encoded;
}

/*
Native string methods must be alias-safe: the destination register can be the
same VM register as the receiver or an argument. When a method keeps a
StringObject view into its inputs, build the result in a local Value first and
publish it only after all input reads are complete.
*/
bool publish_string_result(Value& target, Value& result) {
  if (result.tag == ValueTag::Invalid) {
    return false;
  }
  value_move_assign_fast(target, result);
  return true;
}

size_t count_non_overlapping_matches(
    memory::X3StringView text,
    memory::X3StringView needle,
    int64_t max_count) {
  auto text_view = as_view(text);
  auto needle_view = as_view(needle);
  if (needle_view.empty() || max_count == 0) {
    return 0;
  }
  size_t count = 0;
  size_t start = 0;
  while (max_count < 0 || count < static_cast<size_t>(max_count)) {
    const auto pos = text_view.find(needle_view, start);
    if (pos == std::string::npos) {
      break;
    }
    ++count;
    start = pos + needle_view.size();
  }
  return count;
}

bool replace_string_body(
    const Value& original,
    memory::X3StringView text,
    memory::X3StringView old_text,
    memory::X3StringView new_text,
    int64_t max_count,
    Value& out) {
  auto text_view = as_view(text);
  auto old_view = as_view(old_text);
  auto new_view = as_view(new_text);
  if (max_count == 0) {
    value_assign_fast(out, original);
    return true;
  }
  if (old_view.empty()) {
    out = make_string_from_view(text);
    return true;
  }
  if (old_view.size() == 1 && new_view.size() == 1 && max_count < 0) {
    char* result = nullptr;
    Value result_value = make_uninitialized_string_value(text_view.size(), result);
    if (result == nullptr) {
      return false;
    }
    const char old_ch = old_view[0];
    const char new_ch = new_view[0];
    for (size_t i = 0; i < text_view.size(); ++i) {
      const char ch = text_view[i];
      result[i] = ch == old_ch ? new_ch : ch;
    }
    return publish_string_result(out, result_value);
  }

  const size_t match_count = count_non_overlapping_matches(text, old_text, max_count);
  if (match_count == 0) {
    value_assign_fast(out, original);
    return true;
  }

  const size_t result_size =
      new_view.size() >= old_view.size()
          ? text_view.size() + (new_view.size() - old_view.size()) * match_count
          : text_view.size() - (old_view.size() - new_view.size()) * match_count;
  char* result = nullptr;
  Value result_value = make_uninitialized_string_value(result_size, result);
  if (result == nullptr) {
    return false;
  }
  size_t write = 0;
  size_t start = 0;
  size_t count = 0;
  while (count < match_count) {
    const auto pos = text_view.find(old_view, start);
    const size_t prefix_size = pos - start;
    if (prefix_size != 0) {
      std::memcpy(result + write, text_view.data() + start, prefix_size);
      write += prefix_size;
    }
    if (!new_view.empty()) {
      std::memcpy(result + write, new_view.data(), new_view.size());
      write += new_view.size();
    }
    start = pos + old_view.size();
    ++count;
  }
  const size_t suffix_size = text_view.size() - start;
  if (suffix_size != 0) {
    std::memcpy(result + write, text_view.data() + start, suffix_size);
  }
  return publish_string_result(out, result_value);
}

template <typename Items>
bool join_string_values(
    memory::X3StringView sep,
    const Items& items,
    Value& out,
    std::string& error) {
  constexpr size_t kStackJoinViewCount = 8;
  std::array<memory::X3StringView, kStackJoinViewCount> stack_views{};
  std::vector<memory::X3StringView> heap_views;
  memory::X3StringView* views = stack_views.data();
  if (items.size() > kStackJoinViewCount) {
    heap_views.resize(items.size());
    views = heap_views.data();
  }

  size_t total_size = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    memory::X3StringView item;
    if (!get_string_view_checked(items[i], "str.join item", item, error)) {
      return false;
    }
    views[i] = item;
    total_size += item.size;
  }
  if (!items.empty()) {
    total_size += sep.size * (items.size() - 1);
  }

  char* result = nullptr;
  Value result_value = make_uninitialized_string_value(total_size, result);
  if (result == nullptr) {
    return false;
  }
  size_t write = 0;
  for (size_t i = 0; i < items.size(); ++i) {
    const memory::X3StringView item = views[i];
    if (i != 0 && sep.size != 0) {
      std::memcpy(result + write, sep.data, sep.size);
      write += sep.size;
    }
    if (item.size != 0) {
      std::memcpy(result + write, item.data, item.size);
      write += item.size;
    }
  }
  return publish_string_result(out, result_value);
}

bool collect_join_iterable(Runtime& runtime, const Value& iterable, std::vector<Value>& items, std::string& error) {
  Value iterator;
  if (!runtime_get_iter(runtime, iterable, iterator, error)) {
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    items.push_back(std::move(item));
  }
}

bool transform_ascii_case(
    const Value& value,
    const char* target_name,
    bool upper,
    Value& out,
    std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, target_name, text, error)) {
    return false;
  }
  const auto view = as_view(text);
  char* result = nullptr;
  Value result_value = make_uninitialized_string_value(view.size(), result);
  if (result == nullptr) {
    return false;
  }
  for (size_t i = 0; i < view.size(); ++i) {
    const auto ch = static_cast<unsigned char>(view[i]);
    result[i] = static_cast<char>(upper ? std::toupper(ch) : std::tolower(ch));
  }
  return publish_string_result(out, result_value);
}

bool string_upper_body(const Value& value, Value& out, std::string& error) {
  return transform_ascii_case(value, "str.upper target", true, out, error);
}

bool string_lower_body(const Value& value, Value& out, std::string& error) {
  return transform_ascii_case(value, "str.lower target", false, out, error);
}

bool trim_char_set_contains(memory::X3StringView chars, char ch);

bool string_strip_body(const Value& value, const Value* chars_value, Value& out, std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.strip target", text, error)) {
    return false;
  }
  uint32_t start = 0;
  uint32_t end = text.size;
  if (chars_value == nullptr) {
    while (start < end && memory::x3_is_ascii_space(text.data[start])) {
      ++start;
    }
    while (end > start && memory::x3_is_ascii_space(text.data[end - 1])) {
      --end;
    }
  } else {
    memory::X3StringView chars;
    if (!get_string_view_checked(*chars_value, "str.strip chars", chars, error)) {
      return false;
    }
    while (start < end && trim_char_set_contains(chars, text.data[start])) {
      ++start;
    }
    while (end > start && trim_char_set_contains(chars, text.data[end - 1])) {
      --end;
    }
  }
  if (start == 0 && end == text.size) {
    value_assign_fast(out, value);
    return true;
  }
  out = make_string_from_view(memory::X3StringView{text.data + start, end - start});
  return true;
}

bool trim_char_set_contains(memory::X3StringView chars, char ch) {
  for (uint32_t i = 0; i < chars.size; ++i) {
    if (chars.data[i] == ch) {
      return true;
    }
  }
  return false;
}

bool string_rstrip_body(const Value& value, const Value* chars_value, Value& out, std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.rstrip target", text, error)) {
    return false;
  }

  uint32_t end = text.size;
  if (chars_value == nullptr) {
    while (end > 0 && memory::x3_is_ascii_space(text.data[end - 1])) {
      --end;
    }
  } else {
    memory::X3StringView chars;
    if (!get_string_view_checked(*chars_value, "str.rstrip chars", chars, error)) {
      return false;
    }
    while (end > 0 && trim_char_set_contains(chars, text.data[end - 1])) {
      --end;
    }
  }

  if (end == text.size) {
    value_assign_fast(out, value);
    return true;
  }
  out = make_string_from_view(memory::X3StringView{text.data, end});
  return true;
}

bool string_lstrip_body(const Value& value, const Value* chars_value, Value& out, std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.lstrip target", text, error)) {
    return false;
  }

  uint32_t start = 0;
  if (chars_value == nullptr) {
    while (start < text.size && memory::x3_is_ascii_space(text.data[start])) {
      ++start;
    }
  } else {
    memory::X3StringView chars;
    if (!get_string_view_checked(*chars_value, "str.lstrip chars", chars, error)) {
      return false;
    }
    while (start < text.size && trim_char_set_contains(chars, text.data[start])) {
      ++start;
    }
  }

  if (start == 0) {
    value_assign_fast(out, value);
    return true;
  }
  out = make_string_from_view(memory::X3StringView{text.data + start, text.size - start});
  return true;
}

bool string_index_arg(const Value& value, int64_t default_value, int64_t length, int64_t& out, std::string& error) {
  if (value.tag == ValueTag::None) {
    out = default_value;
    return true;
  }
  if (value.tag != ValueTag::Int64) {
    error = "slice indices must be integers or None";
    return false;
  }
  out = value.as.i64;
  if (out < 0) {
    out += length;
    if (out < 0) {
      out = 0;
    }
  }
  if (out > length) {
    out = length;
  }
  return true;
}

bool string_bounds_from_args(memory::X3StringView text, const Value* start_value, const Value* end_value, size_t& start, size_t& end, std::string& error) {
  const int64_t length = static_cast<int64_t>(text.size);
  int64_t start_i = 0;
  int64_t end_i = length;
  if (start_value != nullptr && !string_index_arg(*start_value, 0, length, start_i, error)) {
    return false;
  }
  if (end_value != nullptr && !string_index_arg(*end_value, length, length, end_i, error)) {
    return false;
  }
  if (end_i < start_i) {
    end_i = start_i;
  }
  start = static_cast<size_t>(start_i);
  end = static_cast<size_t>(end_i);
  return true;
}

bool string_startswith_body(
    const Value& value,
    const Value& prefix_value,
    const Value* start_value,
    const Value* end_value,
    Value& out,
    std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.startswith target", text, error)) {
    return false;
  }
  size_t start = 0;
  size_t end = text.size;
  if (!string_bounds_from_args(text, start_value, end_value, start, end, error)) {
    return false;
  }
  const size_t span = end - start;
  const char* span_data = text.data == nullptr ? "" : text.data + start;
  if (auto* tuple = value_as_tuple(prefix_value)) {
    for (uint32_t i = 0; i < tuple->items.size(); ++i) {
      memory::X3StringView prefix;
      if (!get_string_view_checked(tuple->items[i], "str.startswith prefix", prefix, error)) {
        return false;
      }
      if (prefix.size <= span &&
          (prefix.size == 0 || std::memcmp(span_data, prefix.data, prefix.size) == 0)) {
        value_set_bool(out, true);
        return true;
      }
    }
    value_set_bool(out, false);
    return true;
  }
  memory::X3StringView prefix;
  if (!get_string_view_checked(prefix_value, "str.startswith prefix", prefix, error)) {
    return false;
  }
  value_set_bool(out, prefix.size <= span &&
                          (prefix.size == 0 || std::memcmp(span_data, prefix.data, prefix.size) == 0));
  return true;
}

bool string_endswith_body(
    const Value& value,
    const Value& suffix_value,
    const Value* start_value,
    const Value* end_value,
    Value& out,
    std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.endswith target", text, error)) {
    return false;
  }
  size_t start = 0;
  size_t end = text.size;
  if (!string_bounds_from_args(text, start_value, end_value, start, end, error)) {
    return false;
  }
  const size_t span = end - start;
  const char* span_data = text.data == nullptr ? "" : text.data + start;
  if (auto* tuple = value_as_tuple(suffix_value)) {
    for (uint32_t i = 0; i < tuple->items.size(); ++i) {
      memory::X3StringView suffix;
      if (!get_string_view_checked(tuple->items[i], "str.endswith suffix", suffix, error)) {
        return false;
      }
      if (suffix.size <= span &&
          (suffix.size == 0 ||
           std::memcmp(span_data + (span - suffix.size), suffix.data, suffix.size) == 0)) {
        value_set_bool(out, true);
        return true;
      }
    }
    value_set_bool(out, false);
    return true;
  }
  memory::X3StringView suffix;
  if (!get_string_view_checked(suffix_value, "str.endswith suffix", suffix, error)) {
    return false;
  }
  value_set_bool(out, suffix.size <= span &&
                          (suffix.size == 0 ||
                           std::memcmp(span_data + (span - suffix.size), suffix.data, suffix.size) == 0));
  return true;
}

bool string_find_body(
    const Value& value,
    const Value& needle_value,
    const Value* start_value,
    const Value* end_value,
    Value& out,
    std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.find target", text, error)) {
    return false;
  }
  memory::X3StringView needle;
  if (!get_string_view_checked(needle_value, "str.find substring", needle, error)) {
    return false;
  }
  size_t start = 0;
  size_t end = text.size;
  if (!string_bounds_from_args(text, start_value, end_value, start, end, error)) {
    return false;
  }
  const auto span = memory::X3StringView{text.data == nullptr ? nullptr : text.data + start, static_cast<uint32_t>(end - start)};
  if (needle.size == 1) {
    const void* pos = std::memchr(span.data, static_cast<unsigned char>(needle.data[0]), span.size);
    value_set_int64(out, pos == nullptr ? -1 : static_cast<int64_t>(start + (static_cast<const char*>(pos) - span.data)));
    return true;
  }
  const auto pos = as_view(span).find(as_view(needle));
  value_set_int64(out, pos == std::string::npos ? -1 : static_cast<int64_t>(start + pos));
  return true;
}

bool string_rfind_body(
    const Value& value,
    const Value& needle_value,
    const Value* start_value,
    const Value* end_value,
    Value& out,
    std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.rfind target", text, error)) {
    return false;
  }
  memory::X3StringView needle;
  if (!get_string_view_checked(needle_value, "str.rfind substring", needle, error)) {
    return false;
  }
  size_t start = 0;
  size_t end = text.size;
  if (!string_bounds_from_args(text, start_value, end_value, start, end, error)) {
    return false;
  }
  const auto span = memory::X3StringView{text.data == nullptr ? nullptr : text.data + start, static_cast<uint32_t>(end - start)};
  const auto pos = as_view(span).rfind(as_view(needle));
  value_set_int64(out, pos == std::string::npos ? -1 : static_cast<int64_t>(start + pos));
  return true;
}

bool string_count_body(const Value& value, const Value& needle_value, Value& out, std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.count target", text, error)) {
    return false;
  }
  memory::X3StringView needle;
  if (!get_string_view_checked(needle_value, "str.count substring", needle, error)) {
    return false;
  }
  auto text_view = as_view(text);
  auto needle_view = as_view(needle);
  if (needle_view.empty()) {
    value_set_int64(out, static_cast<int64_t>(text_view.size() + 1));
    return true;
  }
  if (needle.size == 1) {
    int64_t count = 0;
    const char needle_ch = needle.data[0];
    for (uint32_t i = 0; i < text.size; ++i) {
      if (text.data[i] == needle_ch) {
        ++count;
      }
    }
    value_set_int64(out, count);
    return true;
  }
  int64_t count = 0;
  size_t start = 0;
  while (true) {
    const auto pos = text_view.find(needle_view, start);
    if (pos == std::string::npos) {
      break;
    }
    ++count;
    start = pos + needle_view.size();
  }
  value_set_int64(out, count);
  return true;
}

bool string_upper_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.upper", error)) {
    return false;
  }
  return string_upper_body(args[0], out, error);
}

bool string_lower_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.lower", error)) {
    return false;
  }
  return string_lower_body(args[0], out, error);
}

bool string_strip_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.strip expected 0 or 1 arguments";
    return false;
  }
  return string_strip_body(args[0], argc == 2 ? &args[1] : nullptr, out, error);
}

bool string_rstrip_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.rstrip expected 0 or 1 arguments";
    return false;
  }
  return string_rstrip_body(args[0], argc == 2 ? &args[1] : nullptr, out, error);
}

bool string_lstrip_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.lstrip expected 0 or 1 arguments";
    return false;
  }
  return string_lstrip_body(args[0], argc == 2 ? &args[1] : nullptr, out, error);
}

bool string_upper_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value*,
    const uint32_t*,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count != 0 || leading == nullptr) {
    error = "str.upper expected no arguments";
    return false;
  }
  return string_upper_body(leading[0], out, error);
}

bool string_lower_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value*,
    const uint32_t*,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count != 0 || leading == nullptr) {
    error = "str.lower expected no arguments";
    return false;
  }
  return string_lower_body(leading[0], out, error);
}

bool string_strip_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count > 1 || leading == nullptr) {
    error = "str.strip expected 0 or 1 arguments";
    return false;
  }
  const Value* chars_value = register_arg_count == 0 ? nullptr : &registers[register_args[0]];
  return string_strip_body(leading[0], chars_value, out, error);
}

bool string_rstrip_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count > 1 || leading == nullptr) {
    error = "str.rstrip expected 0 or 1 arguments";
    return false;
  }
  const Value* chars_value = register_arg_count == 0 ? nullptr : &registers[register_args[0]];
  return string_rstrip_body(leading[0], chars_value, out, error);
}

bool string_lstrip_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count > 1 || leading == nullptr) {
    error = "str.lstrip expected 0 or 1 arguments";
    return false;
  }
  const Value* chars_value = register_arg_count == 0 ? nullptr : &registers[register_args[0]];
  return string_lstrip_body(leading[0], chars_value, out, error);
}

bool string_startswith_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "str.startswith expected 1 to 3 arguments";
    return false;
  }
  const Value* start_value = argc >= 3 ? &args[2] : nullptr;
  const Value* end_value = argc >= 4 ? &args[3] : nullptr;
  return string_startswith_body(args[0], args[1], start_value, end_value, out, error);
}

bool string_startswith_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count < 1 || register_arg_count > 3 ||
      leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.startswith expected 1 to 3 arguments";
    return false;
  }
  const Value* start_value = register_arg_count >= 2 ? &registers[register_args[1]] : nullptr;
  const Value* end_value = register_arg_count >= 3 ? &registers[register_args[2]] : nullptr;
  return string_startswith_body(leading[0], registers[register_args[0]], start_value, end_value, out, error);
}

bool string_endswith_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "str.endswith expected 1 to 3 arguments";
    return false;
  }
  const Value* start_value = argc >= 3 ? &args[2] : nullptr;
  const Value* end_value = argc >= 4 ? &args[3] : nullptr;
  return string_endswith_body(args[0], args[1], start_value, end_value, out, error);
}

bool string_endswith_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count < 1 || register_arg_count > 3 ||
      leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.endswith expected 1 to 3 arguments";
    return false;
  }
  const Value* start_value = register_arg_count >= 2 ? &registers[register_args[1]] : nullptr;
  const Value* end_value = register_arg_count >= 3 ? &registers[register_args[2]] : nullptr;
  return string_endswith_body(leading[0], registers[register_args[0]], start_value, end_value, out, error);
}

bool string_find_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "str.find expected 1 to 3 arguments";
    return false;
  }
  const Value* start_value = argc >= 3 ? &args[2] : nullptr;
  const Value* end_value = argc >= 4 ? &args[3] : nullptr;
  return string_find_body(args[0], args[1], start_value, end_value, out, error);
}

bool string_find_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count < 1 || register_arg_count > 3 ||
      leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.find expected 1 to 3 arguments";
    return false;
  }
  const Value* start_value = register_arg_count >= 2 ? &registers[register_args[1]] : nullptr;
  const Value* end_value = register_arg_count >= 3 ? &registers[register_args[2]] : nullptr;
  return string_find_body(leading[0], registers[register_args[0]], start_value, end_value, out, error);
}

bool string_count_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.count", error)) {
    return false;
  }
  return string_count_body(args[0], args[1], out, error);
}

bool string_rfind_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "str.rfind expected 1 to 3 arguments";
    return false;
  }
  const Value* start_value = argc >= 3 ? &args[2] : nullptr;
  const Value* end_value = argc >= 4 ? &args[3] : nullptr;
  return string_rfind_body(args[0], args[1], start_value, end_value, out, error);
}

bool string_index_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "str.index expected 1 to 3 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* start_value = argc >= 3 ? &args[2] : nullptr;
  const Value* end_value = argc >= 4 ? &args[3] : nullptr;
  if (!string_find_body(args[0], args[1], start_value, end_value, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (out.tag == ValueTag::Int64 && out.as.i64 < 0) {
    error = "substring not found";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

bool string_rindex_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "str.rindex expected 1 to 3 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* start_value = argc >= 3 ? &args[2] : nullptr;
  const Value* end_value = argc >= 4 ? &args[3] : nullptr;
  if (!string_rfind_body(args[0], args[1], start_value, end_value, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (out.tag == ValueTag::Int64 && out.as.i64 < 0) {
    error = "substring not found";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

bool string_count_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count != 1 || leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.count expected 1 argument";
    return false;
  }
  return string_count_body(leading[0], registers[register_args[0]], out, error);
}

bool string_replace_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3 && argc != 4) {
    error = "str.replace expected 3 or 4 arguments, got " + std::to_string(argc);
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.replace target", text, error)) {
    return false;
  }
  memory::X3StringView old_text;
  if (!get_string_view_checked(args[1], "str.replace old", old_text, error)) {
    return false;
  }
  memory::X3StringView new_text;
  if (!get_string_view_checked(args[2], "str.replace new", new_text, error)) {
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
  return replace_string_body(args[0], text, old_text, new_text, max_count, out);
}

bool string_replace_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || (register_arg_count != 2 && register_arg_count != 3) ||
      leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.replace expected 2 or 3 arguments";
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(leading[0], "str.replace target", text, error)) {
    return false;
  }
  memory::X3StringView old_text;
  if (!get_string_view_checked(registers[register_args[0]], "str.replace old", old_text, error)) {
    return false;
  }
  memory::X3StringView new_text;
  if (!get_string_view_checked(registers[register_args[1]], "str.replace new", new_text, error)) {
    return false;
  }
  int64_t max_count = -1;
  if (register_arg_count == 3) {
    const Value& count_value = registers[register_args[2]];
    if (count_value.tag != ValueTag::Int64) {
      error = "str.replace count must be int";
      return false;
    }
    max_count = count_value.as.i64;
  }
  return replace_string_body(leading[0], text, old_text, new_text, max_count, out);
}

bool string_join_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.join", error)) {
    return false;
  }
  memory::X3StringView sep;
  if (!get_string_view_checked(args[0], "str.join separator", sep, error)) {
    return false;
  }
  if (auto* list = value_as_list(args[1])) {
    return join_string_values(sep, list->items, out, error);
  }
  if (args[1].tag == ValueTag::Object && args[1].as.obj != nullptr && args[1].as.obj->kind == ObjectKind::Tuple) {
    auto* tuple = reinterpret_cast<TupleObject*>(args[1].as.obj);
    return join_string_values(sep, tuple->items, out, error);
  }
  std::vector<Value> items;
  if (!collect_join_iterable(runtime, args[1], items, error)) {
    error = "str.join argument must be iterable";
    return false;
  }
  return join_string_values(sep, items, out, error);
}

bool string_join_fast_method(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count != 1 || leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.join expected 1 argument";
    return false;
  }
  memory::X3StringView sep;
  if (!get_string_view_checked(leading[0], "str.join separator", sep, error)) {
    return false;
  }
  const Value& sequence = registers[register_args[0]];
  if (auto* list = value_as_list(sequence)) {
    return join_string_values(sep, list->items, out, error);
  }
  if (auto* tuple = value_as_tuple(sequence)) {
    return join_string_values(sep, tuple->items, out, error);
  }
  std::vector<Value> items;
  if (!collect_join_iterable(runtime, sequence, items, error)) {
    error = "str.join argument must be iterable";
    return false;
  }
  return join_string_values(sep, items, out, error);
}

bool string_maketrans_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "str.maketrans expected 1 to 3 arguments";
    return false;
  }
  if (argc == 1) {
    auto* dict = value_as_dict(args[0]);
    if (dict == nullptr) {
      error = "if you give only one argument to maketrans it must be a dict";
      return false;
    }
    out = Value::dict({});
    for (const auto& entry : dict->entries) {
      Value key;
      if (auto* text = value_as_string(entry.first)) {
        auto view = string_object_view(*text);
        if (view.size() != 1) {
          error = "string keys in translate table must be of length 1";
          return false;
        }
        key = Value::int64(static_cast<unsigned char>(view.data()[0]));
      } else {
        value_assign_fast(key, entry.first);
      }
      if (!mapping_set_item(out, key, entry.second, error)) {
        return false;
      }
    }
    return true;
  }
  memory::X3StringView from;
  memory::X3StringView to;
  if (!get_string_view_checked(args[0], "str.maketrans x", from, error) ||
      !get_string_view_checked(args[1], "str.maketrans y", to, error)) {
    return false;
  }
  if (from.size != to.size) {
    error = "the first two maketrans arguments must have equal length";
    return false;
  }
  out = Value::dict({});
  for (size_t i = 0; i < from.size; ++i) {
    if (!mapping_set_item(
            out,
            Value::int64(static_cast<unsigned char>(from.data[i])),
            Value::int64(static_cast<unsigned char>(to.data[i])),
            error)) {
      return false;
    }
  }
  if (argc == 3) {
    memory::X3StringView delete_chars;
    if (!get_string_view_checked(args[2], "str.maketrans z", delete_chars, error)) {
      return false;
    }
    for (size_t i = 0; i < delete_chars.size; ++i) {
      if (!mapping_set_item(out, Value::int64(static_cast<unsigned char>(delete_chars.data[i])), Value::none(), error)) {
        return false;
      }
    }
  }
  return true;
}

bool string_translate_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "str.translate expected 1 argument";
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.translate target", text, error)) {
    return false;
  }
  std::string result;
  result.reserve(text.size);
  for (size_t i = 0; i < text.size; ++i) {
    const unsigned char ch = static_cast<unsigned char>(text.data[i]);
    Value replacement;
    std::string lookup_error;
    if (!mapping_get_item(args[1], Value::int64(static_cast<int64_t>(ch)), replacement, lookup_error)) {
      result.push_back(static_cast<char>(ch));
      continue;
    }
    if (replacement.tag == ValueTag::None) {
      continue;
    }
    if (replacement.tag == ValueTag::Int64) {
      if (replacement.as.i64 < 0 || replacement.as.i64 > 255) {
        error = "character mapping must be in range(256)";
        return false;
      }
      result.push_back(static_cast<char>(replacement.as.i64));
      continue;
    }
    memory::X3StringView replacement_text;
    if (!get_string_view_checked(replacement, "str.translate replacement", replacement_text, error)) {
      return false;
    }
    result.append(replacement_text.data, replacement_text.size);
  }
  out = Value::string(std::move(result));
  return true;
}

bool string_format_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "str.format expected at least 1 argument";
    return false;
  }
  memory::X3StringView format_ref;
  if (!get_string_view_checked(args[0], "str.format target", format_ref, error)) {
    return false;
  }
  const auto format = as_view(format_ref);
  std::string result;
  uint32_t next_arg = 1;
  for (size_t i = 0; i < format.size();) {
    if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '{') {
      result.push_back('{');
      i += 2;
      continue;
    }
    if (format[i] == '}' && i + 1 < format.size() && format[i + 1] == '}') {
      result.push_back('}');
      i += 2;
      continue;
    }
    if (format[i] == '{') {
      const auto close = format.find('}', i + 1);
      if (close == std::string::npos) {
        error = "str.format unmatched '{'";
        return false;
      }
      uint32_t arg_index = next_arg++;
      const auto field = format.substr(i + 1, close - i - 1);
      if (!field.empty()) {
        char* end = nullptr;
        std::string field_text(field);
        const auto parsed = std::strtoul(field_text.c_str(), &end, 10);
        if (end != field_text.c_str()) {
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
    result.push_back(format[i++]);
  }
  out = Value::string(std::move(result));
  return true;
}

std::string_view format_field_name(std::string_view field) {
  const size_t conversion = field.find('!');
  const size_t spec = field.find(':');
  size_t end = std::min(
      conversion == std::string_view::npos ? field.size() : conversion,
      spec == std::string_view::npos ? field.size() : spec);
  return field.substr(0, end);
}

const Value* find_format_keyword(
    std::string_view name,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (name == std::string_view(kwargs[i].name)) {
      return kwargs[i].value;
    }
  }
  return nullptr;
}

bool string_format_method_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "str.format expected at least 1 argument";
    return false;
  }
  memory::X3StringView format_ref;
  if (!get_string_view_checked(args[0], "str.format target", format_ref, error)) {
    return false;
  }
  const auto format = as_view(format_ref);
  std::string result;
  uint32_t next_arg = 1;
  for (size_t i = 0; i < format.size();) {
    if (format[i] == '{' && i + 1 < format.size() && format[i + 1] == '{') {
      result.push_back('{');
      i += 2;
      continue;
    }
    if (format[i] == '}' && i + 1 < format.size() && format[i + 1] == '}') {
      result.push_back('}');
      i += 2;
      continue;
    }
    if (format[i] == '{') {
      const auto close = format.find('}', i + 1);
      if (close == std::string::npos) {
        error = "str.format unmatched '{'";
        return false;
      }
      const auto field = format.substr(i + 1, close - i - 1);
      const auto field_name = format_field_name(field);
      const Value* replacement = nullptr;
      if (field_name.empty()) {
        if (next_arg >= argc) {
          error = "str.format replacement index out of range";
          return false;
        }
        replacement = &args[next_arg++];
      } else {
        char* end = nullptr;
        std::string field_text(field_name);
        const auto parsed = std::strtoul(field_text.c_str(), &end, 10);
        if (end != field_text.c_str() && *end == '\0') {
          const uint32_t arg_index = static_cast<uint32_t>(parsed + 1);
          if (arg_index >= argc) {
            error = "str.format replacement index out of range";
            return false;
          }
          replacement = &args[arg_index];
        } else {
          replacement = find_format_keyword(field_name, kwargs, kwargc);
          if (replacement == nullptr) {
            error = "str.format missing keyword '" + std::string(field_name) + "'";
            return false;
          }
        }
      }
      result += value_to_string(*replacement);
      i = close + 1;
      continue;
    }
    result.push_back(format[i++]);
  }
  out = Value::string(std::move(result));
  return true;
}

bool string_encode_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "str.encode expected 0 to 2 arguments, got " + std::to_string(argc - 1);
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.encode target", text, error)) {
    return false;
  }
  std::string encoding = "utf-8";
  if (argc >= 2) {
    memory::X3StringView encoding_ref;
    if (!get_string_view_checked(args[1], "str.encode encoding", encoding_ref, error)) {
      return false;
    }
    encoding = std::string(as_view(encoding_ref));
  }
  std::string errors = "strict";
  if (argc == 3) {
    memory::X3StringView errors_ref;
    if (!get_string_view_checked(args[2], "str.encode errors", errors_ref, error)) {
      return false;
    }
    errors = std::string(as_view(errors_ref));
  }
  for (auto& ch : encoding) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  encoding = canonical_encoding(std::move(encoding));
  for (auto& ch : errors) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (encoding != "ascii" && encoding != "utf_8" && encoding != "utf_8_sig" && encoding != "latin_1" && encoding != "cp437") {
    error = "only utf-8/ascii/latin-1/cp437 encoding is supported";
    return false;
  }
  if (encoding == "ascii") {
    std::string encoded;
    auto view = as_view(text);
    encoded.reserve(view.size());
    for (size_t i = 0; i < view.size();) {
      const unsigned char ch = static_cast<unsigned char>(view[i]);
      if (ch < 128) {
        encoded.push_back(static_cast<char>(ch));
        ++i;
        continue;
      }
      const size_t width = utf8_codepoint_width(ch);
      const uint32_t codepoint = width == 0 || i + width > view.size() ? ch : decode_utf8_codepoint(view.substr(i), width);
      const size_t advance = width == 0 ? 1 : width;
      if (errors == "ignore") {
        i += advance;
      } else if (errors == "replace") {
        encoded.push_back('?');
        i += advance;
      } else if (errors == "backslashreplace") {
        append_ascii_backslash_escape(codepoint, encoded);
        i += advance;
      } else {
        error = "ascii codec can't encode character";
        runtime.raise_class_error("UnicodeEncodeError", error);
        return false;
      }
    }
    out = Value::bytes(std::move(encoded));
    return true;
  }
  if (encoding == "latin_1") {
    std::string encoded = latin1_encode_text(runtime, as_view(text), errors, error);
    if (!error.empty()) {
      return false;
    }
    out = Value::bytes(std::move(encoded));
    return true;
  }
  if (encoding == "cp437") {
    std::string encoded;
    if (!cp437_encode_text(as_view(text), errors, encoded, error)) {
      runtime.raise_class_error("UnicodeEncodeError", error);
      return false;
    }
    out = Value::bytes(std::move(encoded));
    return true;
  }
  if (encoding == "utf_8_sig") {
    out = Value::bytes(std::string("\xef\xbb\xbf", 3) + std::string(as_view(text)));
    return true;
  }
  out = Value::bytes(std::string(as_view(text)));
  return true;
}

Value split_whitespace(memory::X3StringView text) {
  auto text_view = as_view(text);
  size_t count = 0;
  bool in_word = false;
  for (const unsigned char ch : text_view) {
    const bool space = string_ascii_isspace(ch);
    if (!space && !in_word) {
      ++count;
    }
    in_word = !space;
  }
  Value out = Value::list_reserved(count);
  auto* list = value_as_list(out);
  size_t i = 0;
  while (i < text_view.size()) {
    while (i < text_view.size() && string_ascii_isspace(static_cast<unsigned char>(text_view[i]))) {
      ++i;
    }
    const size_t start = i;
    while (i < text_view.size() && !string_ascii_isspace(static_cast<unsigned char>(text_view[i]))) {
      ++i;
    }
    if (i > start) {
      list->items.push_back(make_string_range_unchecked(text, start, i - start));
    }
  }
  return out;
}

bool split_separator(
    memory::X3StringView text,
    memory::X3StringView sep,
    Value& out,
    std::string& error) {
  auto text_view = as_view(text);
  auto sep_view = as_view(sep);
  if (sep_view.empty()) {
    error = "empty separator";
    return false;
  }
  if (sep_view.size() == 1) {
    const char sep_ch = sep_view[0];
    size_t count = 1;
    for (const auto ch : text_view) {
      if (ch == sep_ch) {
        ++count;
      }
    }
    Value result = Value::list_reserved(count);
    auto* list = value_as_list(result);
    size_t start = 0;
    for (size_t i = 0; i < text_view.size(); ++i) {
      if (text_view[i] == sep_ch) {
        list->items.push_back(make_string_range_unchecked(text, start, i - start));
        start = i + 1;
      }
    }
    list->items.push_back(make_string_range_unchecked(text, start, text_view.size() - start));
    value_move_assign_fast(out, result);
    return true;
  }
  Value result = Value::list_reserved(count_non_overlapping_matches(text, sep, -1) + 1);
  auto* list = value_as_list(result);
  size_t start = 0;
  while (true) {
    const size_t pos = text_view.find(sep_view, start);
    if (pos == std::string::npos) {
      list->items.push_back(make_string_range_unchecked(text, start, text_view.size() - start));
      value_move_assign_fast(out, result);
      return true;
    }
    list->items.push_back(make_string_range_unchecked(text, start, pos - start));
    start = pos + sep_view.size();
  }
}

bool string_split_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.split expected 1 or 2 arguments, got " + std::to_string(argc);
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.split target", text, error)) {
    return false;
  }
  if (argc == 1) {
    out = split_whitespace(text);
  } else {
    memory::X3StringView sep;
    if (!get_string_view_checked(args[1], "str.split separator", sep, error)) {
      return false;
    }
    if (!split_separator(text, sep, out, error)) {
      return false;
    }
  }
  return true;
}

bool string_split_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count > 1 || leading == nullptr ||
      (register_arg_count != 0 && (registers == nullptr || register_args == nullptr))) {
    error = "str.split expected 0 or 1 arguments";
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(leading[0], "str.split target", text, error)) {
    return false;
  }
  if (register_arg_count == 0) {
    out = split_whitespace(text);
  } else {
    memory::X3StringView sep;
    if (!get_string_view_checked(registers[register_args[0]], "str.split separator", sep, error)) {
      return false;
    }
    if (!split_separator(text, sep, out, error)) {
      return false;
    }
  }
  return true;
}

bool string_partition_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "str.partition expected separator";
    return false;
  }
  memory::X3StringView text;
  memory::X3StringView sep;
  if (!get_string_view_checked(args[0], "str.partition target", text, error) ||
      !get_string_view_checked(args[1], "str.partition separator", sep, error)) {
    return false;
  }
  if (sep.size == 0) {
    error = "empty separator";
    return false;
  }
  const std::string_view text_view = as_view(text);
  const std::string_view sep_view = as_view(sep);
  const size_t pos = text_view.find(sep_view);
  if (pos == std::string_view::npos) {
    out = Value::tuple({make_string_from_view(text), Value::string(""), Value::string("")});
    return true;
  }
  out = Value::tuple({
      make_string_range_unchecked(text, 0, pos),
      make_string_from_view(sep),
      make_string_range_unchecked(text, pos + sep.size, text.size - pos - sep.size),
  });
  return true;
}

bool string_rpartition_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "str.rpartition expected separator";
    return false;
  }
  memory::X3StringView text;
  memory::X3StringView sep;
  if (!get_string_view_checked(args[0], "str.rpartition target", text, error) ||
      !get_string_view_checked(args[1], "str.rpartition separator", sep, error)) {
    return false;
  }
  if (sep.size == 0) {
    error = "empty separator";
    return false;
  }
  const std::string_view text_view = as_view(text);
  const std::string_view sep_view = as_view(sep);
  const size_t pos = text_view.rfind(sep_view);
  if (pos == std::string_view::npos) {
    out = Value::tuple({Value::string(""), Value::string(""), make_string_from_view(text)});
    return true;
  }
  out = Value::tuple({
      make_string_range_unchecked(text, 0, pos),
      make_string_from_view(sep),
      make_string_range_unchecked(text, pos + sep.size, text.size - pos - sep.size),
  });
  return true;
}

enum class StringCharClassKind {
  Lower,
  Upper,
  Alpha,
  Digit,
  Alnum,
  Space,
};

bool string_char_class_method(
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    const char* name,
    StringCharClassKind kind) {
  if (argc != 1) {
    error = std::string(name) + " expected no arguments";
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], name, text, error)) {
    return false;
  }
  if (text.size == 0) {
    value_set_bool(out, false);
    return true;
  }

  bool has_cased = false;
  bool result = true;
  const std::string_view view = as_view(text);
  for (unsigned char ch : view) {
    switch (kind) {
      case StringCharClassKind::Lower:
        if (std::isalpha(ch)) {
          has_cased = true;
          if (!std::islower(ch)) result = false;
        }
        break;
      case StringCharClassKind::Upper:
        if (std::isalpha(ch)) {
          has_cased = true;
          if (!std::isupper(ch)) result = false;
        }
        break;
      case StringCharClassKind::Alpha:
        if (!std::isalpha(ch)) result = false;
        break;
      case StringCharClassKind::Digit:
        if (!std::isdigit(ch)) result = false;
        break;
      case StringCharClassKind::Alnum:
        if (!std::isalnum(ch)) result = false;
        break;
      case StringCharClassKind::Space:
        if (!string_ascii_isspace(ch)) result = false;
        break;
    }
    if (!result) {
      break;
    }
  }

  if (kind == StringCharClassKind::Lower || kind == StringCharClassKind::Upper) {
    result = result && has_cased;
  }
  value_set_bool(out, result);
  return true;
}

bool string_islower_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return string_char_class_method(args, argc, out, error, "str.islower", StringCharClassKind::Lower);
}

bool string_isupper_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return string_char_class_method(args, argc, out, error, "str.isupper", StringCharClassKind::Upper);
}

bool string_isalpha_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return string_char_class_method(args, argc, out, error, "str.isalpha", StringCharClassKind::Alpha);
}

bool string_isdigit_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return string_char_class_method(args, argc, out, error, "str.isdigit", StringCharClassKind::Digit);
}

bool string_isalnum_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return string_char_class_method(args, argc, out, error, "str.isalnum", StringCharClassKind::Alnum);
}

bool string_isspace_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return string_char_class_method(args, argc, out, error, "str.isspace", StringCharClassKind::Space);
}

bool string_isascii_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.isascii", error)) {
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.isascii target", text, error)) {
    return false;
  }
  bool ok = true;
  for (unsigned char ch : as_view(text)) {
    if (ch >= 128) {
      ok = false;
      break;
    }
  }
  value_set_bool(out, ok);
  return true;
}

bool string_isidentifier_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.isidentifier", error)) {
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.isidentifier target", text, error)) {
    return false;
  }
  if (text.size == 0) {
    value_set_bool(out, false);
    return true;
  }
  auto is_start = [](unsigned char ch) {
    return ch == '_' || std::isalpha(ch) != 0 || ch >= 0x80;
  };
  auto is_continue = [&](unsigned char ch) {
    return is_start(ch) || std::isdigit(ch) != 0;
  };
  if (!is_start(static_cast<unsigned char>(text.data[0]))) {
    value_set_bool(out, false);
    return true;
  }
  for (size_t i = 1; i < text.size; ++i) {
    if (!is_continue(static_cast<unsigned char>(text.data[i]))) {
      value_set_bool(out, false);
      return true;
    }
  }
  value_set_bool(out, true);
  return true;
}

bool string_isdecimal_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return string_char_class_method(args, argc, out, error, "str.isdecimal", StringCharClassKind::Digit);
}

bool string_isnumeric_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return string_char_class_method(args, argc, out, error, "str.isnumeric", StringCharClassKind::Digit);
}

bool string_casefold_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.casefold", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return string_lower_body(args[0], out, error);
}

bool string_capitalize_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.capitalize", error)) {
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.capitalize target", text, error)) {
    return false;
  }
  auto view = as_view(text);
  char* result = nullptr;
  Value result_value = make_uninitialized_string_value(view.size(), result);
  if (result == nullptr) {
    return false;
  }
  for (size_t i = 0; i < view.size(); ++i) {
    const auto ch = static_cast<unsigned char>(view[i]);
    result[i] = static_cast<char>(i == 0 ? std::toupper(ch) : std::tolower(ch));
  }
  return publish_string_result(out, result_value);
}

bool string_swapcase_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.swapcase", error)) {
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.swapcase target", text, error)) {
    return false;
  }
  auto view = as_view(text);
  char* result = nullptr;
  Value result_value = make_uninitialized_string_value(view.size(), result);
  if (result == nullptr) {
    return false;
  }
  for (size_t i = 0; i < view.size(); ++i) {
    const auto ch = static_cast<unsigned char>(view[i]);
    result[i] = static_cast<char>(std::islower(ch) ? std::toupper(ch) : std::tolower(ch));
  }
  return publish_string_result(out, result_value);
}

bool string_title_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.title", error)) {
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.title target", text, error)) {
    return false;
  }
  auto view = as_view(text);
  char* result = nullptr;
  Value result_value = make_uninitialized_string_value(view.size(), result);
  if (result == nullptr) {
    return false;
  }
  bool new_word = true;
  for (size_t i = 0; i < view.size(); ++i) {
    const auto ch = static_cast<unsigned char>(view[i]);
    result[i] = static_cast<char>(new_word ? std::toupper(ch) : std::tolower(ch));
    new_word = std::isalnum(ch) == 0;
  }
  return publish_string_result(out, result_value);
}

bool string_istitle_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "str.istitle", error)) {
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.istitle target", text, error)) {
    return false;
  }
  bool new_word = true;
  bool seen_cased = false;
  bool ok = true;
  for (unsigned char ch : as_view(text)) {
    if (std::isalpha(ch)) {
      seen_cased = true;
      if (new_word) {
        if (!std::isupper(ch)) {
          ok = false;
          break;
        }
      } else if (!std::islower(ch)) {
        ok = false;
        break;
      }
      new_word = false;
    } else {
      new_word = std::isalnum(ch) == 0;
    }
  }
  value_set_bool(out, ok && seen_cased);
  return true;
}

bool parse_fill_width_args(
    const Value* args,
    uint32_t argc,
    const char* name,
    memory::X3StringView& text,
    int64_t& width,
    char& fill,
    std::string& error) {
  if (argc < 2 || argc > 3) {
    error = std::string(name) + " expected width and optional fillchar";
    return false;
  }
  if (!get_string_view_checked(args[0], name, text, error)) {
    return false;
  }
  if (args[1].tag != ValueTag::Int64) {
    error = std::string(name) + " width must be int";
    return false;
  }
  width = args[1].as.i64;
  fill = ' ';
  if (argc == 3) {
    memory::X3StringView fill_text;
    if (!get_string_view_checked(args[2], "fillchar", fill_text, error)) {
      return false;
    }
    if (fill_text.size != 1) {
      error = "fill character must be exactly one character long";
      return false;
    }
    fill = fill_text.data[0];
  }
  return true;
}

bool make_padded_string(std::string_view text, int64_t width, char fill, size_t left_pad, Value& out) {
  if (width <= static_cast<int64_t>(text.size())) {
    out = Value::string_view(text);
    return true;
  }
  const size_t total = static_cast<size_t>(width);
  std::string result(total, fill);
  std::memcpy(result.data() + left_pad, text.data(), text.size());
  out = Value::string(std::move(result));
  return true;
}

bool string_center_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  memory::X3StringView text;
  int64_t width = 0;
  char fill = ' ';
  if (!parse_fill_width_args(args, argc, "str.center", text, width, fill, error)) {
    return false;
  }
  auto view = as_view(text);
  const size_t pad = width > static_cast<int64_t>(view.size()) ? static_cast<size_t>(width - view.size()) : 0;
  return make_padded_string(view, width, fill, pad / 2, out);
}

bool string_ljust_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  memory::X3StringView text;
  int64_t width = 0;
  char fill = ' ';
  if (!parse_fill_width_args(args, argc, "str.ljust", text, width, fill, error)) {
    return false;
  }
  return make_padded_string(as_view(text), width, fill, 0, out);
}

bool string_rjust_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  memory::X3StringView text;
  int64_t width = 0;
  char fill = ' ';
  if (!parse_fill_width_args(args, argc, "str.rjust", text, width, fill, error)) {
    return false;
  }
  auto view = as_view(text);
  const size_t pad = width > static_cast<int64_t>(view.size()) ? static_cast<size_t>(width - view.size()) : 0;
  return make_padded_string(view, width, fill, pad, out);
}

bool string_zfill_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.zfill", error)) {
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.zfill target", text, error)) {
    return false;
  }
  if (args[1].tag != ValueTag::Int64) {
    error = "str.zfill width must be int";
    return false;
  }
  auto view = as_view(text);
  const int64_t width = args[1].as.i64;
  if (width <= static_cast<int64_t>(view.size())) {
    out = Value::string_view(view);
    return true;
  }
  const size_t total = static_cast<size_t>(width);
  std::string result(total, '0');
  size_t source = 0;
  size_t dest = 0;
  if (!view.empty() && (view[0] == '+' || view[0] == '-')) {
    result[0] = view[0];
    source = 1;
    dest = 1;
  }
  std::memcpy(result.data() + (total - (view.size() - source)), view.data() + source, view.size() - source);
  (void)dest;
  out = Value::string(std::move(result));
  return true;
}

bool string_removeprefix_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.removeprefix", error)) {
    return false;
  }
  memory::X3StringView text;
  memory::X3StringView prefix;
  if (!get_string_view_checked(args[0], "str.removeprefix target", text, error) ||
      !get_string_view_checked(args[1], "str.removeprefix prefix", prefix, error)) {
    return false;
  }
  auto t = as_view(text);
  auto p = as_view(prefix);
  if (t.size() >= p.size() && t.substr(0, p.size()) == p) {
    out = Value::string_view(t.substr(p.size()));
  } else {
    value_assign_fast(out, args[0]);
  }
  return true;
}

bool string_removesuffix_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.removesuffix", error)) {
    return false;
  }
  memory::X3StringView text;
  memory::X3StringView suffix;
  if (!get_string_view_checked(args[0], "str.removesuffix target", text, error) ||
      !get_string_view_checked(args[1], "str.removesuffix suffix", suffix, error)) {
    return false;
  }
  auto t = as_view(text);
  auto s = as_view(suffix);
  if (!s.empty() && t.size() >= s.size() && t.substr(t.size() - s.size()) == s) {
    out = Value::string_view(t.substr(0, t.size() - s.size()));
  } else {
    value_assign_fast(out, args[0]);
  }
  return true;
}

bool string_splitlines_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "str.splitlines expected optional keepends";
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.splitlines target", text, error)) {
    return false;
  }
  const bool keepends = argc == 2 && value_truthy(args[1]);
  std::vector<Value> lines;
  auto view = as_view(text);
  size_t start = 0;
  for (size_t i = 0; i < view.size(); ++i) {
    if (view[i] != '\n' && view[i] != '\r') {
      continue;
    }
    size_t end = i;
    size_t next = i + 1;
    if (view[i] == '\r' && next < view.size() && view[next] == '\n') {
      ++next;
    }
    if (keepends) {
      end = next;
    }
    lines.push_back(Value::string_view(view.substr(start, end - start)));
    i = next - 1;
    start = next;
  }
  if (start < view.size()) {
    lines.push_back(Value::string_view(view.substr(start)));
  }
  out = Value::list(std::move(lines));
  return true;
}

bool string_rsplit_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  // The current split implementation has no maxsplit path yet. For the common
  // one-argument/two-argument forms, right-split is equivalent to split.
  return string_split_method(runtime, args, argc, out, error, user_data);
}

bool string_expandtabs_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "str.expandtabs expected optional tabsize";
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.expandtabs target", text, error)) {
    return false;
  }
  int64_t tabsize = 8;
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      error = "str.expandtabs tabsize must be int";
      return false;
    }
    tabsize = args[1].as.i64;
  }
  std::string result;
  size_t column = 0;
  for (char ch : as_view(text)) {
    if (ch == '\t') {
      const size_t spaces = tabsize <= 0 ? 0 : static_cast<size_t>(tabsize) - (column % static_cast<size_t>(tabsize));
      result.append(spaces, ' ');
      column += spaces;
    } else {
      result.push_back(ch);
      column = (ch == '\n' || ch == '\r') ? 0 : column + 1;
    }
  }
  out = Value::string(std::move(result));
  return true;
}

} // namespace

static constexpr BuiltinMethodSpec kStringMethods[] = {
    {"capitalize", "str.capitalize", string_capitalize_method},
    {"casefold", "str.casefold", string_casefold_method},
    {"center", "str.center", string_center_method},
    {"count", "str.count", string_count_method, string_count_fast_method},
    {"encode", "str.encode", string_encode_method},
    {"endswith", "str.endswith", string_endswith_method, string_endswith_fast_method},
    {"expandtabs", "str.expandtabs", string_expandtabs_method},
    {"find", "str.find", string_find_method, string_find_fast_method},
    {"format", "str.format", string_format_method, nullptr, false, string_format_method_kw},
    {"index", "str.index", string_index_method},
    {"isalnum", "str.isalnum", string_isalnum_method},
    {"isalpha", "str.isalpha", string_isalpha_method},
    {"isascii", "str.isascii", string_isascii_method},
    {"isdecimal", "str.isdecimal", string_isdecimal_method},
    {"isdigit", "str.isdigit", string_isdigit_method},
    {"isidentifier", "str.isidentifier", string_isidentifier_method},
    {"islower", "str.islower", string_islower_method},
    {"isnumeric", "str.isnumeric", string_isnumeric_method},
    {"isspace", "str.isspace", string_isspace_method},
    {"istitle", "str.istitle", string_istitle_method},
    {"isupper", "str.isupper", string_isupper_method},
    {"join", "str.join", string_join_method, string_join_fast_method},
    {"ljust", "str.ljust", string_ljust_method},
    {"lower", "str.lower", string_lower_method, string_lower_fast_method},
    {"lstrip", "str.lstrip", string_lstrip_method, string_lstrip_fast_method},
    {"maketrans", "str.maketrans", string_maketrans_method},
    {"translate", "str.translate", string_translate_method},
    {"partition", "str.partition", string_partition_method},
    {"removeprefix", "str.removeprefix", string_removeprefix_method},
    {"removesuffix", "str.removesuffix", string_removesuffix_method},
    {"replace", "str.replace", string_replace_method, string_replace_fast_method},
    {"rfind", "str.rfind", string_rfind_method},
    {"rindex", "str.rindex", string_rindex_method},
    {"rjust", "str.rjust", string_rjust_method},
    {"rpartition", "str.rpartition", string_rpartition_method},
    {"rsplit", "str.rsplit", string_rsplit_method},
    {"rstrip", "str.rstrip", string_rstrip_method, string_rstrip_fast_method},
    {"split", "str.split", string_split_method, string_split_fast_method},
    {"splitlines", "str.splitlines", string_splitlines_method},
    {"startswith", "str.startswith", string_startswith_method, string_startswith_fast_method},
    {"strip", "str.strip", string_strip_method, string_strip_fast_method},
    {"swapcase", "str.swapcase", string_swapcase_method},
    {"title", "str.title", string_title_method},
    {"upper", "str.upper", string_upper_method, string_upper_fast_method},
    {"zfill", "str.zfill", string_zfill_method},
};

const BuiltinMethodSpec* find_string_method_spec(const std::string& name) {
  for (const auto& method : kStringMethods) {
    if (name == method.name) {
      return &method;
    }
  }
  return nullptr;
}

bool string_install_class_methods(Runtime& runtime, ClassObject& string_class) {
  for (const auto& method : kStringMethods) {
    string_class.attrs[method.name] = runtime.make_native_function(
        method.full_name,
        method.callback,
        nullptr,
        nullptr,
        method.fast_callback,
        method.fast_releases_vm_lock,
        method.keyword_callback);
  }
  ++string_class.version;
  return true;
}

const BuiltinMethodSpec* string_find_method_spec(const Value& object, const std::string& name) {
  if (object.tag != ValueTag::Object || object.as.obj == nullptr || object.as.obj->kind != ObjectKind::String) {
    return nullptr;
  }
  return find_string_method_spec(name);
}

bool string_get_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag != ValueTag::Object || object.as.obj == nullptr || object.as.obj->kind != ObjectKind::String) {
    return false;
  }
  return bind_builtin_method_from_table(object, name, kStringMethods, std::size(kStringMethods), out);
}

} // namespace xlang3
