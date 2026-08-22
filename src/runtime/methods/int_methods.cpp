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

#include <array>

namespace xlang3 {

namespace {

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

} // namespace

bool int_get_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag != ValueTag::Int64) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"to_bytes", "int.to_bytes", int_to_bytes_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
