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

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <zlib.h>

namespace xlang3 {

namespace {

bool binascii_bytes_arg(const Value& value, const char* name, std::string_view& out, std::string& owned, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_view(*bytes);
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = std::string_view(bytearray->value.data(), bytearray->value.size());
    return true;
  }
  if (auto* string = value_as_string(value)) {
    owned = string_object_to_string(*string);
    out = std::string_view(owned.data(), owned.size());
    return true;
  }
  error = std::string(name) + " must be bytes-like";
  return false;
}

bool binascii_bool_arg(const Value& value, bool default_value) {
  if (value.tag == ValueTag::Invalid || value.tag == ValueTag::None) {
    return default_value;
  }
  return value_truthy(value);
}

bool binascii_raise(Runtime& runtime, const char* class_name, std::string message, std::string& error) {
  error = std::move(message);
  runtime.raise_class_error(class_name, error);
  return false;
}

int hex_digit(unsigned char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

bool binascii_hexlify(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    return binascii_raise(runtime, "TypeError", "b2a_hex() expected data, optional sep, bytes_per_sep", error);
  }

  std::string owned;
  std::string_view input;
  if (!binascii_bytes_arg(args[0], "b2a_hex data", input, owned, error)) {
    return false;
  }

  int64_t bytes_per_sep = 1;
  std::string sep;
  bool use_sep = false;
  if (argc >= 2 && args[1].tag != ValueTag::None) {
    std::string sep_owned;
    std::string_view sep_view;
    if (!binascii_bytes_arg(args[1], "b2a_hex sep", sep_view, sep_owned, error)) {
      return false;
    }
    if (sep_view.size() != 1) {
      return binascii_raise(runtime, "ValueError", "sep must be length 1", error);
    }
    sep.assign(sep_view.data(), sep_view.size());
    use_sep = true;
  }
  if (argc >= 3 && args[2].tag == ValueTag::Int64) {
    bytes_per_sep = args[2].as.i64;
  }
  if (use_sep && bytes_per_sep == 0) {
    return binascii_raise(runtime, "ValueError", "bytes_per_sep cannot be zero", error);
  }

  static constexpr char kHex[] = "0123456789abcdef";
  std::string result;
  result.reserve(input.size() * 2 + (use_sep ? input.size() / 2 : 0));
  const uint64_t group = static_cast<uint64_t>(bytes_per_sep < 0 ? -bytes_per_sep : bytes_per_sep);
  for (size_t i = 0; i < input.size(); ++i) {
    if (use_sep && i != 0 && group != 0) {
      const size_t from_right = input.size() - i;
      const bool insert = bytes_per_sep > 0 ? (from_right % group == 0) : (i % group == 0);
      if (insert) {
        result += sep;
      }
    }
    const unsigned char ch = static_cast<unsigned char>(input[i]);
    result.push_back(kHex[ch >> 4]);
    result.push_back(kHex[ch & 0x0f]);
  }
  out = Value::bytes(std::move(result));
  return true;
}

bool binascii_unhexlify(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    return binascii_raise(runtime, "TypeError", "a2b_hex() expected one argument", error);
  }
  std::string owned;
  std::string_view input;
  if (!binascii_bytes_arg(args[0], "a2b_hex data", input, owned, error)) {
    return false;
  }
  if ((input.size() & 1u) != 0) {
    return binascii_raise(runtime, "binascii.Error", "Odd-length string", error);
  }
  std::string result;
  result.reserve(input.size() / 2);
  for (size_t i = 0; i < input.size(); i += 2) {
    const int hi = hex_digit(static_cast<unsigned char>(input[i]));
    const int lo = hex_digit(static_cast<unsigned char>(input[i + 1]));
    if (hi < 0 || lo < 0) {
      return binascii_raise(runtime, "binascii.Error", "Non-hexadecimal digit found", error);
    }
    result.push_back(static_cast<char>((hi << 4) | lo));
  }
  out = Value::bytes(std::move(result));
  return true;
}

bool binascii_crc32(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    return binascii_raise(runtime, "TypeError", "crc32() expected data and optional value", error);
  }
  std::string owned;
  std::string_view input;
  if (!binascii_bytes_arg(args[0], "crc32 data", input, owned, error)) {
    return false;
  }
  uint32_t seed = 0;
  if (argc == 2 && args[1].tag == ValueTag::Int64) {
    seed = static_cast<uint32_t>(args[1].as.i64);
  }
  const uLong result = ::crc32(seed, reinterpret_cast<const Bytef*>(input.data()), static_cast<uInt>(input.size()));
  out = Value::int64(static_cast<int64_t>(static_cast<uint32_t>(result)));
  return true;
}

std::array<int8_t, 256> make_base64_decode_table() {
  std::array<int8_t, 256> table{};
  table.fill(-1);
  const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; alphabet[i] != '\0'; ++i) {
    table[static_cast<unsigned char>(alphabet[i])] = static_cast<int8_t>(i);
  }
  return table;
}

bool binascii_b2a_base64_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    bool newline,
    Value& out,
    std::string& error) {
  if (argc < 1 || argc > 2) {
    return binascii_raise(runtime, "TypeError", "b2a_base64() expected data and optional newline", error);
  }
  std::string owned;
  std::string_view input;
  if (!binascii_bytes_arg(args[0], "b2a_base64 data", input, owned, error)) {
    return false;
  }
  if (argc >= 2) {
    newline = binascii_bool_arg(args[1], newline);
  }

  static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;
  result.reserve(((input.size() + 2) / 3) * 4 + (newline ? 1 : 0));
  for (size_t i = 0; i < input.size(); i += 3) {
    const uint32_t b0 = static_cast<unsigned char>(input[i]);
    const uint32_t b1 = i + 1 < input.size() ? static_cast<unsigned char>(input[i + 1]) : 0;
    const uint32_t b2 = i + 2 < input.size() ? static_cast<unsigned char>(input[i + 2]) : 0;
    result.push_back(kAlphabet[b0 >> 2]);
    result.push_back(kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
    result.push_back(i + 1 < input.size() ? kAlphabet[((b1 & 0x0f) << 2) | (b2 >> 6)] : '=');
    result.push_back(i + 2 < input.size() ? kAlphabet[b2 & 0x3f] : '=');
  }
  if (newline) {
    result.push_back('\n');
  }
  out = Value::bytes(std::move(result));
  return true;
}

bool binascii_b2a_base64(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return binascii_b2a_base64_impl(runtime, args, argc, true, out, error);
}

bool binascii_b2a_base64_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  bool newline = true;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (std::string_view(kwargs[i].name) != "newline") {
      return binascii_raise(runtime, "TypeError", "b2a_base64() got an unexpected keyword argument", error);
    }
    newline = binascii_bool_arg(*kwargs[i].value, newline);
  }
  return binascii_b2a_base64_impl(runtime, args, argc, newline, out, error);
}

bool binascii_a2b_base64_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    bool strict_mode,
    Value& out,
    std::string& error) {
  if (argc < 1 || argc > 2) {
    return binascii_raise(runtime, "TypeError", "a2b_base64() expected data and optional strict_mode", error);
  }
  std::string owned;
  std::string_view input;
  if (!binascii_bytes_arg(args[0], "a2b_base64 data", input, owned, error)) {
    return false;
  }
  if (argc >= 2) {
    strict_mode = binascii_bool_arg(args[1], strict_mode);
  }

  static const std::array<int8_t, 256> decode_table = make_base64_decode_table();
  std::string clean;
  clean.reserve(input.size());
  for (unsigned char ch : input) {
    if (std::isspace(ch) != 0 && !strict_mode) {
      continue;
    }
    if (ch == '=' || decode_table[ch] >= 0) {
      clean.push_back(static_cast<char>(ch));
      continue;
    }
    if (strict_mode) {
      return binascii_raise(runtime, "binascii.Error", "Only base64 data is allowed", error);
    }
  }
  if ((clean.size() % 4) != 0) {
    return binascii_raise(runtime, "binascii.Error", "Incorrect padding", error);
  }

  std::string result;
  result.reserve((clean.size() / 4) * 3);
  for (size_t i = 0; i < clean.size(); i += 4) {
    int pad = 0;
    uint32_t value = 0;
    for (size_t j = 0; j < 4; ++j) {
      const unsigned char ch = static_cast<unsigned char>(clean[i + j]);
      if (ch == '=') {
        ++pad;
        value <<= 6;
      } else {
        const int8_t decoded = decode_table[ch];
        if (decoded < 0 || pad != 0) {
          return binascii_raise(runtime, "binascii.Error", "Incorrect padding", error);
        }
        value = (value << 6) | static_cast<uint32_t>(decoded);
      }
    }
    if (pad > 2) {
      return binascii_raise(runtime, "binascii.Error", "Incorrect padding", error);
    }
    result.push_back(static_cast<char>((value >> 16) & 0xff));
    if (pad < 2) result.push_back(static_cast<char>((value >> 8) & 0xff));
    if (pad < 1) result.push_back(static_cast<char>(value & 0xff));
  }
  out = Value::bytes(std::move(result));
  return true;
}

bool binascii_a2b_base64(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return binascii_a2b_base64_impl(runtime, args, argc, false, out, error);
}

bool binascii_a2b_base64_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  bool strict_mode = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (std::string_view(kwargs[i].name) != "strict_mode") {
      return binascii_raise(runtime, "TypeError", "a2b_base64() got an unexpected keyword argument", error);
    }
    strict_mode = binascii_bool_arg(*kwargs[i].value, strict_mode);
  }
  return binascii_a2b_base64_impl(runtime, args, argc, strict_mode, out, error);
}

Value make_binascii_exception(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.emplace_back("__module__", Value::string("binascii"));
  attrs.emplace_back("__qualname__", Value::string(name));
  if (const Value* exception = runtime.find_builtin("Exception")) {
    return Value::class_object(name, std::move(attrs), *exception);
  }
  return Value::class_object(name, std::move(attrs));
}

} // namespace

void register_binascii_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "binascii");
  builder.value("Error", make_binascii_exception(runtime, "Error"))
      .value("Incomplete", make_binascii_exception(runtime, "Incomplete"))
      .value("crc32", runtime.make_native_function("binascii.crc32", binascii_crc32))
      .value("b2a_hex", runtime.make_native_function("binascii.b2a_hex", binascii_hexlify))
      .value("hexlify", runtime.make_native_function("binascii.hexlify", binascii_hexlify))
      .value("a2b_hex", runtime.make_native_function("binascii.a2b_hex", binascii_unhexlify))
      .value("unhexlify", runtime.make_native_function("binascii.unhexlify", binascii_unhexlify))
      .value("b2a_base64", runtime.make_native_function(
                               "binascii.b2a_base64",
                               binascii_b2a_base64,
                               nullptr,
                               nullptr,
                               nullptr,
                               false,
                               binascii_b2a_base64_kw))
      .value("a2b_base64", runtime.make_native_function(
                               "binascii.a2b_base64",
                               binascii_a2b_base64,
                               nullptr,
                               nullptr,
                               nullptr,
                               false,
                               binascii_a2b_base64_kw));
  runtime.register_module("binascii", builder.finish());
}

} // namespace xlang3
