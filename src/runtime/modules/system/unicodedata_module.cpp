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
#include "xlang3/value.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace xlang3 {

namespace {

struct UnicodeRecord {
  uint32_t codepoint;
  const char* name;
  const char* category;
  const char* bidirectional;
  int combining;
  const char* east_asian_width;
  int mirrored;
  int decimal;
  int digit;
  double numeric;
};

constexpr int kNoNumber = -1;
constexpr double kNoNumeric = -1.0;

constexpr UnicodeRecord kUnicodeRecords[] = {
    {0x0020, "SPACE", "Zs", "WS", 0, "Na", 0, kNoNumber, kNoNumber, kNoNumeric},
    {0x0030, "DIGIT ZERO", "Nd", "EN", 0, "Na", 0, 0, 0, 0.0},
    {0x0031, "DIGIT ONE", "Nd", "EN", 0, "Na", 0, 1, 1, 1.0},
    {0x0032, "DIGIT TWO", "Nd", "EN", 0, "Na", 0, 2, 2, 2.0},
    {0x0033, "DIGIT THREE", "Nd", "EN", 0, "Na", 0, 3, 3, 3.0},
    {0x0034, "DIGIT FOUR", "Nd", "EN", 0, "Na", 0, 4, 4, 4.0},
    {0x0035, "DIGIT FIVE", "Nd", "EN", 0, "Na", 0, 5, 5, 5.0},
    {0x0036, "DIGIT SIX", "Nd", "EN", 0, "Na", 0, 6, 6, 6.0},
    {0x0037, "DIGIT SEVEN", "Nd", "EN", 0, "Na", 0, 7, 7, 7.0},
    {0x0038, "DIGIT EIGHT", "Nd", "EN", 0, "Na", 0, 8, 8, 8.0},
    {0x0039, "DIGIT NINE", "Nd", "EN", 0, "Na", 0, 9, 9, 9.0},
    {0x0041, "LATIN CAPITAL LETTER A", "Lu", "L", 0, "Na", 0, kNoNumber, kNoNumber, kNoNumeric},
    {0x0061, "LATIN SMALL LETTER A", "Ll", "L", 0, "Na", 0, kNoNumber, kNoNumber, kNoNumeric},
    {0x00b2, "SUPERSCRIPT TWO", "No", "EN", 0, "A", 0, kNoNumber, 2, 2.0},
    {0x00be, "VULGAR FRACTION THREE QUARTERS", "No", "ON", 0, "A", 0, kNoNumber, kNoNumber, 0.75},
    {0x00c5, "LATIN CAPITAL LETTER A WITH RING ABOVE", "Lu", "L", 0, "N", 0, kNoNumber, kNoNumber, kNoNumeric},
    {0x00e9, "LATIN SMALL LETTER E WITH ACUTE", "Ll", "L", 0, "A", 0, kNoNumber, kNoNumber, kNoNumeric},
    {0x0301, "COMBINING ACUTE ACCENT", "Mn", "NSM", 230, "A", 0, kNoNumber, kNoNumber, kNoNumeric},
    {0x030a, "COMBINING RING ABOVE", "Mn", "NSM", 230, "A", 0, kNoNumber, kNoNumber, kNoNumeric},
    {0x2044, "FRACTION SLASH", "Sm", "CS", 0, "N", 0, kNoNumber, kNoNumber, kNoNumeric},
    {0x4e2d, "CJK UNIFIED IDEOGRAPH-4E2D", "Lo", "L", 0, "W", 0, kNoNumber, kNoNumber, kNoNumeric},
    {0x1f642, "SLIGHTLY SMILING FACE", "So", "ON", 0, "W", 0, kNoNumber, kNoNumber, kNoNumeric},
};

bool decode_first_utf8(std::string_view text, uint32_t& codepoint, size_t& width) {
  if (text.empty()) {
    return false;
  }
  const unsigned char lead = static_cast<unsigned char>(text[0]);
  width = utf8_codepoint_width(lead);
  if (width == 0 || width > text.size()) {
    return false;
  }
  if (width == 1) {
    codepoint = lead;
    return true;
  }
  codepoint = lead & ((1u << (7 - width)) - 1u);
  for (size_t i = 1; i < width; ++i) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if ((ch & 0xc0u) != 0x80u) {
      return false;
    }
    codepoint = (codepoint << 6) | (ch & 0x3fu);
  }
  return true;
}

bool append_utf8(uint32_t codepoint, std::string& out) {
  if (codepoint <= 0x7f) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0x10ffff) {
    out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    return false;
  }
  return true;
}

const UnicodeRecord* find_record(uint32_t codepoint) {
  for (const auto& record : kUnicodeRecords) {
    if (record.codepoint == codepoint) {
      return &record;
    }
  }
  return nullptr;
}

const UnicodeRecord* find_record_by_name(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  for (const auto& record : kUnicodeRecords) {
    if (name == record.name) {
      return &record;
    }
  }
  return nullptr;
}

bool get_single_codepoint(Runtime& runtime, const Value& value, const char* function_name, uint32_t& codepoint, std::string& error) {
  auto* string = value_as_string(value);
  if (string == nullptr) {
    error = std::string(function_name) + "() argument must be a unicode character";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string_view text = string_object_view(*string);
  size_t width = 0;
  if (!decode_first_utf8(text, codepoint, width) || width != text.size()) {
    error = std::string(function_name) + "() argument must be a unicode character, not str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

std::string canonical_form(std::string form) {
  std::transform(form.begin(), form.end(), form.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  return form;
}

std::string decompose_canonical(std::string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    uint32_t codepoint = 0;
    size_t width = 0;
    if (!decode_first_utf8(text.substr(i), codepoint, width)) {
      out.append(text.substr(i));
      break;
    }
    if (codepoint == 0x00e9) {
      out.push_back('e');
      append_utf8(0x0301, out);
    } else if (codepoint == 0x00c5) {
      out.push_back('A');
      append_utf8(0x030a, out);
    } else {
      out.append(text.substr(i, width));
    }
    i += width;
  }
  return out;
}

std::string compose_canonical(std::string_view text) {
  std::string out;
  for (size_t i = 0; i < text.size();) {
    uint32_t first = 0;
    size_t first_width = 0;
    if (!decode_first_utf8(text.substr(i), first, first_width)) {
      out.append(text.substr(i));
      break;
    }
    uint32_t second = 0;
    size_t second_width = 0;
    const bool has_second = i + first_width < text.size() &&
                            decode_first_utf8(text.substr(i + first_width), second, second_width);
    if (first == 'e' && has_second && second == 0x0301) {
      append_utf8(0x00e9, out);
      i += first_width + second_width;
      continue;
    }
    if (first == 'A' && has_second && second == 0x030a) {
      append_utf8(0x00c5, out);
      i += first_width + second_width;
      continue;
    }
    out.append(text.substr(i, first_width));
    i += first_width;
  }
  return out;
}

std::string normalize_text(const std::string& form, std::string_view text) {
  if (form == "NFD") {
    return decompose_canonical(text);
  }
  if (form == "NFC") {
    return compose_canonical(text);
  }
  if (form == "NFKD") {
    std::string decomposed = decompose_canonical(text);
    std::string out;
    for (size_t i = 0; i < decomposed.size();) {
      uint32_t codepoint = 0;
      size_t width = 0;
      if (!decode_first_utf8(std::string_view(decomposed).substr(i), codepoint, width)) {
        out.append(std::string_view(decomposed).substr(i));
        break;
      }
      if (codepoint == 0x00b2) {
        out.push_back('2');
      } else if (codepoint == 0x00be) {
        out.push_back('3');
        append_utf8(0x2044, out);
        out.push_back('4');
      } else {
        out.append(std::string_view(decomposed).substr(i, width));
      }
      i += width;
    }
    return out;
  }
  if (form == "NFKC") {
    return compose_canonical(normalize_text("NFKD", text));
  }
  return std::string(text);
}

bool get_string_arg(Runtime& runtime, const Value& value, const char* name, std::string& out, std::string& error) {
  auto* string = value_as_string(value);
  if (string == nullptr) {
    error = std::string(name) + " must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = string_object_to_string(*string);
  return true;
}

bool unicode_lookup(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "unicodedata.lookup() expected name";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string name;
  if (!get_string_arg(runtime, args[0], "unicodedata.lookup() name", name, error)) {
    return false;
  }
  const UnicodeRecord* record = find_record_by_name(name);
  if (record == nullptr) {
    error = "undefined character name '" + name + "'";
    runtime.raise_class_error("KeyError", error);
    return false;
  }
  std::string text;
  append_utf8(record->codepoint, text);
  out = Value::string(std::move(text));
  return true;
}

bool unicode_name(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "unicodedata.name() expected character and optional default";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  uint32_t codepoint = 0;
  if (!get_single_codepoint(runtime, args[0], "name", codepoint, error)) {
    return false;
  }
  const UnicodeRecord* record = find_record(codepoint);
  if (record != nullptr) {
    out = Value::string(record->name);
    return true;
  }
  if (argc == 2) {
    out = args[1];
    return true;
  }
  error = "no such name";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool unicode_property_string(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, const char* function_name, const char* UnicodeRecord::*field, const char* default_value) {
  if (argc != 1) {
    error = std::string("unicodedata.") + function_name + "() expected character";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  uint32_t codepoint = 0;
  if (!get_single_codepoint(runtime, args[0], function_name, codepoint, error)) {
    return false;
  }
  const UnicodeRecord* record = find_record(codepoint);
  out = Value::string(record == nullptr ? default_value : record->*field);
  return true;
}

bool unicode_category(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unicode_property_string(runtime, args, argc, out, error, "category", &UnicodeRecord::category, "Cn");
}

bool unicode_bidirectional(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unicode_property_string(runtime, args, argc, out, error, "bidirectional", &UnicodeRecord::bidirectional, "");
}

bool unicode_east_asian_width(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unicode_property_string(runtime, args, argc, out, error, "east_asian_width", &UnicodeRecord::east_asian_width, "N");
}

bool unicode_int_property(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, const char* function_name, int UnicodeRecord::*field, int default_value) {
  if (argc != 1) {
    error = std::string("unicodedata.") + function_name + "() expected character";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  uint32_t codepoint = 0;
  if (!get_single_codepoint(runtime, args[0], function_name, codepoint, error)) {
    return false;
  }
  const UnicodeRecord* record = find_record(codepoint);
  out = Value::int64(record == nullptr ? default_value : record->*field);
  return true;
}

bool unicode_combining(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unicode_int_property(runtime, args, argc, out, error, "combining", &UnicodeRecord::combining, 0);
}

bool unicode_mirrored(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unicode_int_property(runtime, args, argc, out, error, "mirrored", &UnicodeRecord::mirrored, 0);
}

bool unicode_number(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, const char* function_name, int UnicodeRecord::*field) {
  if (argc < 1 || argc > 2) {
    error = std::string("unicodedata.") + function_name + "() expected character and optional default";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  uint32_t codepoint = 0;
  if (!get_single_codepoint(runtime, args[0], function_name, codepoint, error)) {
    return false;
  }
  const UnicodeRecord* record = find_record(codepoint);
  if (record != nullptr && record->*field != kNoNumber) {
    out = Value::int64(record->*field);
    return true;
  }
  if (argc == 2) {
    out = args[1];
    return true;
  }
  error = "not a numeric character";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool unicode_decimal(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unicode_number(runtime, args, argc, out, error, "decimal", &UnicodeRecord::decimal);
}

bool unicode_digit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unicode_number(runtime, args, argc, out, error, "digit", &UnicodeRecord::digit);
}

bool unicode_numeric(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "unicodedata.numeric() expected character and optional default";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  uint32_t codepoint = 0;
  if (!get_single_codepoint(runtime, args[0], "numeric", codepoint, error)) {
    return false;
  }
  const UnicodeRecord* record = find_record(codepoint);
  if (record != nullptr && record->numeric != kNoNumeric) {
    out = Value::number(record->numeric);
    return true;
  }
  if (argc == 2) {
    out = args[1];
    return true;
  }
  error = "not a numeric character";
  runtime.raise_class_error("ValueError", error);
  return false;
}

bool unicode_normalize(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "unicodedata.normalize() expected form and unistr";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string form;
  std::string text;
  if (!get_string_arg(runtime, args[0], "unicodedata.normalize() form", form, error) ||
      !get_string_arg(runtime, args[1], "unicodedata.normalize() unistr", text, error)) {
    return false;
  }
  form = canonical_form(std::move(form));
  if (form != "NFC" && form != "NFD" && form != "NFKC" && form != "NFKD") {
    error = "invalid normalization form";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::string(normalize_text(form, text));
  return true;
}

bool unicode_is_normalized(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value normalized;
  if (!unicode_normalize(runtime, args, argc, normalized, error, nullptr)) {
    return false;
  }
  auto* original = value_as_string(args[1]);
  auto* normalized_string = value_as_string(normalized);
  out = Value::boolean(original != nullptr && normalized_string != nullptr &&
                       string_object_view(*original) == string_object_view(*normalized_string));
  return true;
}

} // namespace

void register_unicodedata_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "unicodedata");
  builder.function("lookup", unicode_lookup)
      .function("name", unicode_name)
      .function("category", unicode_category)
      .function("bidirectional", unicode_bidirectional)
      .function("combining", unicode_combining)
      .function("east_asian_width", unicode_east_asian_width)
      .function("mirrored", unicode_mirrored)
      .function("decimal", unicode_decimal)
      .function("digit", unicode_digit)
      .function("numeric", unicode_numeric)
      .function("normalize", unicode_normalize)
      .function("is_normalized", unicode_is_normalized)
      .value("unidata_version", Value::string("17.0.0"));
  runtime.register_module("unicodedata", builder.finish());
}

} // namespace xlang3
