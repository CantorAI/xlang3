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
#include "xlang3/builtins.h"
#include "xlang3/cp437_codec.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include "runtime/memory/x3_string_ref.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace xlang3 {

namespace {

bool get_string_view_checked(
    const Value& value,
    const char* name,
    memory::X3StringView& out,
    std::string& error);
Value make_string_from_view(memory::X3StringView text);

bool string_str_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "str.__str__ expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.__str__ target", text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = make_string_from_view(text);
  return true;
}

bool string_repr_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "str.__repr__ expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.__repr__ target", text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::string(value_to_repr(make_string_from_view(text)));
  return true;
}

bool string_getitem_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "str.__getitem__ expected one index";
    return false;
  }
  Value target = args[0];
  if (value_as_instance(target) != nullptr) {
    Value payload;
    std::string ignored;
    if (object_get_attr(target, "__xlang3_string_value__", payload, ignored)) {
      target = std::move(payload);
    }
  }
  if (sequence_get_item(target, args[1], out, error)) {
    return true;
  }
  runtime.raise_class_error(error == "index out of range" ? "IndexError" : "TypeError", error);
  return false;
}

memory::X3StringView get_string_view(const Value& value, const char* name, std::string& error) {
  if (auto* text = value_as_string(value)) {
    return memory::x3_string_view(string_object_view(*text));
  }
  if (value_as_instance(value) != nullptr) {
    Value payload;
    std::string ignored;
    if (object_get_attr(value, "__xlang3_string_value__", payload, ignored)) {
      if (auto* text = value_as_string(payload)) {
        return memory::x3_string_view(string_object_view(*text));
      }
    }
  }
  error = std::string(name) + " must be a string";
  return {};
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

bool unicode_identifier_codepoint(uint32_t codepoint, bool first) {
  if (codepoint == '_') return true;
  if (codepoint < 0x80u) {
    return std::isalpha(static_cast<unsigned char>(codepoint)) != 0 ||
        (!first && std::isdigit(static_cast<unsigned char>(codepoint)) != 0);
  }
  if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) return false;
#ifdef _WIN32
  wchar_t units[2]{};
  int count = 1;
  if (codepoint <= 0xffffu) {
    units[0] = static_cast<wchar_t>(codepoint);
  } else {
    const uint32_t adjusted = codepoint - 0x10000u;
    units[0] = static_cast<wchar_t>(0xd800u + (adjusted >> 10u));
    units[1] = static_cast<wchar_t>(0xdc00u + (adjusted & 0x3ffu));
    count = 2;
  }
  WORD type1[2]{};
  WORD type3[2]{};
  if (GetStringTypeW(CT_CTYPE1, units, count, type1) == 0 ||
      GetStringTypeW(CT_CTYPE3, units, count, type3) == 0) {
    return false;
  }
  const WORD combined_type1 = static_cast<WORD>(type1[0] | type1[1]);
  const WORD combined_type3 = static_cast<WORD>(type3[0] | type3[1]);
  const bool alphabetic = (combined_type1 & C1_ALPHA) != 0 ||
      (codepoint >= 0x1d400u && codepoint <= 0x1d7cbu);
  if (first) {
    return alphabetic || codepoint == 0x1885u || codepoint == 0x1886u ||
        codepoint == 0x2118u || codepoint == 0x212eu ||
        codepoint == 0x309bu || codepoint == 0x309cu;
  }
  return alphabetic || (combined_type1 & C1_DIGIT) != 0 ||
      (combined_type3 & (C3_NONSPACING | C3_DIACRITIC)) != 0 ||
      codepoint == 0x00b7u || codepoint == 0x0387u ||
      (codepoint >= 0x1369u && codepoint <= 0x1371u) || codepoint == 0x19dau;
#else
  return false;
#endif
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
  if (name == "locale" || name == "mbcs" || name == "ansi") {
    return "mbcs";
  }
  return name;
}

void append_encoded_unit(std::string& out, uint32_t value, size_t width,
                         bool little_endian) {
  if (little_endian) {
    for (size_t index = 0; index < width; ++index) {
      out.push_back(static_cast<char>((value >> (index * 8)) & 0xff));
    }
  } else {
    for (size_t index = width; index > 0; --index) {
      out.push_back(static_cast<char>((value >> ((index - 1) * 8)) & 0xff));
    }
  }
}

bool encode_utf16_or_utf32(std::string_view text, const std::string& encoding,
                           std::string& encoded, std::string& error) {
  const bool utf32 = encoding.rfind("utf_32", 0) == 0;
  const bool little_endian = encoding != "utf_16_be" && encoding != "utf_32_be";
  encoded.clear();
  if (encoding == "utf_16") {
    encoded.append("\xff\xfe", 2);
  } else if (encoding == "utf_32") {
    encoded.append("\xff\xfe\x00\x00", 4);
  }
  for (size_t offset = 0; offset < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[offset]);
    const size_t width = utf8_codepoint_width(lead);
    if (width == 0 || offset + width > text.size()) {
      error = "invalid UTF-8 string storage";
      return false;
    }
    const uint32_t codepoint = decode_utf8_codepoint(text.substr(offset), width);
    offset += width;
    if (codepoint > 0x10ffff) {
      error = "code point not in range(0x110000)";
      return false;
    }
    if (utf32) {
      append_encoded_unit(encoded, codepoint, 4, little_endian);
    } else if (codepoint <= 0xffff) {
      append_encoded_unit(encoded, codepoint, 2, little_endian);
    } else {
      const uint32_t adjusted = codepoint - 0x10000;
      append_encoded_unit(encoded, 0xd800 + (adjusted >> 10), 2, little_endian);
      append_encoded_unit(encoded, 0xdc00 + (adjusted & 0x3ff), 2, little_endian);
    }
  }
  return true;
}

#if defined(_WIN32)
bool mbcs_encode_text(
    std::string_view text,
    const std::string& errors,
    std::string& encoded,
    std::string& error) {
  encoded.clear();
  encoded.reserve(text.size());
  for (size_t index = 0; index < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    const size_t width = utf8_codepoint_width(lead);
    const bool valid_width = width != 0 && index + width <= text.size();
    const uint32_t codepoint = valid_width
        ? decode_utf8_codepoint(text.substr(index), width)
        : lead;
    const size_t advance = valid_width ? width : 1;
    if (errors == "surrogateescape" && codepoint >= 0xdc80u && codepoint <= 0xdcffu) {
      encoded.push_back(static_cast<char>(codepoint - 0xdc00u));
      index += advance;
      continue;
    }
    wchar_t wide[2]{};
    int wide_count = 0;
    if (codepoint <= 0xffffu && !(codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
      wide[0] = static_cast<wchar_t>(codepoint);
      wide_count = 1;
    } else if (codepoint >= 0x10000u && codepoint <= 0x10ffffu) {
      const uint32_t adjusted = codepoint - 0x10000u;
      wide[0] = static_cast<wchar_t>(0xd800u + (adjusted >> 10u));
      wide[1] = static_cast<wchar_t>(0xdc00u + (adjusted & 0x3ffu));
      wide_count = 2;
    }
    BOOL used_default = FALSE;
    char buffer[16]{};
    const int count = wide_count == 0 ? 0 : WideCharToMultiByte(
        CP_ACP,
        WC_NO_BEST_FIT_CHARS,
        wide,
        wide_count,
        buffer,
        static_cast<int>(sizeof(buffer)),
        nullptr,
        &used_default);
    if (count <= 0 || used_default) {
      if (errors == "ignore") {
        index += advance;
        continue;
      }
      if (errors == "replace") {
        encoded.push_back('?');
        index += advance;
        continue;
      }
      error = "mbcs codec can't encode character";
      return false;
    }
    encoded.append(buffer, static_cast<size_t>(count));
    index += advance;
  }
  return true;
}
#endif

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
    } else if (errors == "xmlcharrefreplace") {
      encoded += "&#" + std::to_string(codepoint) + ";";
      i += advance;
    } else if (errors == "surrogateescape" && codepoint >= 0xdc80u && codepoint <= 0xdcffu) {
      encoded.push_back(static_cast<char>(codepoint - 0xdc00u));
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
#ifdef _WIN32
  if (std::any_of(view.begin(), view.end(), [](char ch) {
        return static_cast<unsigned char>(ch) >= 0x80u;
      })) {
    const int wide_size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, view.data(), static_cast<int>(view.size()), nullptr, 0);
    if (wide_size > 0) {
      std::wstring wide(static_cast<size_t>(wide_size), L'\0');
      if (MultiByteToWideChar(
              CP_UTF8, MB_ERR_INVALID_CHARS, view.data(), static_cast<int>(view.size()),
              wide.data(), wide_size) == wide_size) {
        const DWORD flags = upper ? LCMAP_UPPERCASE : LCMAP_LOWERCASE;
        std::wstring mapped;
        mapped.reserve(wide.size());
        bool mapping_ok = true;
        for (size_t i = 0; i < wide.size();) {
          const size_t source_count =
              wide[i] >= 0xd800 && wide[i] <= 0xdbff && i + 1 < wide.size() &&
                  wide[i + 1] >= 0xdc00 && wide[i + 1] <= 0xdfff
              ? 2u : 1u;
          const wchar_t code_unit = wide[i];
          if (!upper && code_unit == 0x212a) {
            mapped.push_back(L'k');
          } else if (!upper && code_unit == 0x0130) {
            mapped.push_back(L'i');
            mapped.push_back(static_cast<wchar_t>(0x0307));
          } else if (upper && code_unit == 0x017f) {
            mapped.push_back(L'S');
          } else if (upper && code_unit == 0x1c80) {
            mapped.push_back(static_cast<wchar_t>(0x0412));
          } else if (upper && (code_unit == 0xfb05 || code_unit == 0xfb06)) {
            mapped += L"ST";
          } else {
            wchar_t piece[4]{};
            const int piece_size = LCMapStringEx(
                LOCALE_NAME_INVARIANT, flags, wide.data() + i,
                static_cast<int>(source_count), piece, static_cast<int>(std::size(piece)),
                nullptr, nullptr, 0);
            if (piece_size <= 0) {
              mapping_ok = false;
              break;
            }
            mapped.append(piece, piece + piece_size);
          }
          i += source_count;
        }
        if (mapping_ok) {
          const int mapped_size = static_cast<int>(mapped.size());
            const int utf8_size = WideCharToMultiByte(
                CP_UTF8, WC_ERR_INVALID_CHARS, mapped.data(), mapped_size,
                nullptr, 0, nullptr, nullptr);
            if (utf8_size > 0) {
              std::string utf8(static_cast<size_t>(utf8_size), '\0');
              if (WideCharToMultiByte(
                      CP_UTF8, WC_ERR_INVALID_CHARS, mapped.data(), mapped_size,
                      utf8.data(), utf8_size, nullptr, nullptr) == utf8_size) {
                out = Value::string(std::move(utf8));
                return true;
              }
            }
        }
      }
    }
  }
#endif
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
  const auto view = as_view(text);
  const int64_t length = static_cast<int64_t>(utf8_codepoint_count(view));
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
  start = utf8_byte_offset(view, static_cast<size_t>(start_i));
  end = utf8_byte_offset(view, static_cast<size_t>(end_i));
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
    const size_t byte_pos = pos == nullptr
        ? std::string::npos
        : start + static_cast<size_t>(static_cast<const char*>(pos) - span.data);
    value_set_int64(
        out, byte_pos == std::string::npos
            ? -1
            : static_cast<int64_t>(utf8_codepoint_count(as_view(text).substr(0, byte_pos))));
    return true;
  }
  const auto pos = as_view(span).find(as_view(needle));
  const size_t byte_pos = pos == std::string::npos ? std::string::npos : start + pos;
  value_set_int64(
      out, byte_pos == std::string::npos
          ? -1
          : static_cast<int64_t>(utf8_codepoint_count(as_view(text).substr(0, byte_pos))));
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
  const size_t byte_pos = pos == std::string::npos ? std::string::npos : start + pos;
  value_set_int64(
      out, byte_pos == std::string::npos
          ? -1
          : static_cast<int64_t>(utf8_codepoint_count(as_view(text).substr(0, byte_pos))));
  return true;
}

bool string_count_body(
    const Value& value,
    const Value& needle_value,
    const Value* start_value,
    const Value* end_value,
    Value& out,
    std::string& error) {
  memory::X3StringView text;
  if (!get_string_view_checked(value, "str.count target", text, error)) {
    return false;
  }
  memory::X3StringView needle;
  if (!get_string_view_checked(needle_value, "str.count substring", needle, error)) {
    return false;
  }
  size_t start_bound = 0;
  size_t end_bound = text.size;
  if (!string_bounds_from_args(text, start_value, end_value, start_bound, end_bound, error)) {
    return false;
  }
  auto text_view = as_view(text).substr(start_bound, end_bound - start_bound);
  auto needle_view = as_view(needle);
  if (needle_view.empty()) {
    value_set_int64(out, static_cast<int64_t>(text_view.size() + 1));
    return true;
  }
  if (needle.size == 1) {
    int64_t count = 0;
    const char needle_ch = needle.data[0];
    for (const char ch : text_view) {
      if (ch == needle_ch) {
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

bool string_strip_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.strip expected 0 or 1 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!string_strip_body(args[0], argc == 2 ? &args[1] : nullptr, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool string_rstrip_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.rstrip expected 0 or 1 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!string_rstrip_body(args[0], argc == 2 ? &args[1] : nullptr, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool string_lstrip_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "str.lstrip expected 0 or 1 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!string_lstrip_body(args[0], argc == 2 ? &args[1] : nullptr, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool string_upper_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  const Value* target = nullptr;
  if (leading_count == 1 && register_arg_count == 0 && leading != nullptr) {
    target = &leading[0];
  } else if (leading_count == 0 && register_arg_count == 1 &&
             registers != nullptr && register_args != nullptr) {
    target = &registers[register_args[0]];
  }
  if (target == nullptr) {
    error = "str.upper expected no arguments";
    return false;
  }
  return string_upper_body(*target, out, error);
}

bool string_lower_fast_method(
    Runtime&,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  const Value* target = nullptr;
  if (leading_count == 1 && register_arg_count == 0 && leading != nullptr) {
    target = &leading[0];
  } else if (leading_count == 0 && register_arg_count == 1 &&
             registers != nullptr && register_args != nullptr) {
    target = &registers[register_args[0]];
  }
  if (target == nullptr) {
    error = "str.lower expected no arguments";
    return false;
  }
  return string_lower_body(*target, out, error);
}

bool string_strip_fast_method(
    Runtime& runtime,
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
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* chars_value = register_arg_count == 0 ? nullptr : &registers[register_args[0]];
  if (!string_strip_body(leading[0], chars_value, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool string_rstrip_fast_method(
    Runtime& runtime,
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
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* chars_value = register_arg_count == 0 ? nullptr : &registers[register_args[0]];
  if (!string_rstrip_body(leading[0], chars_value, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool string_lstrip_fast_method(
    Runtime& runtime,
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
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* chars_value = register_arg_count == 0 ? nullptr : &registers[register_args[0]];
  if (!string_lstrip_body(leading[0], chars_value, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool string_startswith_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "str.startswith expected 1 to 3 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* start_value = argc >= 3 ? &args[2] : nullptr;
  const Value* end_value = argc >= 4 ? &args[3] : nullptr;
  if (!string_startswith_body(args[0], args[1], start_value, end_value, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool string_startswith_fast_method(
    Runtime& runtime,
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
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* start_value = register_arg_count >= 2 ? &registers[register_args[1]] : nullptr;
  const Value* end_value = register_arg_count >= 3 ? &registers[register_args[2]] : nullptr;
  if (!string_startswith_body(leading[0], registers[register_args[0]], start_value, end_value, out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
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
  if (argc < 2 || argc > 4) {
    error = "str.count expected 1 to 3 arguments";
    return false;
  }
  return string_count_body(args[0], args[1], argc >= 3 ? &args[2] : nullptr,
                           argc >= 4 ? &args[3] : nullptr, out, error);
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
  if (leading_count != 1 || register_arg_count < 1 || register_arg_count > 3 ||
      leading == nullptr || registers == nullptr || register_args == nullptr) {
    error = "str.count expected 1 to 3 arguments";
    return false;
  }
  return string_count_body(leading[0], registers[register_args[0]],
                           register_arg_count >= 2 ? &registers[register_args[1]] : nullptr,
                           register_arg_count >= 3 ? &registers[register_args[2]] : nullptr,
                           out, error);
}

bool string_replace_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3 && argc != 4) {
    error = "str.replace expected 3 or 4 arguments, got " + std::to_string(argc);
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(args[0], "str.replace target", text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView old_text;
  if (!get_string_view_checked(args[1], "str.replace old", old_text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView new_text;
  if (!get_string_view_checked(args[2], "str.replace new", new_text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t max_count = -1;
  if (argc == 4) {
    if (args[3].tag != ValueTag::Int64) {
      error = "str.replace count must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    max_count = args[3].as.i64;
  }
  return replace_string_body(args[0], text, old_text, new_text, max_count, out);
}

bool string_replace_fast_method(
    Runtime& runtime,
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
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(leading[0], "str.replace target", text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView old_text;
  if (!get_string_view_checked(registers[register_args[0]], "str.replace old", old_text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView new_text;
  if (!get_string_view_checked(registers[register_args[1]], "str.replace new", new_text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t max_count = -1;
  if (register_arg_count == 3) {
    const Value& count_value = registers[register_args[2]];
    if (count_value.tag != ValueTag::Int64) {
      error = "str.replace count must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    max_count = count_value.as.i64;
  }
  return replace_string_body(leading[0], text, old_text, new_text, max_count, out);
}

bool string_join_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "str.join", error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView sep;
  if (!get_string_view_checked(args[0], "str.join separator", sep, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (auto* list = value_as_list(args[1])) {
    if (join_string_values(sep, list->items, out, error)) return true;
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[1].tag == ValueTag::Object && args[1].as.obj != nullptr && args[1].as.obj->kind == ObjectKind::Tuple) {
    auto* tuple = reinterpret_cast<TupleObject*>(args[1].as.obj);
    if (join_string_values(sep, tuple->items, out, error)) return true;
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> items;
  if (!collect_join_iterable(runtime, args[1], items, error)) {
    error = "str.join argument must be iterable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (join_string_values(sep, items, out, error)) return true;
  runtime.raise_class_error("TypeError", error);
  return false;
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
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView sep;
  if (!get_string_view_checked(leading[0], "str.join separator", sep, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value& sequence = registers[register_args[0]];
  if (auto* list = value_as_list(sequence)) {
    if (join_string_values(sep, list->items, out, error)) return true;
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (auto* tuple = value_as_tuple(sequence)) {
    if (join_string_values(sep, tuple->items, out, error)) return true;
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> items;
  if (!collect_join_iterable(runtime, sequence, items, error)) {
    error = "str.join argument must be iterable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (join_string_values(sep, items, out, error)) return true;
  runtime.raise_class_error("TypeError", error);
  return false;
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

std::string format_replacement_value(
    Runtime& runtime,
    const Value& value,
    std::string_view field,
    std::string& error);

size_t format_field_close(std::string_view format, size_t open) {
  size_t nested = 0;
  for (size_t i = open + 1; i < format.size(); ++i) {
    if (format[i] == '{') {
      ++nested;
    } else if (format[i] == '}') {
      if (nested == 0) return i;
      --nested;
    }
  }
  return std::string_view::npos;
}

std::string resolve_positional_nested_fields(
    std::string_view field, const Value* args, uint32_t argc, uint32_t& next_arg, std::string& error) {
  std::string resolved;
  for (size_t i = 0; i < field.size();) {
    if (i + 1 < field.size() && field[i] == '{' && field[i + 1] == '}') {
      if (next_arg >= argc) {
        error = "str.format replacement index out of range";
        return {};
      }
      resolved += value_to_string(args[next_arg++]);
      i += 2;
    } else {
      resolved.push_back(field[i++]);
    }
  }
  return resolved;
}

bool string_format_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  error.clear();
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
      const auto close = format_field_close(format, i);
      if (close == std::string::npos) {
        error = "str.format unmatched '{'";
        return false;
      }
      uint32_t arg_index = next_arg++;
      const auto raw_field = format.substr(i + 1, close - i - 1);
      std::string resolved_field = resolve_positional_nested_fields(raw_field, args, argc, next_arg, error);
      if (!error.empty()) return false;
      const std::string_view field(resolved_field);
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
      result += format_replacement_value(runtime, args[arg_index], field, error);
      if (!error.empty()) {
        return false;
      }
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

std::string_view format_field_spec(std::string_view field) {
  const size_t spec = field.find(':');
  if (spec == std::string_view::npos) {
    return {};
  }
  return field.substr(spec + 1);
}

char format_field_conversion(std::string_view field) {
  const size_t conversion = field.find('!');
  if (conversion == std::string_view::npos || conversion + 1 >= field.size()) {
    return '\0';
  }
  const char value = field[conversion + 1];
  return value == 's' || value == 'r' || value == 'a' ? value : '\0';
}

std::string format_int_base(int64_t value, uint32_t base, bool uppercase) {
  const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
  const bool negative = value < 0;
  uint64_t magnitude = negative
      ? static_cast<uint64_t>(-(value + 1)) + 1u
      : static_cast<uint64_t>(value);
  std::string text;
  do {
    text.push_back(digits[magnitude % base]);
    magnitude /= base;
  } while (magnitude != 0);
  if (negative) {
    text.push_back('-');
  }
  std::reverse(text.begin(), text.end());
  return text;
}

std::string apply_simple_format_width(std::string text, std::string_view spec) {
  if (spec.empty()) {
    return text;
  }
  size_t i = 0;
  char fill = ' ';
  if (i < spec.size() && spec[i] == '0') {
    fill = '0';
    ++i;
  }
  int64_t width = 0;
  bool has_width = false;
  while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i]))) {
    has_width = true;
    width = width * 10 + static_cast<int64_t>(spec[i] - '0');
    ++i;
  }
  if (!has_width || width <= static_cast<int64_t>(text.size())) {
    return text;
  }
  const size_t pad = static_cast<size_t>(width - static_cast<int64_t>(text.size()));
  if (fill == '0' && !text.empty() && text[0] == '-') {
    return "-" + std::string(pad, fill) + text.substr(1);
  }
  return std::string(pad, fill) + text;
}

std::string format_replacement_value(
    Runtime& runtime,
    const Value& value,
    std::string_view field,
    std::string& error) {
  const std::string_view spec = format_field_spec(field);
  const char conversion = format_field_conversion(field);
  if (conversion != '\0') {
    Value converted_value;
    if (conversion == 's') {
      if (!builtin_str_from_value(runtime, value, converted_value, error)) {
        return {};
      }
    } else if (value_as_instance(value) != nullptr) {
      Value repr_method;
      if (!object_get_attr(value, "__repr__", repr_method, error) ||
          !runtime_call_callable(runtime, repr_method, nullptr, 0, converted_value, error)) {
        return {};
      }
    } else {
      converted_value = Value::string(value_to_repr(value));
    }
    auto* converted_string = value_as_string(converted_value);
    if (converted_string == nullptr) {
      error = conversion == 's' ? "__str__ returned non-string" : "__repr__ returned non-string";
      runtime.raise_class_error("TypeError", error);
      return {};
    }
    error.clear();
    std::string converted = string_object_to_string(*converted_string);
    if (spec.empty()) {
      return converted;
    }
    char type = '\0';
    if (std::isalpha(static_cast<unsigned char>(spec.back()))) {
      type = spec.back();
    }
    if (type != '\0' && type != 's') {
      error = "unsupported format specifier";
      return {};
    }
    return apply_simple_format_width(
        std::move(converted),
        type == 's' ? spec.substr(0, spec.size() - 1) : spec);
  }
  if (value_as_instance(value) != nullptr) {
    Value format_method;
    if (!object_get_attr(value, "__format__", format_method, error)) {
      return {};
    }
    Value format_spec = Value::string(std::string(spec));
    Value formatted;
    if (!runtime_call_callable(runtime, format_method, &format_spec, 1, formatted, error)) {
      return {};
    }
    auto* formatted_string = value_as_string(formatted);
    if (formatted_string == nullptr) {
      error = "__format__ must return a str";
      runtime.raise_class_error("TypeError", error);
      return {};
    }
    error.clear();
    return string_object_to_string(*formatted_string);
  }
  if (spec.empty()) {
    return value_to_string(value);
  }

  if (const Value* format_builtin = runtime.find_builtin("format")) {
    Value format_args[] = {value, Value::string(std::string(spec))};
    Value formatted;
    if (!runtime_call_callable(runtime, *format_builtin, format_args, 2, formatted, error)) return {};
    if (auto* text = value_as_string(formatted)) {
      error.clear();
      return string_object_to_string(*text);
    }
  }
  char type = '\0';
  if (!spec.empty() && std::isalpha(static_cast<unsigned char>(spec.back()))) {
    type = spec.back();
  }
  int64_t int_value = 0;
  if ((type == 'x' || type == 'X' || type == 'd' || type == 'b' || type == 'o') &&
      value_int_like_to_i64(value, int_value)) {
    uint32_t base = 10;
    if (type == 'x' || type == 'X') base = 16;
    else if (type == 'b') base = 2;
    else if (type == 'o') base = 8;
    std::string text = format_int_base(int_value, base, type == 'X');
    return apply_simple_format_width(std::move(text), spec.substr(0, spec.size() - 1));
  }
  if ((type == 'f' || type == 'F' || type == 'e' || type == 'E' ||
       type == 'g' || type == 'G') &&
      (value.tag == ValueTag::Int64 || value.tag == ValueTag::Bool || value.tag == ValueTag::Double)) {
    const auto body = spec.substr(0, spec.size() - 1);
    size_t cursor = 0;
    const bool zero_fill = cursor < body.size() && body[cursor] == '0';
    if (zero_fill) ++cursor;
    int width = 0;
    while (cursor < body.size() && std::isdigit(static_cast<unsigned char>(body[cursor]))) {
      width = width * 10 + static_cast<int>(body[cursor++] - '0');
    }
    int precision = -1;
    if (cursor < body.size() && body[cursor] == '.') {
      ++cursor;
      precision = 0;
      while (cursor < body.size() && std::isdigit(static_cast<unsigned char>(body[cursor]))) {
        precision = precision * 10 + static_cast<int>(body[cursor++] - '0');
      }
    }
    if (cursor != body.size()) {
      error = "unsupported format specifier";
      return {};
    }
    std::ostringstream stream;
    if (type == 'f' || type == 'F') stream << std::fixed;
    if (type == 'e' || type == 'E') stream << std::scientific;
    if (type == 'E' || type == 'F' || type == 'G') stream << std::uppercase;
    if (precision >= 0) stream << std::setprecision(precision);
    const double number = value.tag == ValueTag::Int64
        ? static_cast<double>(value.as.i64)
        : value.tag == ValueTag::Bool ? (value.as.b ? 1.0 : 0.0) : value.as.f64;
    stream << number;
    std::string text = stream.str();
    if (width > static_cast<int>(text.size())) {
      const size_t padding = static_cast<size_t>(width - static_cast<int>(text.size()));
      if (zero_fill && !text.empty() && (text[0] == '-' || text[0] == '+')) {
        text.insert(1, padding, '0');
      } else {
        text.insert(0, padding, zero_fill ? '0' : ' ');
      }
    }
    return text;
  }
  if (type == 's' || type == '\0') {
    return apply_simple_format_width(value_to_string(value), type == 's' ? spec.substr(0, spec.size() - 1) : spec);
  }
  error = "unsupported format specifier";
  return {};
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
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  error.clear();
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
      const auto close = format_field_close(format, i);
      if (close == std::string::npos) {
        error = "str.format unmatched '{'";
        return false;
      }
      const auto raw_field = format.substr(i + 1, close - i - 1);
      const bool main_is_auto = format_field_name(raw_field).empty();
      uint32_t main_auto_index = 0;
      if (main_is_auto) {
        if (next_arg >= argc) { error = "str.format replacement index out of range"; return false; }
        main_auto_index = next_arg++;
      }
      std::string resolved_field;
      for (size_t cursor = 0; cursor < raw_field.size();) {
        if (raw_field[cursor] == '{') {
          const size_t end = raw_field.find('}', cursor + 1);
          if (end == std::string_view::npos) { error = "str.format unmatched '{'"; return false; }
          const auto nested_name = raw_field.substr(cursor + 1, end - cursor - 1);
          const Value* nested = nested_name.empty() && next_arg < argc
              ? &args[next_arg++] : find_format_keyword(nested_name, kwargs, kwargc);
          if (nested == nullptr) { error = "str.format missing nested field"; return false; }
          resolved_field += value_to_string(*nested);
          cursor = end + 1;
        } else {
          resolved_field.push_back(raw_field[cursor++]);
        }
      }
      const std::string_view field(resolved_field);
      const auto field_name = format_field_name(field);
      const Value* replacement = nullptr;
      if (field_name.empty()) {
        replacement = &args[main_auto_index];
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
            runtime.raise_class_error("KeyError", std::string(field_name));
            return false;
          }
        }
      }
      result += format_replacement_value(runtime, *replacement, field, error);
      if (!error.empty()) {
        return false;
      }
      i = close + 1;
      continue;
    }
    result.push_back(format[i++]);
  }
  out = Value::string(std::move(result));
  return true;
}

bool string_format_map_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  error.clear();
  if (argc != 2) {
    error = "str.format_map expected 1 argument";
    return false;
  }
  memory::X3StringView format_ref;
  if (!get_string_view_checked(args[0], "str.format_map target", format_ref, error)) {
    return false;
  }

  Value getitem;
  if (!object_get_attr(args[1], "__getitem__", getitem, error)) {
    error = "str.format_map argument must be a mapping";
    return false;
  }
  error.clear();

  const auto format = as_view(format_ref);
  std::string result;
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
        error = "str.format_map unmatched '{'";
        return false;
      }
      const auto field = format.substr(i + 1, close - i - 1);
      const auto field_name = format_field_name(field);
      if (field_name.empty()) {
        error = "Format string contains positional fields";
        return false;
      }
      Value key = Value::string(std::string(field_name));
      Value replacement;
      if (!runtime_call_callable(runtime, getitem, &key, 1, replacement, error)) {
        return false;
      }
      result += format_replacement_value(runtime, replacement, field, error);
      if (!error.empty()) {
        return false;
      }
      i = close + 1;
      continue;
    }
    if (format[i] == '}') {
      error = "str.format_map single '}' encountered in format string";
      return false;
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
  if (encoding != "ascii" && encoding != "latin_1" && encoding != "cp437" && encoding != "mbcs") {
    Value codecs_module;
    const Value* import_function = runtime.find_builtin("__import__");
    if (import_function == nullptr) {
      error = "__import__ is unavailable";
      return false;
    }
    Value import_arg = Value::string("_codecs");
    if (!runtime_call_callable(runtime, *import_function, &import_arg, 1, codecs_module, error)) return false;
    Value lookup_function;
    Value codec_info;
    Value lookup_arg = Value::string(encoding);
    if (!module_get_attr(codecs_module, "lookup", lookup_function, error) ||
        !runtime_call_callable(runtime, lookup_function, &lookup_arg, 1, codec_info, error)) {
      return false;
    }
    Value is_text_encoding;
    bool is_text = true;
    std::string ignored;
    if (object_get_attr(codec_info, "_is_text_encoding", is_text_encoding, ignored) &&
        !runtime_truthy(runtime, is_text_encoding, is_text, error)) {
      return false;
    }
    if (!is_text) {
      error = "'" + encoding + "' is not a text encoding; use codecs.encode() to handle arbitrary codecs";
      runtime.raise_class_error("LookupError", error);
      return false;
    }
    Value encode_function;
    if (!module_get_attr(codecs_module, "encode", encode_function, error)) return false;
    Value call_args[3] = {args[0], Value::string(encoding), Value::string(errors)};
    Value encoded;
    if (!runtime_call_callable(runtime, encode_function, call_args, 3, encoded, error)) return false;
    if (value_as_bytes(encoded) == nullptr) {
      const char* result_type = value_as_string(encoded) != nullptr ? "str" :
          (encoded.tag == ValueTag::None ? "NoneType" : "object");
      error = "'" + encoding + "' encoder returned '" + result_type +
          "' instead of 'bytes'; use codecs.encode() to encode to arbitrary types";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    out = std::move(encoded);
    return true;
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
      } else if (errors == "xmlcharrefreplace") {
        encoded += "&#" + std::to_string(codepoint) + ";";
        i += advance;
      } else if (errors == "surrogateescape" && codepoint >= 0xdc80u && codepoint <= 0xdcffu) {
        encoded.push_back(static_cast<char>(codepoint - 0xdc00u));
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
  if (encoding == "cp424") {
    std::string encoded;
    const auto view = as_view(text);
    for (size_t i = 0; i < view.size();) {
      const unsigned char ch = static_cast<unsigned char>(view[i]);
      const size_t width = utf8_codepoint_width(ch);
      const uint32_t codepoint = width == 0 || i + width > view.size()
          ? ch : decode_utf8_codepoint(view.substr(i), width);
      const size_t advance = width == 0 ? 1 : width;
      if (codepoint == 0x00a2) encoded.push_back('J');
      else if (codepoint == '\r') encoded.push_back('\r');
      else if (codepoint == '\n') encoded.push_back('%');
      else if (errors == "ignore") {}
      else if (errors == "replace") encoded.push_back('?');
      else {
        error = "'charmap' codec can't encode character";
        runtime.raise_class_error("UnicodeEncodeError", error);
        return false;
      }
      i += advance;
    }
    out = Value::bytes(std::move(encoded));
    return true;
  }
  if (encoding.rfind("utf_16", 0) == 0 || encoding.rfind("utf_32", 0) == 0) {
    std::string encoded;
    if (!encode_utf16_or_utf32(as_view(text), encoding, encoded, error)) {
      runtime.raise_class_error("UnicodeEncodeError", error);
      return false;
    }
    out = Value::bytes(std::move(encoded));
    return true;
  }
  if (encoding == "mbcs") {
#if defined(_WIN32)
    std::string encoded;
    if (!mbcs_encode_text(as_view(text), errors, encoded, error)) {
      runtime.raise_class_error("UnicodeEncodeError", error);
      return false;
    }
    out = Value::bytes(std::move(encoded));
    return true;
#else
    out = Value::bytes(std::string(as_view(text)));
    return true;
#endif
  }
  if (encoding == "utf_8_sig") {
    out = Value::bytes(std::string("\xef\xbb\xbf", 3) + std::string(as_view(text)));
    return true;
  }
  if (errors == "surrogateescape") {
    const auto view = as_view(text);
    std::string encoded;
    encoded.reserve(view.size());
    for (size_t index = 0; index < view.size();) {
      if (index + 2 < view.size() &&
          static_cast<unsigned char>(view[index]) == 0xedu &&
          (static_cast<unsigned char>(view[index + 1]) == 0xb2u ||
           static_cast<unsigned char>(view[index + 1]) == 0xb3u) &&
          (static_cast<unsigned char>(view[index + 2]) & 0xc0u) == 0x80u) {
        const uint32_t codepoint =
            ((static_cast<unsigned char>(view[index]) & 0x0fu) << 12u) |
            ((static_cast<unsigned char>(view[index + 1]) & 0x3fu) << 6u) |
            (static_cast<unsigned char>(view[index + 2]) & 0x3fu);
        if (codepoint >= 0xdc80u && codepoint <= 0xdcffu) {
          encoded.push_back(static_cast<char>(codepoint - 0xdc00u));
          index += 3;
          continue;
        }
      }
      encoded.push_back(view[index++]);
    }
    out = Value::bytes(std::move(encoded));
    return true;
  }
  out = Value::bytes(std::string(as_view(text)));
  return true;
}

bool string_encode_method_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1 || argc > 3) {
    error = "str.encode expected 0 to 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value merged[3] = {args[0], Value::string("utf-8"), Value::string("strict")};
  bool supplied[3] = {true, false, false};
  for (uint32_t index = 1; index < argc; ++index) {
    merged[index] = args[index];
    supplied[index] = true;
  }
  static const char* names[] = {"", "encoding", "errors"};
  for (uint32_t index = 0; index < kwargc; ++index) {
    const std::string name = kwargs[index].name == nullptr ? std::string() : kwargs[index].name;
    size_t destination = 3;
    for (size_t candidate = 1; candidate < 3; ++candidate) {
      if (name == names[candidate]) destination = candidate;
    }
    if (destination == 3) {
      error = "str.encode got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (supplied[destination]) {
      error = "str.encode got multiple values for argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    merged[destination] = *kwargs[index].value;
    supplied[destination] = true;
  }
  const uint32_t merged_argc = supplied[2] ? 3 : supplied[1] ? 2 : 1;
  return string_encode_method(runtime, merged, merged_argc, out, error, user_data);
}

Value split_whitespace(memory::X3StringView text, int64_t maxsplit = -1) {
  auto text_view = as_view(text);
  size_t count = 0;
  bool in_word = false;
  if (maxsplit < 0) {
    for (const unsigned char ch : text_view) {
      const bool space = string_ascii_isspace(ch);
      if (!space && !in_word) {
        ++count;
      }
      in_word = !space;
    }
  } else {
    count = static_cast<size_t>(maxsplit) + 1;
  }
  Value out = Value::list_reserved(count);
  auto* list = value_as_list(out);
  size_t i = 0;
  int64_t splits = 0;
  while (i < text_view.size()) {
    while (i < text_view.size() && string_ascii_isspace(static_cast<unsigned char>(text_view[i]))) {
      ++i;
    }
    const size_t start = i;
    if (maxsplit >= 0 && splits >= maxsplit) {
      size_t end = text_view.size();
      while (end > start && string_ascii_isspace(static_cast<unsigned char>(text_view[end - 1]))) {
        --end;
      }
      if (end > start) {
        list->items.push_back(make_string_range_unchecked(text, start, end - start));
      }
      return out;
    }
    while (i < text_view.size() && !string_ascii_isspace(static_cast<unsigned char>(text_view[i]))) {
      ++i;
    }
    if (i > start) {
      list->items.push_back(make_string_range_unchecked(text, start, i - start));
      ++splits;
    }
  }
  return out;
}

bool split_separator(
    memory::X3StringView text,
    memory::X3StringView sep,
    Value& out,
    std::string& error,
    int64_t maxsplit = -1) {
  auto text_view = as_view(text);
  auto sep_view = as_view(sep);
  if (sep_view.empty()) {
    error = "empty separator";
    return false;
  }
  if (sep_view.size() == 1) {
    const char sep_ch = sep_view[0];
    size_t count = 1;
    int64_t seen = 0;
    for (const auto ch : text_view) {
      if (ch == sep_ch) {
        ++count;
        if (maxsplit >= 0 && ++seen >= maxsplit) {
          break;
        }
      }
    }
    Value result = Value::list_reserved(count);
    auto* list = value_as_list(result);
    size_t start = 0;
    int64_t splits = 0;
    for (size_t i = 0; i < text_view.size(); ++i) {
      if (text_view[i] == sep_ch && (maxsplit < 0 || splits < maxsplit)) {
        list->items.push_back(make_string_range_unchecked(text, start, i - start));
        start = i + 1;
        ++splits;
      }
    }
    list->items.push_back(make_string_range_unchecked(text, start, text_view.size() - start));
    value_move_assign_fast(out, result);
    return true;
  }
  const int64_t reserved_matches = maxsplit < 0 ? count_non_overlapping_matches(text, sep, -1) : maxsplit;
  Value result = Value::list_reserved(static_cast<size_t>(reserved_matches < 0 ? 0 : reserved_matches) + 1);
  auto* list = value_as_list(result);
  size_t start = 0;
  int64_t splits = 0;
  while (true) {
    if (maxsplit >= 0 && splits >= maxsplit) {
      list->items.push_back(make_string_range_unchecked(text, start, text_view.size() - start));
      value_move_assign_fast(out, result);
      return true;
    }
    const size_t pos = text_view.find(sep_view, start);
    if (pos == std::string::npos) {
      list->items.push_back(make_string_range_unchecked(text, start, text_view.size() - start));
      value_move_assign_fast(out, result);
      return true;
    }
    list->items.push_back(make_string_range_unchecked(text, start, pos - start));
    start = pos + sep_view.size();
    ++splits;
  }
}

bool split_args(
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    memory::X3StringView& text,
    const Value*& sep_value,
    int64_t& maxsplit,
    std::string& error) {
  if (argc < 1 || argc > 3) {
    error = "str.split expected target, optional sep, and optional maxsplit";
    return false;
  }
  if (!get_string_view_checked(args[0], "str.split target", text, error)) {
    return false;
  }
  sep_value = argc >= 2 ? &args[1] : nullptr;
  maxsplit = -1;
  if (argc >= 3) {
    if (args[2].tag != ValueTag::Int64) {
      error = "str.split maxsplit must be int";
      return false;
    }
    maxsplit = args[2].as.i64;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (name == "sep") {
      if (sep_value != nullptr) {
        error = "str.split got multiple values for argument 'sep'";
        return false;
      }
      sep_value = kwargs[i].value;
    } else if (name == "maxsplit") {
      if (argc >= 3) {
        error = "str.split got multiple values for argument 'maxsplit'";
        return false;
      }
      if (kwargs[i].value->tag != ValueTag::Int64) {
        error = "str.split maxsplit must be int";
        return false;
      }
      maxsplit = kwargs[i].value->as.i64;
    } else {
      error = "str.split got an unexpected keyword argument '" + std::string(name) + "'";
      return false;
    }
  }
  return true;
}

bool split_common(
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  memory::X3StringView text;
  const Value* sep_value = nullptr;
  int64_t maxsplit = -1;
  if (!split_args(args, argc, kwargs, kwargc, text, sep_value, maxsplit, error)) {
    return false;
  }
  if (sep_value == nullptr || sep_value->tag == ValueTag::None) {
    out = split_whitespace(text, maxsplit);
  } else {
    memory::X3StringView sep;
    if (!get_string_view_checked(*sep_value, "str.split separator", sep, error)) {
      return false;
    }
    if (!split_separator(text, sep, out, error, maxsplit)) {
      return false;
    }
  }
  return true;
}

Value rsplit_whitespace(memory::X3StringView text, int64_t maxsplit = -1) {
  auto text_view = as_view(text);
  std::vector<Value> items;
  size_t end = text_view.size();
  int64_t splits = 0;
  while (end > 0) {
    while (end > 0 && string_ascii_isspace(static_cast<unsigned char>(text_view[end - 1]))) {
      --end;
    }
    if (end == 0) {
      break;
    }
    if (maxsplit >= 0 && splits >= maxsplit) {
      size_t start = 0;
      while (start < end && string_ascii_isspace(static_cast<unsigned char>(text_view[start]))) {
        ++start;
      }
      if (end > start) {
        items.push_back(make_string_range_unchecked(text, start, end - start));
      }
      break;
    }
    size_t start = end;
    while (start > 0 && !string_ascii_isspace(static_cast<unsigned char>(text_view[start - 1]))) {
      --start;
    }
    items.push_back(make_string_range_unchecked(text, start, end - start));
    end = start;
    ++splits;
  }
  std::reverse(items.begin(), items.end());
  return Value::list(std::move(items));
}

bool rsplit_separator(
    memory::X3StringView text,
    memory::X3StringView sep,
    Value& out,
    std::string& error,
    int64_t maxsplit = -1) {
  auto text_view = as_view(text);
  auto sep_view = as_view(sep);
  if (sep_view.empty()) {
    error = "empty separator";
    return false;
  }
  std::vector<Value> items;
  size_t end = text_view.size();
  int64_t splits = 0;
  while (maxsplit < 0 || splits < maxsplit) {
    const size_t search_pos = end == 0 ? 0 : end - 1;
    const size_t pos = text_view.rfind(sep_view, search_pos);
    if (pos == std::string_view::npos || pos + sep_view.size() > end) {
      break;
    }
    items.push_back(make_string_range_unchecked(text, pos + sep_view.size(), end - pos - sep_view.size()));
    end = pos;
    ++splits;
    if (end == 0) {
      break;
    }
  }
  items.push_back(make_string_range_unchecked(text, 0, end));
  std::reverse(items.begin(), items.end());
  out = Value::list(std::move(items));
  return true;
}

bool rsplit_common(
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  memory::X3StringView text;
  const Value* sep_value = nullptr;
  int64_t maxsplit = -1;
  if (!split_args(args, argc, kwargs, kwargc, text, sep_value, maxsplit, error)) {
    return false;
  }
  if (sep_value == nullptr || sep_value->tag == ValueTag::None) {
    out = rsplit_whitespace(text, maxsplit);
  } else {
    memory::X3StringView sep;
    if (!get_string_view_checked(*sep_value, "str.rsplit separator", sep, error)) {
      return false;
    }
    if (!rsplit_separator(text, sep, out, error, maxsplit)) {
      return false;
    }
  }
  return true;
}

bool string_split_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!split_common(args, argc, nullptr, 0, out, error)) {
    runtime.raise_class_error(error == "empty separator" ? "ValueError" : "TypeError", error);
    return false;
  }
  return true;
}

bool string_split_kw_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (!split_common(args, argc, kwargs, kwargc, out, error)) {
    runtime.raise_class_error(error == "empty separator" ? "ValueError" : "TypeError", error);
    return false;
  }
  return true;
}

bool string_split_fast_method(
    Runtime& runtime,
    const Value* leading,
    uint32_t leading_count,
    const Value* registers,
    const uint32_t* register_args,
    uint32_t register_arg_count,
    Value& out,
    std::string& error,
    void*) {
  if (leading_count != 1 || register_arg_count > 2 || leading == nullptr ||
      (register_arg_count != 0 && (registers == nullptr || register_args == nullptr))) {
    error = "str.split expected 0 to 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  memory::X3StringView text;
  if (!get_string_view_checked(leading[0], "str.split target", text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t maxsplit = -1;
  if (register_arg_count >= 2) {
    const Value& maxsplit_value = registers[register_args[1]];
    if (maxsplit_value.tag != ValueTag::Int64) {
      error = "str.split maxsplit must be int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    maxsplit = maxsplit_value.as.i64;
  }
  if (register_arg_count == 0) {
    out = split_whitespace(text, maxsplit);
  } else {
    const Value& sep_value = registers[register_args[0]];
    if (sep_value.tag == ValueTag::None) {
      out = split_whitespace(text, maxsplit);
    } else {
      memory::X3StringView sep;
      if (!get_string_view_checked(sep_value, "str.split separator", sep, error)) {
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      if (!split_separator(text, sep, out, error, maxsplit)) {
        runtime.raise_class_error(error == "empty separator" ? "ValueError" : "TypeError", error);
        return false;
      }
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
  const auto view = as_view(text);
  bool first = true;
  for (size_t i = 0; i < view.size();) {
    const unsigned char lead = static_cast<unsigned char>(view[i]);
    const size_t width = utf8_codepoint_width(lead);
    if (width == 0 || i + width > view.size()) {
      value_set_bool(out, false);
      return true;
    }
    const uint32_t codepoint = decode_utf8_codepoint(view.substr(i), width);
    if (!unicode_identifier_codepoint(codepoint, first)) {
      value_set_bool(out, false);
      return true;
    }
    first = false;
    i += width;
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
    const unsigned char ch = static_cast<unsigned char>(view[i]);
    size_t linebreak_width = 0;
    if (ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f' ||
        ch == 0x1c || ch == 0x1d || ch == 0x1e) {
      linebreak_width = 1;
    } else if (ch == 0xc2 && i + 1 < view.size() &&
               static_cast<unsigned char>(view[i + 1]) == 0x85) {
      linebreak_width = 2;
    } else if (ch == 0xe2 && i + 2 < view.size() &&
               static_cast<unsigned char>(view[i + 1]) == 0x80 &&
               (static_cast<unsigned char>(view[i + 2]) == 0xa8 ||
                static_cast<unsigned char>(view[i + 2]) == 0xa9)) {
      linebreak_width = 3;
    }
    if (linebreak_width == 0) continue;
    size_t end = i;
    size_t next = i + linebreak_width;
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

bool string_splitlines_kw_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (kwargc == 0) {
    return string_splitlines_method(runtime, args, argc, out, error, user_data);
  }
  if (argc != 1 || kwargc != 1 || kwargs[0].name == nullptr || kwargs[0].value == nullptr ||
      std::string_view(kwargs[0].name) != "keepends") {
    error = "str.splitlines got an unexpected or duplicate keyword argument";
    return false;
  }
  Value positional[] = {args[0], *kwargs[0].value};
  return string_splitlines_method(runtime, positional, 2, out, error, user_data);
}

bool string_eq_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "str.__eq__ expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto* left = value_as_string(args[0]);
  const auto* right = value_as_string(args[1]);
  if (left == nullptr || right == nullptr) {
    if (const Value* not_implemented = runtime.find_builtin("NotImplemented")) {
      value_assign_fast(out, *not_implemented);
    } else {
      value_set_bool(out, false);
    }
    return true;
  }
  value_set_bool(out, string_object_view(*left) == string_object_view(*right));
  return true;
}

bool string_rsplit_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  (void)runtime;
  (void)user_data;
  return rsplit_common(args, argc, nullptr, 0, out, error);
}

bool string_rsplit_kw_method(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return rsplit_common(args, argc, kwargs, kwargc, out, error);
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
    {"__eq__", "str.__eq__", string_eq_method},
    {"__getitem__", "str.__getitem__", string_getitem_method},
    {"__repr__", "str.__repr__", string_repr_method},
    {"__str__", "str.__str__", string_str_method},
    {"capitalize", "str.capitalize", string_capitalize_method},
    {"casefold", "str.casefold", string_casefold_method},
    {"center", "str.center", string_center_method},
    {"count", "str.count", string_count_method, string_count_fast_method},
    {"encode", "str.encode", string_encode_method, nullptr, false, string_encode_method_kw},
    {"endswith", "str.endswith", string_endswith_method, string_endswith_fast_method},
    {"expandtabs", "str.expandtabs", string_expandtabs_method},
    {"find", "str.find", string_find_method, string_find_fast_method},
    {"format", "str.format", string_format_method, nullptr, false, string_format_method_kw},
    {"format_map", "str.format_map", string_format_map_method},
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
    {"join", "str.join", string_join_method, string_join_fast_method, false, nullptr, "($self, iterable, /)"},
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
    {"rsplit", "str.rsplit", string_rsplit_method, nullptr, false, string_rsplit_kw_method},
    {"rstrip", "str.rstrip", string_rstrip_method, string_rstrip_fast_method},
    {"split", "str.split", string_split_method, string_split_fast_method, false, string_split_kw_method},
    {"splitlines", "str.splitlines", string_splitlines_method, nullptr, false, string_splitlines_kw_method},
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
    Value function = runtime.make_native_function(
        method.full_name,
        method.callback,
        nullptr,
        nullptr,
        method.fast_callback,
        method.fast_releases_vm_lock,
        method.keyword_callback);
    builtin_method_set_text_signature(function, method.text_signature);
    string_class.attrs[method.name] = std::move(function);
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
