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

#include <cctype>

namespace xlang3 {

namespace {

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = std::string(name) + " must be a string";
    return false;
  }
  out = reinterpret_cast<StringObject*>(value.as.obj)->value;
  return true;
}

bool bytes_decode_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 && argc != 2) {
    error = "bytes.decode expected 1 or 2 arguments, got " + std::to_string(argc);
    return false;
  }
  if (args[0].tag != ValueTag::Object || args[0].as.obj == nullptr || args[0].as.obj->kind != ObjectKind::Bytes) {
    error = "bytes.decode target is not bytes";
    return false;
  }
  if (argc == 2) {
    std::string encoding;
    if (!get_string_arg(args[1], "bytes.decode encoding", encoding, error)) {
      return false;
    }
    for (auto& ch : encoding) {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (encoding != "utf-8" && encoding != "utf8") {
      error = "only utf-8 encoding is supported";
      return false;
    }
  }
  out = Value::string(reinterpret_cast<BytesObject*>(args[0].as.obj)->value);
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
    target += bytes->value;
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

bool memoryview_tobytes_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "memoryview.tobytes", error)) {
    return false;
  }
  auto* view = value_as_memoryview(args[0]);
  if (view == nullptr) {
    error = "memoryview.tobytes target is not memoryview";
    return false;
  }
  std::string bytes;
  if (!append_bytes_from_value(bytes, args[0], error)) {
    return false;
  }
  out = Value::bytes(std::move(bytes));
  return true;
}

} // namespace

bool bytes_get_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag != ValueTag::Object || object.as.obj == nullptr || object.as.obj->kind != ObjectKind::Bytes) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"decode", "bytes.decode", bytes_decode_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

bool bytearray_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_bytearray(object) == nullptr) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"append", "bytearray.append", bytearray_append_method},
      {"extend", "bytearray.extend", bytearray_extend_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

bool memoryview_get_method(const Value& object, const std::string& name, Value& out) {
  if (value_as_memoryview(object) == nullptr) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"tobytes", "memoryview.tobytes", memoryview_tobytes_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
