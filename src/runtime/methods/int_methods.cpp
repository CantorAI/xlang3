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
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include <array>
#include <limits>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

bool int_bit_length_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "int.bit_length expected no arguments";
    return false;
  }
  if (args[0].tag != ValueTag::Int64) {
    error = "int.bit_length target must be int";
    return false;
  }
  uint64_t value = args[0].as.i64 < 0
      ? static_cast<uint64_t>(-(args[0].as.i64 + 1)) + 1u
      : static_cast<uint64_t>(args[0].as.i64);
  int64_t bits = 0;
  while (value != 0) {
    ++bits;
    value >>= 1u;
  }
  value_set_int64(out, bits);
  return true;
}

bool int_to_bytes_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "int.to_bytes expected value, length, and byteorder";
    return false;
  }
  if (args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64) {
    error = "int.to_bytes value and length must be int";
    return false;
  }
  auto* order = value_as_string(args[2]);
  if (order == nullptr) {
    error = "int.to_bytes byteorder must be str";
    return false;
  }
  const std::string byteorder = string_object_to_string(*order);
  if (byteorder != "little" && byteorder != "big") {
    error = "byteorder must be either 'little' or 'big'";
    return false;
  }
  if (args[1].as.i64 < 0 || args[1].as.i64 > 8) {
    error = "int.to_bytes length is out of supported range";
    return false;
  }
  uint64_t value = static_cast<uint64_t>(args[0].as.i64);
  const uint32_t length = static_cast<uint32_t>(args[1].as.i64);
  std::string bytes;
  bytes.resize(length);
  for (uint32_t i = 0; i < length; ++i) {
    const uint32_t shift_index = byteorder == "little" ? i : (length - 1 - i);
    bytes[i] = static_cast<char>((value >> (shift_index * 8)) & 0xffu);
  }
  out = Value::bytes(bytes);
  return true;
}

bool byteorder_is_big(const Value& value, bool& is_big, std::string& error) {
  auto* order = value_as_string(value);
  if (order == nullptr) {
    error = "byteorder must be str";
    return false;
  }
  const std::string_view byteorder = string_object_view(*order);
  if (byteorder == "big") {
    is_big = true;
    return true;
  }
  if (byteorder == "little") {
    is_big = false;
    return true;
  }
  error = "byteorder must be either 'little' or 'big'";
  return false;
}

bool bool_arg(const Value& value, bool& out) {
  if (value.tag == ValueTag::Bool) {
    out = value.as.b;
    return true;
  }
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64 != 0;
    return true;
  }
  return false;
}

bool collect_from_bytes_input(const Value& value, std::vector<uint8_t>& bytes, std::string& error) {
  if (auto* object = value_as_bytes(value)) {
    const auto view = bytes_object_view(*object);
    bytes.assign(view.begin(), view.end());
    return true;
  }
  if (auto* object = value_as_bytearray(value)) {
    bytes.assign(object->value.begin(), object->value.end());
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    if (view->released) {
      error = "operation forbidden on released memoryview object";
      return false;
    }
    std::string_view data;
    if (auto* owner = value_as_bytes(view->owner)) {
      data = bytes_object_view(*owner);
    } else if (auto* owner = value_as_bytearray(view->owner)) {
      data = owner->value;
    } else {
      error = "memoryview owner is not byte-addressable";
      return false;
    }
    if (view->offset > data.size() || view->size > data.size() - view->offset) {
      error = "memoryview slice is out of range";
      return false;
    }
    data = data.substr(view->offset, view->size);
    bytes.assign(data.begin(), data.end());
    return true;
  }

  Value iterator;
  if (!sequence_get_iter(value, iterator, error)) {
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
    if (item.tag != ValueTag::Int64 || item.as.i64 < 0 || item.as.i64 > 255) {
      error = "bytes must be in range(0, 256)";
      return false;
    }
    bytes.push_back(static_cast<uint8_t>(item.as.i64));
  }
}

bool int_from_bytes_impl(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (argc < 1 || argc > 2) {
    error = "int.from_bytes expected bytes and optional byteorder";
    return false;
  }

  bool is_big = true;
  bool signed_value = false;
  if (argc >= 2 && !byteorder_is_big(args[1], is_big, error)) {
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (name == "byteorder") {
      if (argc >= 2) {
        error = "int.from_bytes got multiple values for argument 'byteorder'";
        return false;
      }
      if (!byteorder_is_big(*kwargs[i].value, is_big, error)) {
        return false;
      }
    } else if (name == "signed") {
      if (!bool_arg(*kwargs[i].value, signed_value)) {
        error = "signed must be bool";
        return false;
      }
    } else {
      error = "int.from_bytes got an unexpected keyword argument '" + std::string(name) + "'";
      return false;
    }
  }

  std::vector<uint8_t> bytes;
  if (!collect_from_bytes_input(args[0], bytes, error)) {
    return false;
  }
  if (bytes.size() > 8) {
    error = "int too large to convert";
    return false;
  }

  uint64_t raw = 0;
  for (size_t i = 0; i < bytes.size(); ++i) {
    const size_t index = is_big ? i : (bytes.size() - 1 - i);
    raw = (raw << 8u) | static_cast<uint64_t>(bytes[index]);
  }

  if (signed_value && !bytes.empty()) {
    const uint8_t sign_byte = is_big ? bytes.front() : bytes.back();
    if ((sign_byte & 0x80u) != 0) {
      const uint64_t bits = static_cast<uint64_t>(bytes.size() * 8u);
      if (bits < 64u) {
        const uint64_t mask = (uint64_t{1} << bits) - 1u;
        raw = (~raw + 1u) & mask;
        value_set_int64(out, -static_cast<int64_t>(raw));
        return true;
      }
      value_set_int64(out, static_cast<int64_t>(raw));
      return true;
    }
  }
  if (raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    error = "int too large to convert";
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(raw));
  return true;
}

bool int_from_bytes_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return int_from_bytes_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool int_from_bytes_kw_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return int_from_bytes_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

} // namespace

bool int_get_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag != ValueTag::Int64) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"bit_length", "int.bit_length", int_bit_length_method},
      {"to_bytes", "int.to_bytes", int_to_bytes_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

bool int_install_class_methods(Runtime& runtime, ClassObject& int_class) {
  int_class.attrs["from_bytes"] = Value::static_method(runtime.make_native_function(
      "int.from_bytes",
      int_from_bytes_method,
      nullptr,
      nullptr,
      nullptr,
      false,
      int_from_bytes_kw_method));
  ++int_class.version;
  return true;
}

} // namespace xlang3
