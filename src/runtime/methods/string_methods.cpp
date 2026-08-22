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

#include "runtime/memory/x3_string_ref.h"

#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
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

bool collect_join_iterable(const Value& iterable, std::vector<Value>& items, std::string& error) {
  Value iterator;
  if (!sequence_get_iter(iterable, iterator, error)) {
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

bool string_strip_body(const Value& value, Value& out, std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.strip target", text, error)) {
    return false;
  }
  const auto trimmed = memory::x3_trim_ascii(text);
  if (trimmed.data == text.data && trimmed.size == text.size) {
    value_assign_fast(out, value);
    return true;
  }
  out = make_string_from_view(trimmed);
  return true;
}

bool string_startswith_body(const Value& value, const Value& prefix_value, Value& out, std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.startswith target", text, error)) {
    return false;
  }
  memory::X3StringView prefix;
  if (!get_string_view_checked(prefix_value, "str.startswith prefix", prefix, error)) {
    return false;
  }
  value_set_bool(out, prefix.size <= text.size &&
                          (prefix.size == 0 || std::memcmp(text.data, prefix.data, prefix.size) == 0));
  return true;
}

bool string_endswith_body(const Value& value, const Value& suffix_value, Value& out, std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.endswith target", text, error)) {
    return false;
  }
  memory::X3StringView suffix;
  if (!get_string_view_checked(suffix_value, "str.endswith suffix", suffix, error)) {
    return false;
  }
  value_set_bool(out, suffix.size <= text.size &&
                          (suffix.size == 0 ||
                           std::memcmp(text.data + (text.size - suffix.size), suffix.data, suffix.size) == 0));
  return true;
}

bool string_find_body(const Value& value, const Value& needle_value, Value& out, std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.find target", text, error)) {
    return false;
  }
  memory::X3StringView needle;
  if (!get_string_view_checked(needle_value, "str.find substring", needle, error)) {
    return false;
  }
  if (needle.size == 1) {
    const void* pos = std::memchr(text.data, static_cast<unsigned char>(needle.data[0]), text.size);
    value_set_int64(out, pos == nullptr ? -1 : static_cast<int64_t>(static_cast<const char*>(pos) - text.data));
    return true;
  }
  const auto pos = as_view(text).find(as_view(needle));
  value_set_int64(out, pos == std::string::npos ? -1 : static_cast<int64_t>(pos));
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
  if (!method_check_argc(argc, 1, "str.strip", error)) {
    return false;
  }
  return string_strip_body(args[0], out, error);
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
    const Value*,
    const uint32_t*,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count != 0 || leading == nullptr) {
    error = "str.strip expected no arguments";
    return false;
  }
  return string_strip_body(leading[0], out, error);
}

bool string_startswith_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.startswith", error)) {
    return false;
  }
  return string_startswith_body(args[0], args[1], out, error);
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
  if (leading_count != 1 || register_arg_count != 1 || leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.startswith expected 1 argument";
    return false;
  }
  return string_startswith_body(leading[0], registers[register_args[0]], out, error);
}

bool string_endswith_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.endswith", error)) {
    return false;
  }
  return string_endswith_body(args[0], args[1], out, error);
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
  if (leading_count != 1 || register_arg_count != 1 || leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.endswith expected 1 argument";
    return false;
  }
  return string_endswith_body(leading[0], registers[register_args[0]], out, error);
}

bool string_find_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.find", error)) {
    return false;
  }
  return string_find_body(args[0], args[1], out, error);
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
  if (leading_count != 1 || register_arg_count != 1 || leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.find expected 1 argument";
    return false;
  }
  return string_find_body(leading[0], registers[register_args[0]], out, error);
}

bool string_count_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.count", error)) {
    return false;
  }
  return string_count_body(args[0], args[1], out, error);
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

bool string_join_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
  if (!collect_join_iterable(args[1], items, error)) {
    error = "str.join argument must be iterable";
    return false;
  }
  return join_string_values(sep, items, out, error);
}

bool string_join_fast_method(
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
  if (!collect_join_iterable(sequence, items, error)) {
    error = "str.join argument must be iterable";
    return false;
  }
  return join_string_values(sep, items, out, error);
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

bool string_encode_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.encode expected 1 or 2 arguments, got " + std::to_string(argc);
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.encode target", text, error)) {
    return false;
  }
  if (argc == 2) {
    memory::X3StringView encoding_ref;
    if (!get_string_view_checked(args[1], "str.encode encoding", encoding_ref, error)) {
      return false;
    }
    std::string encoding(as_view(encoding_ref));
    for (auto& ch : encoding) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (encoding != "utf-8" && encoding != "utf8") {
      error = "only utf-8 encoding is supported";
      return false;
    }
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

} // namespace

static constexpr BuiltinMethodSpec kStringMethods[] = {
    {"count", "str.count", string_count_method, string_count_fast_method},
    {"encode", "str.encode", string_encode_method, builtin_method_fast_adapter<string_encode_method, 2>},
    {"endswith", "str.endswith", string_endswith_method, string_endswith_fast_method},
    {"find", "str.find", string_find_method, string_find_fast_method},
    {"format", "str.format", string_format_method},
    {"join", "str.join", string_join_method, string_join_fast_method},
    {"lower", "str.lower", string_lower_method, string_lower_fast_method},
    {"replace", "str.replace", string_replace_method, string_replace_fast_method},
    {"split", "str.split", string_split_method, string_split_fast_method},
    {"startswith", "str.startswith", string_startswith_method, string_startswith_fast_method},
    {"strip", "str.strip", string_strip_method, string_strip_fast_method},
    {"upper", "str.upper", string_upper_method, string_upper_fast_method},
};

const BuiltinMethodSpec* find_string_method_spec(const std::string& name) {
  for (const auto& method : kStringMethods) {
    if (name == method.name) {
      return &method;
    }
  }
  return nullptr;
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
