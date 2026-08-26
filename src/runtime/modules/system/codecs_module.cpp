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
#include "xlang3/object_model.h"

#include <cctype>
#include <unordered_map>
#include <string_view>

namespace xlang3 {

namespace {

std::string normalize_encoding(std::string name) {
  for (char& ch : name) {
    if (ch == '-' || ch == ' ' || ch == '.') {
      ch = '_';
    } else {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
  }
  return name;
}

std::string canonical_encoding(std::string name) {
  name = normalize_encoding(std::move(name));
  if (name == "utf8" || name == "u8" || name == "cp65001") {
    return "utf_8";
  }
  if (name == "utf_8_sig") {
    return "utf_8_sig";
  }
  if (name == "latin1" || name == "latin_1" || name == "iso8859_1" || name == "iso_8859_1" || name == "8859") {
    return "latin_1";
  }
  if (name == "us_ascii" || name == "646") {
    return "ascii";
  }
  if (name == "hex_codec") {
    return "hex";
  }
  return name;
}

bool value_text(const Value& value, std::string& out) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  return false;
}

bool value_bytes_text(const Value& value, std::string& out) {
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  return false;
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

std::string latin1_decode_text(std::string_view text) {
  std::string decoded;
  decoded.reserve(text.size() * 2);
  for (unsigned char ch : text) {
    append_utf8(ch, decoded);
  }
  return decoded;
}

std::string ascii_encode_text(Runtime& runtime, std::string_view text, const std::string& errors, std::string& error) {
  std::string encoded;
  encoded.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch < 128) {
      encoded.push_back(static_cast<char>(ch));
      ++i;
      continue;
    }
    const size_t width = utf8_codepoint_width(ch);
    const uint32_t codepoint = width == 0 || i + width > text.size() ? ch : decode_utf8_codepoint(text.substr(i), width);
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
      return {};
    }
  }
  return encoded;
}

std::string ascii_decode_text(Runtime& runtime, std::string_view text, const std::string& errors, std::string& error) {
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
      return {};
    }
  }
  return decoded;
}

std::string normalized_errors(const Value* args, uint32_t argc, uint32_t index) {
  if (index >= argc) {
    return "strict";
  }
  auto* errors_value = value_as_string(args[index]);
  if (errors_value == nullptr) {
    return "strict";
  }
  return normalize_encoding(string_object_to_string(*errors_value));
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + ch - 'a';
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + ch - 'A';
  }
  return -1;
}

bool hex_encode(const std::string& data, Value& out) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(data.size() * 2);
  for (unsigned char ch : data) {
    hex.push_back(digits[(ch >> 4) & 0x0f]);
    hex.push_back(digits[ch & 0x0f]);
  }
  out = Value::bytes(std::move(hex));
  return true;
}

bool hex_decode(const std::string& data, Value& out, std::string& error) {
  std::string bytes;
  bytes.reserve(data.size() / 2);
  int high = -1;
  for (char ch : data) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      continue;
    }
    const int digit = hex_value(ch);
    if (digit < 0) {
      error = "non-hexadecimal digit found";
      return false;
    }
    if (high < 0) {
      high = digit;
    } else {
      bytes.push_back(static_cast<char>((high << 4) | digit));
      high = -1;
    }
  }
  if (high >= 0) {
    error = "odd-length string";
    return false;
  }
  out = Value::bytes(std::move(bytes));
  return true;
}

void string_user_data_cleanup(void* data) {
  delete static_cast<std::string*>(data);
}

bool encode_with_codec(Runtime& runtime, const Value& value, const std::string& encoding, const std::string& errors, Value& out, std::string& error) {
  if (encoding == "hex") {
    std::string data;
    if (!value_bytes_text(value, data)) {
      error = "codecs.encode(..., 'hex') expected bytes";
      return false;
    }
    return hex_encode(data, out);
  }
  if (encoding == "utf_8" || encoding == "utf_8_sig" || encoding == "ascii" || encoding == "latin_1") {
    std::string text;
    if (!value_text(value, text)) {
      error = "codecs.encode expected str";
      return false;
    }
    if (encoding == "ascii") {
      std::string encoded = ascii_encode_text(runtime, text, errors, error);
      if (!error.empty()) {
        return false;
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "latin_1") {
      std::string encoded = latin1_encode_text(runtime, text, errors, error);
      if (!error.empty()) {
        return false;
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "utf_8_sig") {
      out = Value::bytes(std::string("\xef\xbb\xbf", 3) + text);
    } else {
      out = Value::bytes(std::move(text));
    }
    return true;
  }
  error = "unknown encoding: " + encoding;
  return false;
}

bool decode_with_codec(Runtime& runtime, const Value& value, const std::string& encoding, const std::string& errors, Value& out, std::string& error) {
  if (encoding == "hex") {
    std::string data;
    if (!value_bytes_text(value, data) && !value_text(value, data)) {
      error = "codecs.decode(..., 'hex') expected bytes-like or str";
      return false;
    }
    return hex_decode(data, out, error);
  }
  if (encoding == "utf_8" || encoding == "utf_8_sig" || encoding == "ascii" || encoding == "latin_1") {
    std::string data;
    if (!value_bytes_text(value, data)) {
      error = "codecs.decode expected bytes-like";
      return false;
    }
    if (encoding == "ascii") {
      std::string decoded = ascii_decode_text(runtime, data, errors, error);
      if (!error.empty()) {
        return false;
      }
      out = Value::string(std::move(decoded));
    } else if (encoding == "latin_1") {
      out = Value::string(latin1_decode_text(data));
    } else if (encoding == "utf_8_sig") {
      if (data.size() >= 3 &&
          static_cast<unsigned char>(data[0]) == 0xef &&
          static_cast<unsigned char>(data[1]) == 0xbb &&
          static_cast<unsigned char>(data[2]) == 0xbf) {
        data.erase(0, 3);
      }
      out = Value::string(std::move(data));
    } else {
      out = Value::string(std::move(data));
    }
    return true;
  }
  error = "unknown encoding: " + encoding;
  return false;
}

bool codecs_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "codecs.encode expected object and optional encoding/errors";
    return false;
  }
  std::string encoding = "utf_8";
  if (argc >= 2) {
    auto* enc = value_as_string(args[1]);
    if (enc != nullptr) {
      encoding = canonical_encoding(string_object_to_string(*enc));
    }
  }
  return encode_with_codec(runtime, args[0], encoding, normalized_errors(args, argc, 2), out, error);
}

bool codecs_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "codecs.decode expected object and optional encoding/errors";
    return false;
  }
  std::string encoding = "utf_8";
  if (argc >= 2) {
    auto* enc = value_as_string(args[1]);
    if (enc != nullptr) {
      encoding = canonical_encoding(string_object_to_string(*enc));
    }
  }
  return decode_with_codec(runtime, args[0], encoding, normalized_errors(args, argc, 2), out, error);
}

bool codec_info_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "CodecInfo.encode expected object and optional errors";
    return false;
  }
  const auto* encoding = static_cast<const std::string*>(user_data);
  Value encoded;
  if (!encode_with_codec(runtime, args[0], encoding == nullptr ? "utf_8" : *encoding, normalized_errors(args, argc, 1), encoded, error)) {
    return false;
  }
  auto* text = value_as_string(args[0]);
  out = Value::tuple(
      {encoded, Value::int64(text == nullptr ? 0 : static_cast<int64_t>(utf8_codepoint_count(string_object_view(*text))))});
  return true;
}

bool codec_info_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "CodecInfo.decode expected object and optional errors";
    return false;
  }
  const auto* encoding = static_cast<const std::string*>(user_data);
  Value decoded;
  if (!decode_with_codec(runtime, args[0], encoding == nullptr ? "utf_8" : *encoding, normalized_errors(args, argc, 1), decoded, error)) {
    return false;
  }
  std::string bytes;
  value_bytes_text(args[0], bytes);
  out = Value::tuple({decoded, Value::int64(static_cast<int64_t>(bytes.size()))});
  return true;
}

bool codecs_lookup(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "codecs.lookup() expected encoding name";
    return false;
  }
  const std::string name = canonical_encoding(string_object_to_string(*value_as_string(args[0])));
  if (name != "utf_8" && name != "utf_8_sig" && name != "ascii" && name != "latin_1" && name != "hex") {
    error = "unknown encoding: " + name;
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("codecs")});
  Value klass = Value::class_object("CodecInfo", std::move(attrs));
  out = Value::instance(klass);
  object_set_attr(out, "name", Value::string(name), error);
  object_set_attr(
      out,
      "encode",
      runtime.make_native_function("codecs.CodecInfo.encode", codec_info_encode, new std::string(name), string_user_data_cleanup),
      error);
  object_set_attr(
      out,
      "decode",
      runtime.make_native_function("codecs.CodecInfo.decode", codec_info_decode, new std::string(name), string_user_data_cleanup),
      error);
  return true;
}

bool codecs_getencoder(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value info;
  if (!codecs_lookup(runtime, args, argc, info, error, nullptr)) {
    return false;
  }
  return object_get_attr(info, "encode", out, error);
}

bool codecs_getdecoder(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value info;
  if (!codecs_lookup(runtime, args, argc, info, error, nullptr)) {
    return false;
  }
  return object_get_attr(info, "decode", out, error);
}

std::unordered_map<std::string, Value>& error_handler_registry() {
  static std::unordered_map<std::string, Value> handlers;
  return handlers;
}

bool codecs_strict_errors(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "strict error handler re-raises codec exceptions";
  runtime.raise_class_error("UnicodeError", error);
  return false;
}

bool codecs_ignore_errors(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::tuple({Value::string(""), Value::int64(0)});
  return true;
}

bool codecs_replace_errors(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::tuple({Value::string("\xef\xbf\xbd"), Value::int64(0)});
  return true;
}

bool codecs_lookup_error(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "codecs.lookup_error() expected error handler name";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string name = normalize_encoding(string_object_to_string(*value_as_string(args[0])));
  if (name == "strict") {
    out = runtime.make_native_function("codecs.strict_errors", codecs_strict_errors);
    return true;
  }
  if (name == "ignore") {
    out = runtime.make_native_function("codecs.ignore_errors", codecs_ignore_errors);
    return true;
  }
  if (name == "replace") {
    out = runtime.make_native_function("codecs.replace_errors", codecs_replace_errors);
    return true;
  }
  auto it = error_handler_registry().find(name);
  if (it != error_handler_registry().end()) {
    out = it->second;
    return true;
  }
  error = "unknown error handler name '" + name + "'";
  runtime.raise_class_error("LookupError", error);
  return false;
}

bool codecs_register_error(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || value_as_string(args[0]) == nullptr) {
    error = "codecs.register_error() expected name and handler";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string name = normalize_encoding(string_object_to_string(*value_as_string(args[0])));
  error_handler_registry()[name] = args[1];
  out = Value::none();
  return true;
}

} // namespace

void register_codecs_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "codecs");
  builder.function("lookup", codecs_lookup)
      .function("encode", codecs_encode)
      .function("decode", codecs_decode)
      .function("getencoder", codecs_getencoder)
      .function("getdecoder", codecs_getdecoder)
      .function("lookup_error", codecs_lookup_error)
      .function("register_error", codecs_register_error)
      .value("BOM_UTF8", Value::bytes(std::string("\xEF\xBB\xBF", 3)))
      .value("BOM", Value::bytes({}));
  runtime.register_module("codecs", builder.finish());
}

} // namespace xlang3
