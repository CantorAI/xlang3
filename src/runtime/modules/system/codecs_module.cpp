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

namespace xlang3 {

namespace {

std::string normalize_encoding(std::string name) {
  for (char& ch : name) {
    if (ch == '-') {
      ch = '_';
    } else {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
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

bool codecs_lookup(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "codecs.lookup() expected encoding name";
    return false;
  }
  const std::string name = normalize_encoding(string_object_to_string(*value_as_string(args[0])));
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("codecs")});
  Value klass = Value::class_object("CodecInfo", std::move(attrs));
  out = Value::instance(klass);
  object_set_attr(out, "name", Value::string(name), error);
  return true;
}

bool codecs_encode(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "codecs.encode expected object and optional encoding/errors";
    return false;
  }
  std::string encoding = "utf_8";
  if (argc >= 2) {
    auto* enc = value_as_string(args[1]);
    if (enc != nullptr) {
      encoding = normalize_encoding(string_object_to_string(*enc));
    }
  }
  if (encoding == "hex" || encoding == "hex_codec") {
    std::string data;
    if (!value_bytes_text(args[0], data)) {
      error = "codecs.encode(..., 'hex') expected bytes";
      return false;
    }
    return hex_encode(data, out);
  }
  if (encoding == "utf_8" || encoding == "utf8" || encoding == "ascii") {
    std::string text;
    if (!value_text(args[0], text)) {
      error = "codecs.encode expected str";
      return false;
    }
    out = Value::bytes(std::move(text));
    return true;
  }
  error = "unknown encoding: " + encoding;
  return false;
}

bool codecs_decode(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "codecs.decode expected object and optional encoding/errors";
    return false;
  }
  std::string encoding = "utf_8";
  if (argc >= 2) {
    auto* enc = value_as_string(args[1]);
    if (enc != nullptr) {
      encoding = normalize_encoding(string_object_to_string(*enc));
    }
  }
  if (encoding == "hex" || encoding == "hex_codec") {
    std::string data;
    if (!value_bytes_text(args[0], data) && !value_text(args[0], data)) {
      error = "codecs.decode(..., 'hex') expected bytes-like or str";
      return false;
    }
    return hex_decode(data, out, error);
  }
  if (encoding == "utf_8" || encoding == "utf8" || encoding == "ascii") {
    std::string data;
    if (!value_bytes_text(args[0], data)) {
      error = "codecs.decode expected bytes-like";
      return false;
    }
    out = Value::string(std::move(data));
    return true;
  }
  error = "unknown encoding: " + encoding;
  return false;
}

} // namespace

void register_codecs_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "codecs");
  builder.function("lookup", codecs_lookup)
      .function("encode", codecs_encode)
      .function("decode", codecs_decode)
      .value("BOM_UTF8", Value::bytes(std::string("\xEF\xBB\xBF", 3)))
      .value("BOM", Value::bytes({}));
  runtime.register_module("codecs", builder.finish());
}

} // namespace xlang3
