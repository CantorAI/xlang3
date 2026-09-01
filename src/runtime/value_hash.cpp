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

#include "xlang3/object_model.h"
#include "xlang3/set_object.h"

#include <functional>

namespace xlang3 {

namespace {

struct HashBinaryView {
  const char* data = nullptr;
  size_t size = 0;
  bool readonly = true;
};

HashBinaryView hash_binary_view(const Value& value) {
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    return {view.data(), view.size(), true};
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    return {bytearray->value.data(), bytearray->value.size(), false};
  }
  if (auto* memoryview = value_as_memoryview(value)) {
    if (memoryview->released) {
      return {};
    }
    const auto owner = hash_binary_view(memoryview->owner);
    if (owner.data == nullptr || memoryview->offset > owner.size || owner.size - memoryview->offset < memoryview->size) {
      return {};
    }
    return {owner.data + memoryview->offset, memoryview->size, memoryview->readonly && owner.readonly};
  }
  return {};
}

bool is_binary_like_value(const Value& value) {
  return value_as_bytes(value) != nullptr || value_as_bytearray(value) != nullptr || value_as_memoryview(value) != nullptr;
}

bool string_payload_view(const Value& value, std::string_view& out) {
  if (auto* string = value_as_string(value)) {
    out = string_object_view(*string);
    return true;
  }
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return false;
  }
  for (const auto& attr : instance->attrs) {
    if (attr.first == "__xlang3_string_value__") {
      if (auto* string = value_as_string(attr.second)) {
        out = string_object_view(*string);
        return true;
      }
      return false;
    }
  }
  return false;
}

bool int_payload_value(const Value& value, int64_t& out) {
  if (value_int_like_to_i64(value, out)) {
    return true;
  }
  auto* instance = value_as_instance(value);
  if (instance == nullptr) {
    return false;
  }
  for (const auto& attr : instance->attrs) {
    if (attr.first == "__xlang3_int_value__") {
      if (attr.second.tag == ValueTag::Int64) {
        out = attr.second.as.i64;
        return true;
      }
      if (attr.second.tag == ValueTag::Bool) {
        out = attr.second.as.b ? 1 : 0;
        return true;
      }
      return false;
    }
  }
  return false;
}

} // namespace

bool value_key_equal(const Value& lhs, const Value& rhs) {
  if (lhs.tag == ValueTag::Bool || rhs.tag == ValueTag::Bool ||
      lhs.tag == ValueTag::Int64 || rhs.tag == ValueTag::Int64 ||
      value_as_bigint(lhs) != nullptr || value_as_bigint(rhs) != nullptr) {
    Value equal;
    if (value_int_like_compare("==", lhs, rhs, equal) && equal.tag == ValueTag::Bool) {
      return equal.as.b;
    }
  }
  int64_t left_int = 0;
  int64_t right_int = 0;
  if (int_payload_value(lhs, left_int) && int_payload_value(rhs, right_int)) {
    return left_int == right_int;
  }
  std::string_view left_string;
  std::string_view right_string;
  if (string_payload_view(lhs, left_string) && string_payload_view(rhs, right_string)) {
    return left_string == right_string;
  }
  if (lhs.tag == ValueTag::Bool && rhs.tag == ValueTag::Int64) {
    return (lhs.as.b ? 1 : 0) == rhs.as.i64;
  }
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Bool) {
    return lhs.as.i64 == (rhs.as.b ? 1 : 0);
  }
  if (lhs.tag == ValueTag::Int64 && rhs.tag == ValueTag::Int64) {
    return lhs.as.i64 == rhs.as.i64;
  }
  if ((lhs.tag == ValueTag::Int64 || lhs.tag == ValueTag::Double) &&
      (rhs.tag == ValueTag::Int64 || rhs.tag == ValueTag::Double)) {
    const double a = lhs.tag == ValueTag::Int64 ? static_cast<double>(lhs.as.i64) : lhs.as.f64;
    const double b = rhs.tag == ValueTag::Int64 ? static_cast<double>(rhs.as.i64) : rhs.as.f64;
    return a == b;
  }
  if (is_binary_like_value(lhs) && is_binary_like_value(rhs)) {
    const auto left = hash_binary_view(lhs);
    const auto right = hash_binary_view(rhs);
    return left.data != nullptr && right.data != nullptr && left.size == right.size &&
           (left.size == 0 || std::char_traits<char>::compare(left.data, right.data, left.size) == 0);
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
      if (lhs.as.obj != nullptr && rhs.as.obj != nullptr &&
          lhs.as.obj->kind == ObjectKind::Tuple && rhs.as.obj->kind == ObjectKind::Tuple) {
        const auto* left = reinterpret_cast<TupleObject*>(lhs.as.obj);
        const auto* right = reinterpret_cast<TupleObject*>(rhs.as.obj);
        if (left->items.size() != right->items.size()) {
          return false;
        }
        for (size_t i = 0; i < left->items.size(); ++i) {
          if (!value_key_equal(left->items[i], right->items[i])) {
            return false;
          }
        }
        return true;
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
      out = std::hash<int64_t>{}(value.as.b ? 1 : 0);
      return true;
    case ValueTag::Int64:
      out = std::hash<int64_t>{}(value.as.i64);
      return true;
    case ValueTag::Double:
      out = std::hash<double>{}(value.as.f64);
      return true;
    case ValueTag::Object:
      {
        if (value_int_like_hash(value, out)) {
          return true;
        }
        int64_t int_payload = 0;
        if (int_payload_value(value, int_payload)) {
          out = std::hash<int64_t>{}(int_payload);
          return true;
        }
        std::string_view string_payload;
        if (string_payload_view(value, string_payload)) {
          out = std::hash<std::string_view>{}(string_payload);
          return true;
        }
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::String) {
        out = std::hash<std::string_view>{}(string_object_view(*reinterpret_cast<StringObject*>(value.as.obj)));
        return true;
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Bytes) {
        out = std::hash<std::string_view>{}(bytes_object_view(*reinterpret_cast<BytesObject*>(value.as.obj)));
        return true;
      }
      if (auto* view = value_as_memoryview(value)) {
        const auto bytes = hash_binary_view(value);
        if (view->released || bytes.data == nullptr) {
          error = "operation forbidden on released memoryview object";
          return false;
        }
        if (!bytes.readonly || (view->format != "B" && view->format != "b" && view->format != "c")) {
          error = "unhashable type: 'memoryview'";
          return false;
        }
        out = std::hash<std::string_view>{}(std::string_view(bytes.data, bytes.size));
        return true;
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Tuple) {
        const auto* tuple = reinterpret_cast<TupleObject*>(value.as.obj);
        size_t hash = 0x345678ul;
        for (const auto& item : tuple->items) {
          size_t item_hash = 0;
          if (!value_hash_key(item, item_hash, error)) {
            return false;
          }
          hash = (hash ^ item_hash) * 1000003ul;
          hash ^= tuple->items.size();
        }
        out = hash == static_cast<size_t>(-1) ? static_cast<size_t>(-2) : hash;
        return true;
      }
      if (value.as.obj != nullptr) {
        switch (value.as.obj->kind) {
          case ObjectKind::ByteArray:
          case ObjectKind::List:
          case ObjectKind::Dict:
          case ObjectKind::Set:
            if (auto* set = value_as_set(value); set != nullptr && set->frozen) {
              size_t hash = 0x2f4f0f1f0e0d0c0bull;
              for (const auto& item : set->items) {
                size_t item_hash = 0;
                if (!value_hash_key(item, item_hash, error)) {
                  return false;
                }
                hash ^= item_hash + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
              }
              out = hash == static_cast<size_t>(-1) ? static_cast<size_t>(-2) : hash;
              return true;
            }
            error = "object is not hashable";
            return false;
          case ObjectKind::DictKeysView:
          case ObjectKind::DictValuesView:
          case ObjectKind::DictItemsView:
            error = "object is not hashable";
            return false;
          default:
            out = std::hash<const void*>{}(value.as.obj);
            return true;
        }
      }
      error = "object is not hashable";
      return false;
  }
  error = "value is not hashable";
  return false;
}

} // namespace xlang3
