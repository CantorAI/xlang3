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
#include "xlang3/value_hash.h"

#include <functional>

namespace xlang3 {

bool value_key_equal(const Value& lhs, const Value& rhs) {
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    return lhs.as.i64 == rhs.as.i64;
  }
  if ((lhs.tag == ValueTag::Int64 || lhs.tag == ValueTag::Double) &&
      (rhs.tag == ValueTag::Int64 || rhs.tag == ValueTag::Double)) {
    const double a = lhs.tag == ValueTag::Int64 ? static_cast<double>(lhs.as.i64) : lhs.as.f64;
    const double b = rhs.tag == ValueTag::Int64 ? static_cast<double>(rhs.as.i64) : rhs.as.f64;
    return a == b;
  }
  if (lhs.tag != rhs.tag) {
    return false;
  }
  switch (lhs.tag) {
    case ValueTag::Invalid:
    case ValueTag::None:
      return true;
    case ValueTag::Bool:
      return lhs.as.b == rhs.as.b;
    case ValueTag::Int64:
      return lhs.as.i64 == rhs.as.i64;
    case ValueTag::Double:
      return lhs.as.f64 == rhs.as.f64;
    case ValueTag::Object:
      if (lhs.as.obj == rhs.as.obj) {
        return true;
      }
      if (lhs.as.obj != nullptr && rhs.as.obj != nullptr &&
          lhs.as.obj->kind == ObjectKind::String && rhs.as.obj->kind == ObjectKind::String) {
        return string_object_view(*reinterpret_cast<StringObject*>(lhs.as.obj)) ==
               string_object_view(*reinterpret_cast<StringObject*>(rhs.as.obj));
      }
      if (lhs.as.obj != nullptr && rhs.as.obj != nullptr &&
          lhs.as.obj->kind == ObjectKind::Bytes && rhs.as.obj->kind == ObjectKind::Bytes) {
        return bytes_object_view(*reinterpret_cast<BytesObject*>(lhs.as.obj)) ==
               bytes_object_view(*reinterpret_cast<BytesObject*>(rhs.as.obj));
      }
      return false;
  }
  return false;
}

bool value_hash_key(const Value& value, size_t& out, std::string& error) {
  switch (value.tag) {
    case ValueTag::Invalid:
      error = "invalid value is not hashable";
      return false;
    case ValueTag::None:
      out = 0x9e3779b97f4a7c15ull;
      return true;
    case ValueTag::Bool:
      out = std::hash<bool>{}(value.as.b);
      return true;
    case ValueTag::Int64:
      out = std::hash<int64_t>{}(value.as.i64);
      return true;
    case ValueTag::Double:
      out = std::hash<double>{}(value.as.f64);
      return true;
    case ValueTag::Object:
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::String) {
        out = std::hash<std::string_view>{}(string_object_view(*reinterpret_cast<StringObject*>(value.as.obj)));
        return true;
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Bytes) {
        out = std::hash<std::string_view>{}(bytes_object_view(*reinterpret_cast<BytesObject*>(value.as.obj)));
        return true;
      }
      error = "object is not hashable";
      return false;
  }
  error = "value is not hashable";
  return false;
}

} // namespace xlang3
