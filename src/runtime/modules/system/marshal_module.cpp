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

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

constexpr std::string_view kMagic = "X3M1";

enum class MarshalTag : char {
  NoneValue = 'N',
  FalseValue = 'F',
  TrueValue = 'T',
  Int64 = 'i',
  Float64 = 'f',
  String = 's',
  Bytes = 'b',
  List = 'l',
  Tuple = 't',
  Dict = 'd',
  Set = 'e',
};

void append_u32(std::string& out, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
  }
}

void append_u64(std::string& out, uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
  }
}

void append_bytes(std::string& out, std::string_view bytes) {
  append_u32(out, static_cast<uint32_t>(bytes.size()));
  out.append(bytes.data(), bytes.size());
}

bool marshal_value(const Value& value, std::string& out, std::string& error) {
  switch (value.tag) {
  case ValueTag::None:
    out.push_back(static_cast<char>(MarshalTag::NoneValue));
    return true;
  case ValueTag::Bool:
    out.push_back(static_cast<char>(value.as.b ? MarshalTag::TrueValue : MarshalTag::FalseValue));
    return true;
  case ValueTag::Int64:
    out.push_back(static_cast<char>(MarshalTag::Int64));
    append_u64(out, static_cast<uint64_t>(value.as.i64));
    return true;
  case ValueTag::Double: {
    out.push_back(static_cast<char>(MarshalTag::Float64));
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value.as.f64));
    std::memcpy(&bits, &value.as.f64, sizeof(bits));
    append_u64(out, bits);
    return true;
  }
  case ValueTag::Object:
    break;
  case ValueTag::Invalid:
    error = "cannot marshal invalid value";
    return false;
  }

  if (auto* string = value_as_string(value)) {
    out.push_back(static_cast<char>(MarshalTag::String));
    append_bytes(out, string_object_view(*string));
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    out.push_back(static_cast<char>(MarshalTag::Bytes));
    append_bytes(out, bytes_object_view(*bytes));
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out.push_back(static_cast<char>(MarshalTag::Bytes));
    append_bytes(out, bytearray->value);
    return true;
  }
  if (auto* list = value_as_list(value)) {
    out.push_back(static_cast<char>(MarshalTag::List));
    append_u32(out, static_cast<uint32_t>(list->items.size()));
    for (const auto& item : list->items) {
      if (!marshal_value(item, out, error)) {
        return false;
      }
    }
    return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    out.push_back(static_cast<char>(MarshalTag::Tuple));
    append_u32(out, static_cast<uint32_t>(tuple->items.size()));
    for (const auto& item : tuple->items) {
      if (!marshal_value(item, out, error)) {
        return false;
      }
    }
    return true;
  }
  if (auto* dict = value_as_dict(value)) {
    out.push_back(static_cast<char>(MarshalTag::Dict));
    append_u32(out, static_cast<uint32_t>(dict->entries.size()));
    for (const auto& entry : dict->entries) {
      if (!marshal_value(entry.first, out, error) || !marshal_value(entry.second, out, error)) {
        return false;
      }
    }
    return true;
  }
  if (auto* set = value_as_set(value)) {
    out.push_back(static_cast<char>(MarshalTag::Set));
    append_u32(out, static_cast<uint32_t>(set->items.size()));
    for (const auto& item : set->items) {
      if (!marshal_value(item, out, error)) {
        return false;
      }
    }
    return true;
  }

  error = "unsupported marshal value";
  return false;
}

struct MarshalReader {
  std::string_view data;
  size_t pos = 0;

  bool read_byte(char& out) {
    if (pos >= data.size()) {
      return false;
    }
    out = data[pos++];
    return true;
  }

  bool read_u32(uint32_t& out) {
    if (data.size() - pos < 4) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 4; ++i) {
      out |= static_cast<uint32_t>(static_cast<unsigned char>(data[pos++])) << (i * 8);
    }
    return true;
  }

  bool read_u64(uint64_t& out) {
    if (data.size() - pos < 8) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<uint64_t>(static_cast<unsigned char>(data[pos++])) << (i * 8);
    }
    return true;
  }

  bool read_bytes(std::string_view& out) {
    uint32_t size = 0;
    if (!read_u32(size) || data.size() - pos < size) {
      return false;
    }
    out = data.substr(pos, size);
    pos += size;
    return true;
  }
};

bool unmarshal_value(MarshalReader& reader, Value& out, std::string& error) {
  char tag = 0;
  if (!reader.read_byte(tag)) {
    error = "bad marshal data";
    return false;
  }
  switch (static_cast<MarshalTag>(tag)) {
  case MarshalTag::NoneValue:
    value_set_none(out);
    return true;
  case MarshalTag::FalseValue:
    out = Value::boolean(false);
    return true;
  case MarshalTag::TrueValue:
    out = Value::boolean(true);
    return true;
  case MarshalTag::Int64: {
    uint64_t raw = 0;
    if (!reader.read_u64(raw)) {
      error = "bad marshal int";
      return false;
    }
    out = Value::int64(static_cast<int64_t>(raw));
    return true;
  }
  case MarshalTag::Float64: {
    uint64_t raw = 0;
    if (!reader.read_u64(raw)) {
      error = "bad marshal float";
      return false;
    }
    double number = 0.0;
    std::memcpy(&number, &raw, sizeof(number));
    out = Value::number(number);
    return true;
  }
  case MarshalTag::String: {
    std::string_view bytes;
    if (!reader.read_bytes(bytes)) {
      error = "bad marshal string";
      return false;
    }
    out = Value::string(std::string(bytes));
    return true;
  }
  case MarshalTag::Bytes: {
    std::string_view bytes;
    if (!reader.read_bytes(bytes)) {
      error = "bad marshal bytes";
      return false;
    }
    out = Value::bytes(std::string(bytes));
    return true;
  }
  case MarshalTag::List:
  case MarshalTag::Tuple:
  case MarshalTag::Set: {
    uint32_t count = 0;
    if (!reader.read_u32(count)) {
      error = "bad marshal sequence";
      return false;
    }
    std::vector<Value> items;
    items.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      Value item;
      if (!unmarshal_value(reader, item, error)) {
        return false;
      }
      items.push_back(std::move(item));
    }
    if (static_cast<MarshalTag>(tag) == MarshalTag::List) {
      out = Value::list(std::move(items));
    } else if (static_cast<MarshalTag>(tag) == MarshalTag::Tuple) {
      out = Value::tuple(std::move(items));
    } else {
      out = Value::set(std::move(items));
    }
    return true;
  }
  case MarshalTag::Dict: {
    uint32_t count = 0;
    if (!reader.read_u32(count)) {
      error = "bad marshal dict";
      return false;
    }
    std::vector<std::pair<Value, Value>> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      Value key;
      Value value;
      if (!unmarshal_value(reader, key, error) || !unmarshal_value(reader, value, error)) {
        return false;
      }
      entries.push_back({std::move(key), std::move(value)});
    }
    out = Value::dict(std::move(entries));
    return true;
  }
  default:
    error = "unknown marshal type";
    return false;
  }
}

bool get_data_bytes(const Value& value, std::string_view& out, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_view(*bytes);
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  if (auto* string = value_as_string(value)) {
    out = string_object_view(*string);
    return true;
  }
  error = "marshal.loads() expected bytes-like object";
  return false;
}

bool marshal_dumps(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "marshal.dumps() expected value and optional version";
    return false;
  }
  std::string data(kMagic);
  if (!marshal_value(args[0], data, error)) {
    return false;
  }
  out = Value::bytes(std::move(data));
  return true;
}

bool marshal_loads(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "marshal.loads() expected data";
    return false;
  }
  std::string_view data;
  if (!get_data_bytes(args[0], data, error)) {
    return false;
  }
  if (data.size() < kMagic.size() || data.substr(0, kMagic.size()) != kMagic) {
    error = "bad marshal data";
    return false;
  }
  MarshalReader reader{data.substr(kMagic.size()), 0};
  if (!unmarshal_value(reader, out, error)) {
    return false;
  }
  if (reader.pos != reader.data.size()) {
    error = "trailing marshal data";
    return false;
  }
  return true;
}

bool marshal_dump(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "marshal.dump() expected value, file, and optional version";
    return false;
  }
  Value data;
  if (!marshal_dumps(runtime, args, argc == 3 ? 2 : 1, data, error, nullptr)) {
    return false;
  }
  Value write;
  if (!attribute_get(args[1], "write", write, error)) {
    return false;
  }
  Value ignored;
  if (!runtime_call_callable(runtime, write, &data, 1, ignored, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool marshal_load(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "marshal.load() expected file";
    return false;
  }
  Value read;
  if (!attribute_get(args[0], "read", read, error)) {
    return false;
  }
  Value data;
  if (!runtime_call_callable(runtime, read, nullptr, 0, data, error)) {
    return false;
  }
  return marshal_loads(runtime, &data, 1, out, error, nullptr);
}

} // namespace

void register_marshal_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "marshal");
  builder.function("loads", marshal_loads)
      .function("dumps", marshal_dumps)
      .function("load", marshal_load)
      .function("dump", marshal_dump)
      .value("version", Value::int64(5));
  runtime.register_module("marshal", builder.finish());
}

} // namespace xlang3
