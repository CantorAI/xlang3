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
#include "xlang3/ir_codec.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"

#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace xlang3 {

namespace {

constexpr std::string_view kMagic = "X3M1";
thread_local int64_t g_marshal_version = 5;
thread_local std::unordered_map<const Object*, uint32_t>* g_marshal_write_refs = nullptr;
thread_local std::unordered_map<int64_t, uint32_t>* g_marshal_write_int_refs = nullptr;
thread_local std::unordered_map<uint64_t, uint32_t>* g_marshal_write_float_refs = nullptr;
thread_local uint32_t* g_marshal_next_write_ref = nullptr;
thread_local std::unordered_set<const Object*>* g_marshal_active_immutable = nullptr;
thread_local std::vector<Value>* g_marshal_read_refs = nullptr;
thread_local uint32_t g_marshal_write_depth = 0;
thread_local uint32_t g_marshal_read_depth = 0;
thread_local Runtime* g_marshal_runtime = nullptr;
thread_local bool g_marshal_allow_code = true;

struct MarshalDepthGuard {
  uint32_t& depth;
  explicit MarshalDepthGuard(uint32_t& value) : depth(value) { ++depth; }
  ~MarshalDepthGuard() { --depth; }
};

struct MarshalActiveObjectGuard {
  std::unordered_set<const Object*>* active = nullptr;
  const Object* object = nullptr;
  bool inserted = false;

  MarshalActiveObjectGuard() = default;
  MarshalActiveObjectGuard(std::unordered_set<const Object*>* values, const Object* value)
      : active(values), object(value), inserted(values != nullptr && value != nullptr && values->insert(value).second) {}
  MarshalActiveObjectGuard(const MarshalActiveObjectGuard&) = delete;
  MarshalActiveObjectGuard& operator=(const MarshalActiveObjectGuard&) = delete;
  ~MarshalActiveObjectGuard() {
    if (inserted) active->erase(object);
  }
};

enum class MarshalTag : char {
  NoneValue = 'N',
  FalseValue = 'F',
  TrueValue = 'T',
  Int64 = 'i',
  BigInt = 'g',
  Float64 = 'f',
  String = 's',
  Bytes = 'b',
  List = 'l',
  Tuple = 't',
  Dict = 'd',
  Set = 'e',
  Code = 'c',
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
  if (value_as_bigint(value) != nullptr) {
    out.push_back(static_cast<char>(MarshalTag::BigInt));
    append_bytes(out, value_bigint_to_string(value));
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    out.push_back(static_cast<char>(MarshalTag::Bytes));
    append_bytes(out, memoryview_object_view(*view));
    return true;
  }
  if (value_as_instance(value) != nullptr) {
    Value payload;
    std::string ignored;
    if (object_get_attr(value, "__xlang3_bytes_value__", payload, ignored)) {
      if (auto* bytes = value_as_bytes(payload)) {
        out.push_back(static_cast<char>(MarshalTag::Bytes));
        append_bytes(out, bytes_object_view(*bytes));
        return true;
      }
      if (auto* bytes = value_as_bytearray(payload)) {
        out.push_back(static_cast<char>(MarshalTag::Bytes));
        append_bytes(out, bytes->value);
        return true;
      }
    }
  }
  if (auto* code = value_as_code(value)) {
    if (code->module == nullptr) {
      error = "cannot marshal code without a module";
      return false;
    }
    ir::EncodedModule encoded;
    if (!ir::encode_module(*code->module, 0, encoded, error)) {
      return false;
    }
    out.push_back(static_cast<char>(MarshalTag::Code));
    append_u32(out, code->function_id);
    append_bytes(out, code->mode);
    append_bytes(out, code->filename_override);
    append_u64(out, static_cast<uint64_t>(code->first_line_override));
    append_u64(out, static_cast<uint64_t>(code->flags_override));
    append_u32(out, static_cast<uint32_t>(encoded.bytes.size()));
    out.append(reinterpret_cast<const char*>(encoded.bytes.data()), encoded.bytes.size());
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
  case MarshalTag::BigInt: {
    std::string_view digits;
    if (!reader.read_bytes(digits)) { error = "bad marshal bigint"; return false; }
    out = value_bigint_from_decimal(digits, 10, error);
    return out.tag != ValueTag::Invalid;
  }
  case MarshalTag::Code: {
    uint32_t function_id = 0;
    std::string_view mode;
    std::string_view filename;
    uint64_t first_line = 0;
    uint64_t flags = 0;
    uint32_t encoded_size = 0;
    if (!reader.read_u32(function_id) ||
        !reader.read_bytes(mode) ||
        !reader.read_bytes(filename) ||
        !reader.read_u64(first_line) ||
        !reader.read_u64(flags) ||
        !reader.read_u32(encoded_size) ||
        reader.data.size() - reader.pos < encoded_size) {
      error = "bad marshal code";
      return false;
    }
    ir::Module decoded;
    if (!ir::decode_module(
            reinterpret_cast<const uint8_t*>(reader.data.data() + reader.pos),
            encoded_size,
            0,
            decoded,
            error)) {
      return false;
    }
    reader.pos += encoded_size;
    auto module = std::make_shared<const ir::Module>(std::move(decoded));
    if (function_id >= module->functions.size()) {
      error = "bad marshal code function id";
      return false;
    }
    out = Value::code(std::move(module), function_id, std::string(mode));
    auto* code = value_as_code(out);
    code->filename_override = std::string(filename);
    code->first_line_override = static_cast<int64_t>(first_line);
    code->flags_override = static_cast<int64_t>(flags);
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
  if (auto* view = value_as_memoryview(value)) {
    if (view->released) {
      error = "operation forbidden on released memoryview object";
      return false;
    }
    out = memoryview_object_view(*view);
    return true;
  }
  error = "marshal.loads() expected bytes-like object";
  return false;
}

bool validate_marshaled_code(const Value& value, std::unordered_set<const Object*>& active, std::string& error) {
  if (value_as_code(value) != nullptr) {
    if (!g_marshal_allow_code) { error = "unmarshalling code objects is disallowed"; return false; }
    if (!active.insert(value.as.obj).second) { error = "unmarshallable object"; return false; }
    Value constants;
    if (!attribute_get(value, "co_consts", constants, error)) { active.erase(value.as.obj); return false; }
    auto* tuple = value_as_tuple(constants);
    if (tuple == nullptr) { active.erase(value.as.obj); error = "unmarshallable object"; return false; }
    for (const auto& item : tuple->items) {
      if (!validate_marshaled_code(item, active, error)) { active.erase(value.as.obj); return false; }
    }
    active.erase(value.as.obj);
    return true;
  }
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value_as_string(value) != nullptr ||
      value_as_bytes(value) != nullptr || value_as_bytearray(value) != nullptr || value_as_memoryview(value) != nullptr ||
      value_as_bigint(value) != nullptr || value_as_complex(value) != nullptr) return true;
  if (auto* list = value_as_list(value)) {
    if (!active.insert(value.as.obj).second) { error = "unmarshallable object"; return false; }
    for (const auto& item : list->items) if (!validate_marshaled_code(item, active, error)) { active.erase(value.as.obj); return false; }
    active.erase(value.as.obj); return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    if (!active.insert(value.as.obj).second) { error = "unmarshallable object"; return false; }
    for (const auto& item : tuple->items) if (!validate_marshaled_code(item, active, error)) { active.erase(value.as.obj); return false; }
    active.erase(value.as.obj); return true;
  }
  if (auto* set = value_as_set(value)) {
    if (!active.insert(value.as.obj).second) { error = "unmarshallable object"; return false; }
    for (const auto& item : set->items) if (!validate_marshaled_code(item, active, error)) { active.erase(value.as.obj); return false; }
    active.erase(value.as.obj); return true;
  }
  if (auto* dict = value_as_dict(value)) {
    if (!active.insert(value.as.obj).second) { error = "unmarshallable object"; return false; }
    for (const auto& entry : dict->entries) if (!validate_marshaled_code(entry.first, active, error) ||
        !validate_marshaled_code(entry.second, active, error)) { active.erase(value.as.obj); return false; }
    active.erase(value.as.obj); return true;
  }
  if (auto* slice = value_as_slice(value)) {
    return validate_marshaled_code(slice->start, active, error) && validate_marshaled_code(slice->stop, active, error) &&
        validate_marshaled_code(slice->step, active, error);
  }
  error = "unmarshallable object";
  return false;
}

bool marshal_cpython_long(const Value& value, std::string& out, std::string& error) {
  bool negative = false;
  const uint32_t* limbs = nullptr;
  uint32_t limb_count = 0;
  uint32_t local_limbs[2] = {};
  if (value.tag == ValueTag::Int64) {
    negative = value.as.i64 < 0;
    const uint64_t magnitude = negative
        ? static_cast<uint64_t>(-(value.as.i64 + 1)) + 1u
        : static_cast<uint64_t>(value.as.i64);
    local_limbs[0] = static_cast<uint32_t>(magnitude);
    local_limbs[1] = static_cast<uint32_t>(magnitude >> 32u);
    limb_count = local_limbs[1] == 0 ? (local_limbs[0] == 0 ? 0u : 1u) : 2u;
    limbs = local_limbs;
  } else if (!value_bigint_limb_view(value, negative, limbs, limb_count)) {
    error = "marshal expected integer";
    return false;
  }
  uint32_t bit_length = 0;
  if (limb_count != 0) {
    uint32_t high = limbs[limb_count - 1];
    bit_length = (limb_count - 1) * 32u;
    while (high != 0) { ++bit_length; high >>= 1u; }
  }
  const uint32_t digit_count = (bit_length + 14u) / 15u;
  const int32_t signed_count = negative ? -static_cast<int32_t>(digit_count) : static_cast<int32_t>(digit_count);
  append_u32(out, static_cast<uint32_t>(signed_count));
  for (uint32_t i = 0; i < digit_count; ++i) {
    const uint32_t bit = i * 15u;
    const uint32_t limb = bit / 32u;
    const uint32_t shift = bit % 32u;
    uint64_t window = limbs[limb];
    if (limb + 1 < limb_count) window |= static_cast<uint64_t>(limbs[limb + 1]) << 32u;
    const uint16_t digit = static_cast<uint16_t>((window >> shift) & 0x7fffu);
    out.push_back(static_cast<char>(digit & 0xffu));
    out.push_back(static_cast<char>(digit >> 8u));
  }
  return true;
}

bool marshal_cpython_value(const Value& value, std::string& out, std::string& error) {
  MarshalDepthGuard depth_guard(g_marshal_write_depth);
  if (g_marshal_write_depth > 1000) { error = "object too deeply nested to marshal"; return false; }
  bool add_reference = false;
  if (g_marshal_runtime != nullptr) {
    if (const Value* stop = g_marshal_runtime->find_builtin("StopIteration"); stop != nullptr && value_is(value, *stop)) {
      out.push_back('S'); return true;
    }
    if (const Value* ellipsis = g_marshal_runtime->find_builtin("Ellipsis"); ellipsis != nullptr && value_is(value, *ellipsis)) {
      out.push_back('.'); return true;
    }
  }
  if (g_marshal_version >= 3 && value.tag == ValueTag::Int64) {
    auto found = g_marshal_write_int_refs->find(value.as.i64);
    if (found != g_marshal_write_int_refs->end()) {
      out.push_back('r'); append_u32(out, found->second); return true;
    }
    g_marshal_write_int_refs->emplace(value.as.i64, (*g_marshal_next_write_ref)++);
    add_reference = true;
  } else if (g_marshal_version >= 3 && value.tag == ValueTag::Double) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value.as.f64, sizeof(bits));
    auto found = g_marshal_write_float_refs->find(bits);
    if (found != g_marshal_write_float_refs->end()) {
      out.push_back('r'); append_u32(out, found->second); return true;
    }
    g_marshal_write_float_refs->emplace(bits, (*g_marshal_next_write_ref)++);
    add_reference = true;
  }
  const bool recursive_container = value.tag == ValueTag::Object && value.as.obj != nullptr &&
      (value_as_list(value) != nullptr || value_as_tuple(value) != nullptr ||
       value_as_dict(value) != nullptr || value_as_set(value) != nullptr);
  const bool track_active = recursive_container && g_marshal_version < 3;
  MarshalActiveObjectGuard active_guard(g_marshal_active_immutable, track_active ? value.as.obj : nullptr);
  if (track_active && !active_guard.inserted) { error = "object too deeply nested to marshal"; return false; }
  const bool referenceable_object = value.tag == ValueTag::Object && value.as.obj != nullptr &&
      (recursive_container || value_as_string(value) != nullptr || value_as_bytes(value) != nullptr ||
       value_as_bigint(value) != nullptr || value_as_complex(value) != nullptr ||
       value_as_code(value) != nullptr);
  if (referenceable_object && g_marshal_version >= 3) {
    auto found = g_marshal_write_refs->find(value.as.obj);
    if (found != g_marshal_write_refs->end()) {
      out.push_back('r'); append_u32(out, found->second); return true;
    }
    const uint32_t index = (*g_marshal_next_write_ref)++;
    g_marshal_write_refs->emplace(value.as.obj, index);
    add_reference = true;
  }
  if (value.tag == ValueTag::None) { out.push_back('N'); return true; }
  if (value.tag == ValueTag::Bool) { out.push_back(value.as.b ? 'T' : 'F'); return true; }
  if (value.tag == ValueTag::Int64) {
    if (value.as.i64 >= std::numeric_limits<int32_t>::min() && value.as.i64 <= std::numeric_limits<int32_t>::max()) {
      out.push_back(static_cast<char>('i' | (add_reference ? 0x80 : 0)));
      append_u32(out, static_cast<uint32_t>(static_cast<int32_t>(value.as.i64))); return true;
    }
    out.push_back(static_cast<char>('l' | (add_reference ? 0x80 : 0)));
    return marshal_cpython_long(value, out, error);
  }
  if (value_as_bigint(value) != nullptr) {
    out.push_back(static_cast<char>('l' | (add_reference ? 0x80 : 0)));
    return marshal_cpython_long(value, out, error);
  }
  if (value.tag == ValueTag::Double) {
    out.push_back(static_cast<char>('g' | (add_reference ? 0x80 : 0)));
    uint64_t bits = 0; std::memcpy(&bits, &value.as.f64, sizeof(bits)); append_u64(out, bits); return true;
  }
  if (auto* complex = value_as_complex(value)) {
    out.push_back(static_cast<char>('y' | (add_reference ? 0x80 : 0)));
    uint64_t real = 0, imag = 0;
    std::memcpy(&real, &complex->real, sizeof(real));
    std::memcpy(&imag, &complex->imag, sizeof(imag));
    append_u64(out, real); append_u64(out, imag); return true;
  }
  if (auto* string = value_as_string(value)) {
    const char tag = g_marshal_version >= 3 && string_value_is_interned(value) ? 't' : 'u';
    out.push_back(static_cast<char>(tag | (add_reference ? 0x80 : 0)));
    append_bytes(out, string_object_view(*string)); return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    out.push_back(static_cast<char>('s' | (add_reference ? 0x80 : 0)));
    append_bytes(out, bytes_object_view(*bytes)); return true;
  }
  if (auto* bytes = value_as_bytearray(value)) {
    out.push_back('s'); append_bytes(out, bytes->value); return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    out.push_back('s'); append_bytes(out, memoryview_object_view(*view)); return true;
  }
  if (value_as_instance(value) != nullptr) {
    Value payload; std::string ignored;
    if (object_get_attr(value, "__xlang3_bytes_value__", payload, ignored)) {
      if (auto* bytes = value_as_bytes(payload)) { out.push_back('s'); append_bytes(out, bytes_object_view(*bytes)); return true; }
      if (auto* bytes = value_as_bytearray(payload)) { out.push_back('s'); append_bytes(out, bytes->value); return true; }
    }
  }
  if (auto* slice = value_as_slice(value)) {
    if (g_marshal_version < 5) { error = "unmarshallable object"; return false; }
    if (!g_marshal_active_immutable->insert(value.as.obj).second) { error = "object too deeply nested to marshal"; return false; }
    out.push_back(':');
    const bool ok = marshal_cpython_value(slice->start, out, error) &&
        marshal_cpython_value(slice->stop, out, error) &&
        marshal_cpython_value(slice->step, out, error);
    g_marshal_active_immutable->erase(value.as.obj);
    return ok;
  }
  if (value_as_code(value) != nullptr) {
    std::unordered_set<const Object*> active_code;
    if (!validate_marshaled_code(value, active_code, error)) return false;
    std::string custom(kMagic);
    if (!marshal_value(value, custom, error)) return false;
    out.push_back(static_cast<char>('?' | (add_reference ? 0x80 : 0)));
    append_bytes(out, custom); return true;
  }
  if (auto* list = value_as_list(value)) {
    out.push_back(static_cast<char>('[' | (add_reference ? 0x80 : 0))); append_u32(out, static_cast<uint32_t>(list->items.size()));
    for (const auto& item : list->items) if (!marshal_cpython_value(item, out, error)) return false;
    return true;
  }
  if (auto* tuple = value_as_tuple(value)) {
    out.push_back(static_cast<char>('(' | (add_reference ? 0x80 : 0))); append_u32(out, static_cast<uint32_t>(tuple->items.size()));
    for (const auto& item : tuple->items) if (!marshal_cpython_value(item, out, error)) return false;
    return true;
  }
  if (auto* set = value_as_set(value)) {
    out.push_back(static_cast<char>((set->frozen ? '>' : '<') | (add_reference ? 0x80 : 0))); append_u32(out, static_cast<uint32_t>(set->items.size()));
    for (const auto& item : set->items) if (!marshal_cpython_value(item, out, error)) return false;
    return true;
  }
  if (auto* dict = value_as_dict(value)) {
    out.push_back(static_cast<char>('{' | (add_reference ? 0x80 : 0)));
    for (const auto& entry : dict->entries) {
      if (!marshal_cpython_value(entry.first, out, error) || !marshal_cpython_value(entry.second, out, error)) return false;
    }
    out.push_back('0');
    return true;
  }
  error = "unmarshallable object";
  return false;
}

bool unmarshal_cpython_value(MarshalReader& reader, Value& out, std::string& error, bool allow_null = false) {
  MarshalDepthGuard depth_guard(g_marshal_read_depth);
  if (g_marshal_read_depth > 1000) { error = "object too deeply nested to unmarshal"; return false; }
  char raw_tag = 0;
  if (!reader.read_byte(raw_tag)) { error = "EOF read where object expected"; return false; }
  const unsigned char tag_byte = static_cast<unsigned char>(raw_tag);
  const char tag = static_cast<char>(tag_byte & 0x7fu);
  const bool flagged_reference = (tag_byte & 0x80u) != 0;
  const auto remember_reference = [&]() {
    if (flagged_reference && g_marshal_read_refs != nullptr) g_marshal_read_refs->push_back(out);
  };
  const auto box_old_scalar = [&](const std::string& type_name, const std::string& attribute) {
    if (flagged_reference || g_marshal_runtime == nullptr) return;
    const Value* klass = g_marshal_runtime->find_builtin(type_name);
    if (klass == nullptr) return;
    Value boxed = Value::instance(*klass);
    std::string ignored;
    if (object_set_attr(boxed, attribute, out, ignored)) out = std::move(boxed);
  };
  if (tag == 'r') {
    uint32_t index = 0;
    if (!reader.read_u32(index)) { error = "EOF read where object expected"; return false; }
    if (g_marshal_read_refs == nullptr || index >= g_marshal_read_refs->size() ||
        (*g_marshal_read_refs)[index].tag == ValueTag::Invalid) {
      error = "bad marshal data (invalid reference)"; return false;
    }
    value_assign_fast(out, (*g_marshal_read_refs)[index]);
    return true;
  }
  if (tag == '0' && allow_null) { out = Value::invalid(); return true; }
  if (tag == 'N') { out = Value::none(); return true; }
  if (tag == 'F') { out = Value::boolean(false); return true; }
  if (tag == 'T') { out = Value::boolean(true); return true; }
  if (tag == 'S' && g_marshal_runtime != nullptr) {
    if (const Value* value = g_marshal_runtime->find_builtin("StopIteration")) { out = *value; return true; }
  }
  if (tag == '.' && g_marshal_runtime != nullptr) {
    if (const Value* value = g_marshal_runtime->find_builtin("Ellipsis")) { out = *value; return true; }
  }
  if (tag == 'i') {
    uint32_t raw = 0; if (!reader.read_u32(raw)) { error = "EOF read where object expected"; return false; }
    out = Value::int64(static_cast<int32_t>(raw)); remember_reference();
    box_old_scalar("int", "__xlang3_int_value__"); return true;
  }
  if (tag == 'I') {
    uint64_t raw = 0; if (!reader.read_u64(raw)) { error = "EOF read where object expected"; return false; }
    out = Value::int64(static_cast<int64_t>(raw)); remember_reference();
    box_old_scalar("int", "__xlang3_int_value__"); return true;
  }
  if (tag == 'l') {
    uint32_t raw_count = 0; if (!reader.read_u32(raw_count)) { error = "EOF read where object expected"; return false; }
    const int32_t signed_count = static_cast<int32_t>(raw_count);
    if (signed_count == std::numeric_limits<int32_t>::min()) { error = "bad marshal data"; return false; }
    const bool negative = signed_count < 0;
    const uint32_t count = static_cast<uint32_t>(negative ? -signed_count : signed_count);
    if (count > (reader.data.size() - reader.pos) / 2u) { error = "EOF read where object expected"; return false; }
    std::vector<uint8_t> bytes((static_cast<size_t>(count) * 15u + 7u) / 8u, 0);
    for (uint32_t i = 0; i < count; ++i) {
      char low = 0, high = 0;
      if (!reader.read_byte(low) || !reader.read_byte(high)) { error = "EOF read where object expected"; return false; }
      const uint16_t digit = static_cast<uint16_t>(static_cast<unsigned char>(low)) |
          static_cast<uint16_t>(static_cast<unsigned char>(high)) << 8u;
      if (digit > 0x7fffu || (i + 1 == count && digit == 0)) { error = "bad marshal data"; return false; }
      const size_t bit = static_cast<size_t>(i) * 15u;
      for (uint32_t b = 0; b < 15u; ++b) if ((digit >> b) & 1u) bytes[(bit + b) / 8u] |= static_cast<uint8_t>(1u << ((bit + b) % 8u));
    }
    if (!value_bigint_from_bytes(bytes.data(), bytes.size(), false, false, out, error)) return false;
    if (negative) { Value neg; if (!value_int_like_mul(out, Value::int64(-1), neg)) { error = "bad marshal data"; return false; } out = std::move(neg); }
    remember_reference();
    box_old_scalar("int", "__xlang3_int_value__");
    return true;
  }
  if (tag == 'g') {
    uint64_t raw = 0; if (!reader.read_u64(raw)) { error = "EOF read where object expected"; return false; }
    double number = 0; std::memcpy(&number, &raw, sizeof(number)); out = Value::number(number);
    remember_reference(); box_old_scalar("float", "__xlang3_float_value__"); return true;
  }
  if (tag == 'y') {
    uint64_t real_bits = 0, imag_bits = 0;
    if (!reader.read_u64(real_bits) || !reader.read_u64(imag_bits)) { error = "EOF read where object expected"; return false; }
    double real = 0, imag = 0; std::memcpy(&real, &real_bits, sizeof(real)); std::memcpy(&imag, &imag_bits, sizeof(imag));
    out = Value::complex(real, imag); remember_reference(); return true;
  }
  if (tag == 's' || tag == 'u' || tag == 't' || tag == 'a' || tag == 'A') {
    std::string_view bytes; if (!reader.read_bytes(bytes)) { error = "EOF read where object expected"; return false; }
    out = tag == 's' ? Value::bytes(std::string(bytes)) : noninterned_string_value(bytes);
    if (tag == 't') out = intern_string_value(out);
    remember_reference(); return true;
  }
  if (tag == 'z' || tag == 'Z') {
    char length = 0; if (!reader.read_byte(length) || reader.data.size() - reader.pos < static_cast<unsigned char>(length)) { error = "EOF read where object expected"; return false; }
    const auto bytes = reader.data.substr(reader.pos, static_cast<unsigned char>(length)); reader.pos += static_cast<unsigned char>(length);
    out = noninterned_string_value(bytes);
    if (tag == 'Z') out = intern_string_value(out);
    remember_reference(); return true;
  }
  if (tag == '[' || tag == '(' || tag == ')' || tag == '<' || tag == '>') {
    uint32_t count = 0;
    if (tag == ')') { char small = 0; if (!reader.read_byte(small)) { error = "EOF read where object expected"; return false; } count = static_cast<unsigned char>(small); }
    else if (!reader.read_u32(count)) { error = "EOF read where object expected"; return false; }
    if (tag == '[') out = Value::list({});
    else if (tag == '<') out = Value::set({});
    else if (tag == '>') out = Value::frozenset({});
    else out = Value::tuple(std::vector<Value>(count, Value::invalid()));
    if (flagged_reference) g_marshal_read_refs->push_back(out);
    for (uint32_t i = 0; i < count; ++i) {
      Value item; if (!unmarshal_cpython_value(reader, item, error)) return false;
      if (auto* list = value_as_list(out)) list->items.push_back(std::move(item));
      else if (auto* set = value_as_set(out)) {
        if (value_is(item, out)) {
          error = set->frozen ? "bad marshal data (invalid reference)" : "unhashable type: 'set'";
          return false;
        }
        set->items.push_back(std::move(item));
      }
      else value_move_assign_fast(value_as_tuple(out)->items[i], item);
    }
    return true;
  }
  if (tag == '{') {
    out = Value::dict({});
    if (flagged_reference) g_marshal_read_refs->push_back(out);
    auto* dict = value_as_dict(out);
    while (true) {
      Value key; if (!unmarshal_cpython_value(reader, key, error, true)) return false;
      if (key.tag == ValueTag::Invalid) break;
      if (value_is(key, out)) { error = "unhashable type: 'dict'"; return false; }
      Value value; if (!unmarshal_cpython_value(reader, value, error)) return false;
      dict->entries.push_back({std::move(key), std::move(value)});
    }
    return true;
  }
  if (tag == ':') {
    Value start, stop, step;
    if (!unmarshal_cpython_value(reader, start, error) ||
        !unmarshal_cpython_value(reader, stop, error) ||
        !unmarshal_cpython_value(reader, step, error)) return false;
    out = Value::slice(std::move(start), std::move(stop), std::move(step)); return true;
  }
  if (tag == '?') {
    if (!g_marshal_allow_code) { error = "unmarshalling code objects is disallowed"; return false; }
    std::string_view custom;
    if (!reader.read_bytes(custom)) { error = "EOF read where object expected"; return false; }
    if (custom.size() < kMagic.size() || custom.substr(0, kMagic.size()) != kMagic) { error = "bad marshal data"; return false; }
    MarshalReader nested{custom.substr(kMagic.size()), 0};
    const bool ok = unmarshal_value(nested, out, error) && nested.pos == nested.data.size();
    if (ok) remember_reference();
    return ok;
  }
  error = "bad marshal data (unknown type code)";
  return false;
}

bool marshal_dumps(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "marshal.dumps() expected value and optional version";
    return false;
  }
  if (argc == 2 && args[1].tag != ValueTag::Int64) {
    error = "marshal version must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  g_marshal_version = argc == 2 && args[1].tag == ValueTag::Int64 ? args[1].as.i64 : 5;
  g_marshal_runtime = &runtime;
  std::unordered_map<const Object*, uint32_t> write_refs;
  std::unordered_map<int64_t, uint32_t> write_int_refs;
  std::unordered_map<uint64_t, uint32_t> write_float_refs;
  uint32_t next_write_ref = 0;
  std::unordered_set<const Object*> active_immutable;
  g_marshal_write_refs = &write_refs;
  g_marshal_write_int_refs = &write_int_refs;
  g_marshal_write_float_refs = &write_float_refs;
  g_marshal_next_write_ref = &next_write_ref;
  g_marshal_active_immutable = &active_immutable;
  const auto clear_write_context = [&]() {
    g_marshal_write_refs = nullptr;
    g_marshal_write_int_refs = nullptr;
    g_marshal_write_float_refs = nullptr;
    g_marshal_next_write_ref = nullptr;
    g_marshal_active_immutable = nullptr;
    g_marshal_runtime = nullptr;
  };
  std::string data;
  if (value_as_code(args[0]) != nullptr) {
    if (!g_marshal_allow_code) {
      clear_write_context();
      error = "marshalling code objects is disallowed"; runtime.raise_class_error("ValueError", error); return false;
    }
    std::unordered_set<const Object*> active_code;
    if (!validate_marshaled_code(args[0], active_code, error)) {
      clear_write_context();
      runtime.raise_class_error("ValueError", error); return false;
    }
    data.assign(kMagic);
    if (!marshal_value(args[0], data, error)) {
      clear_write_context();
      runtime.raise_class_error("ValueError", error); return false;
    }
  } else if (!marshal_cpython_value(args[0], data, error)) {
    clear_write_context();
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  clear_write_context();
  out = Value::bytes(std::move(data));
  return true;
}

bool marshal_loads(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "marshal.loads() expected data";
    return false;
  }
  std::string_view data;
  if (!get_data_bytes(args[0], data, error)) {
    runtime.raise_class_error(
        error.find("released memoryview") != std::string::npos ? "ValueError" : "TypeError",
        error);
    return false;
  }
  const bool custom = data.size() >= kMagic.size() && data.substr(0, kMagic.size()) == kMagic;
  if (custom && !g_marshal_allow_code) {
    error = "unmarshalling code objects is disallowed";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  MarshalReader reader{custom ? data.substr(kMagic.size()) : data, 0};
  std::vector<Value> read_refs;
  g_marshal_runtime = &runtime;
  g_marshal_read_refs = &read_refs;
  if (custom ? !unmarshal_value(reader, out, error) : !unmarshal_cpython_value(reader, out, error)) {
    g_marshal_read_refs = nullptr;
    g_marshal_runtime = nullptr;
    runtime.raise_class_error(error.rfind("EOF", 0) == 0 ? "EOFError" :
        error.rfind("unhashable", 0) == 0 ? "TypeError" : "ValueError", error);
    return false;
  }
  g_marshal_read_refs = nullptr;
  g_marshal_runtime = nullptr;
  if (custom && reader.pos != reader.data.size()) {
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
  int64_t start_position = 0;
  bool seekable = false;
  Value tell;
  Value seek;
  if (attribute_get(args[0], "tell", tell, error) && attribute_get(args[0], "seek", seek, error)) {
    Value position;
    if (runtime_call_callable(runtime, tell, nullptr, 0, position, error) && position.tag == ValueTag::Int64) {
      start_position = position.as.i64;
      seekable = true;
    }
  }
  error.clear();
  Value data;
  if (!runtime_call_callable(runtime, read, nullptr, 0, data, error)) {
    return false;
  }
  std::string_view bytes;
  if (!get_data_bytes(data, bytes, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value read_buffer;
  if (seekable && attribute_get(args[0], "readinto", read_buffer, error)) {
    Value reset_arg = Value::int64(start_position);
    Value ignored;
    if (!runtime_call_callable(runtime, seek, &reset_arg, 1, ignored, error)) return false;
    Value buffer = Value::bytearray(std::string(bytes.size(), '\0'));
    Value count;
    if (!runtime_call_callable(runtime, read_buffer, &buffer, 1, count, error)) return false;
    if (count.tag != ValueTag::Int64 || count.as.i64 < 0 || static_cast<uint64_t>(count.as.i64) > bytes.size()) {
      error = "readinto() returned invalid length";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    auto* storage = value_as_bytearray(buffer);
    bytes = std::string_view(storage->value.data(), static_cast<size_t>(count.as.i64));
    data = std::move(buffer);
  }
  const bool custom = bytes.size() >= kMagic.size() && bytes.substr(0, kMagic.size()) == kMagic;
  MarshalReader reader{custom ? bytes.substr(kMagic.size()) : bytes, 0};
  std::vector<Value> read_refs;
  g_marshal_read_refs = &read_refs;
  if (custom ? !unmarshal_value(reader, out, error) : !unmarshal_cpython_value(reader, out, error)) {
    g_marshal_read_refs = nullptr;
    runtime.raise_class_error(error.rfind("EOF", 0) == 0 ? "EOFError" :
        error.rfind("unhashable", 0) == 0 ? "TypeError" : "ValueError", error);
    return false;
  }
  g_marshal_read_refs = nullptr;
  if (seekable) {
    const uint64_t consumed = reader.pos + (custom ? kMagic.size() : 0u);
    Value next_position = Value::int64(start_position + static_cast<int64_t>(consumed));
    Value ignored;
    if (!runtime_call_callable(runtime, seek, &next_position, 1, ignored, error)) return false;
  }
  return true;
}

bool marshal_allow_code_keyword(const NativeKeywordArg* kwargs, uint32_t kwargc, bool& allow_code,
                                std::string& error) {
  allow_code = true;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name != "allow_code" || kwargs[i].value == nullptr) {
      error = "unexpected keyword argument '" + name + "'";
      return false;
    }
    if (kwargs[i].value->tag == ValueTag::Bool) allow_code = kwargs[i].value->as.b;
    else if (kwargs[i].value->tag == ValueTag::Int64) allow_code = kwargs[i].value->as.i64 != 0;
    else { error = "allow_code must be bool"; return false; }
  }
  return true;
}

bool marshal_dumps_kw(Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs,
                      uint32_t kwargc, Value& out, std::string& error, void* data) {
  bool allow = true; if (!marshal_allow_code_keyword(kwargs, kwargc, allow, error)) return false;
  g_marshal_allow_code = allow;
  const bool ok = marshal_dumps(runtime, args, argc, out, error, data);
  g_marshal_allow_code = true;
  return ok;
}

bool marshal_loads_kw(Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs,
                      uint32_t kwargc, Value& out, std::string& error, void* data) {
  bool allow = true; if (!marshal_allow_code_keyword(kwargs, kwargc, allow, error)) return false;
  g_marshal_allow_code = allow;
  const bool ok = marshal_loads(runtime, args, argc, out, error, data);
  g_marshal_allow_code = true;
  return ok;
}

bool marshal_dump_kw(Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs,
                     uint32_t kwargc, Value& out, std::string& error, void* data) {
  bool allow = true; if (!marshal_allow_code_keyword(kwargs, kwargc, allow, error)) return false;
  g_marshal_allow_code = allow;
  const bool ok = marshal_dump(runtime, args, argc, out, error, data);
  g_marshal_allow_code = true;
  return ok;
}

bool marshal_load_kw(Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs,
                     uint32_t kwargc, Value& out, std::string& error, void* data) {
  bool allow = true; if (!marshal_allow_code_keyword(kwargs, kwargc, allow, error)) return false;
  g_marshal_allow_code = allow;
  const bool ok = marshal_load(runtime, args, argc, out, error, data);
  g_marshal_allow_code = true;
  return ok;
}

} // namespace

void register_marshal_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "marshal");
  builder.value("loads", runtime.make_native_function("marshal.loads", marshal_loads, nullptr, nullptr, nullptr, false, marshal_loads_kw))
      .value("dumps", runtime.make_native_function("marshal.dumps", marshal_dumps, nullptr, nullptr, nullptr, false, marshal_dumps_kw))
      .value("load", runtime.make_native_function("marshal.load", marshal_load, nullptr, nullptr, nullptr, false, marshal_load_kw))
      .value("dump", runtime.make_native_function("marshal.dump", marshal_dump, nullptr, nullptr, nullptr, false, marshal_dump_kw))
      .value("version", Value::int64(5));
  runtime.register_module("marshal", builder.finish());
}

} // namespace xlang3
