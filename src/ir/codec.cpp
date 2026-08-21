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
#include "xlang3/ir_codec.h"

#include "xlang3/value.h"

#include <cstring>
#include <limits>
#include <utility>

namespace xlang3::ir {
namespace {

constexpr uint32_t kMagic = 0x33524958u; // XIR3
constexpr uint32_t kVersion = 10;
constexpr uint32_t kMaxVectorItems = 1u << 20u;
constexpr uint32_t kMaxStringBytes = 16u << 20u;

enum class ConstTag : uint8_t {
  None = 1,
  Bool = 2,
  Int64 = 3,
  Double = 4,
  String = 5,
  Bytes = 6,
};

struct Writer {
  std::vector<uint8_t> bytes;

  void u8(uint8_t value) {
    bytes.push_back(value);
  }

  void u16(uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 8u) & 0xffu));
  }

  void u32(uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xffu));
    }
  }

  void u64(uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      bytes.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xffu));
    }
  }

  void i64(int64_t value) {
    u64(static_cast<uint64_t>(value));
  }

  void f64(double value) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double must be 64-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    u64(bits);
  }

  bool string(const std::string& value, std::string& error) {
    if (value.size() > std::numeric_limits<uint32_t>::max()) {
      error = "IR string is too large";
      return false;
    }
    u32(static_cast<uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
    return true;
  }
};

struct Reader {
  const uint8_t* data = nullptr;
  std::size_t size = 0;
  std::size_t pos = 0;

  bool take(std::size_t count, const uint8_t*& out) {
    if (count > size || pos > size - count) {
      return false;
    }
    out = data + pos;
    pos += count;
    return true;
  }

  bool u8(uint8_t& out) {
    const uint8_t* p = nullptr;
    if (!take(1, p)) {
      return false;
    }
    out = p[0];
    return true;
  }

  bool u16(uint16_t& out) {
    const uint8_t* p = nullptr;
    if (!take(2, p)) {
      return false;
    }
    out = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u);
    return true;
  }

  bool u32(uint32_t& out) {
    const uint8_t* p = nullptr;
    if (!take(4, p)) {
      return false;
    }
    out = static_cast<uint32_t>(p[0]) |
          (static_cast<uint32_t>(p[1]) << 8u) |
          (static_cast<uint32_t>(p[2]) << 16u) |
          (static_cast<uint32_t>(p[3]) << 24u);
    return true;
  }

  bool u64(uint64_t& out) {
    const uint8_t* p = nullptr;
    if (!take(8, p)) {
      return false;
    }
    out = 0;
    for (int i = 0; i < 8; ++i) {
      out |= static_cast<uint64_t>(p[i]) << (i * 8);
    }
    return true;
  }

  bool i64(int64_t& out) {
    uint64_t value = 0;
    if (!u64(value)) {
      return false;
    }
    out = static_cast<int64_t>(value);
    return true;
  }

  bool f64(double& out) {
    uint64_t bits = 0;
    if (!u64(bits)) {
      return false;
    }
    std::memcpy(&out, &bits, sizeof(out));
    return true;
  }

  bool string(std::string& out) {
    uint32_t count = 0;
    if (!u32(count) || count > kMaxStringBytes) {
      return false;
    }
    const uint8_t* p = nullptr;
    if (!take(count, p)) {
      return false;
    }
    out.assign(reinterpret_cast<const char*>(p), count);
    return true;
  }
};

bool check_count(uint32_t count, std::string& error) {
  if (count > kMaxVectorItems) {
    error = "IR vector is too large";
    return false;
  }
  return true;
}

bool write_count(Writer& w, std::size_t count, std::string& error) {
  if (count > std::numeric_limits<uint32_t>::max()) {
    error = "IR vector is too large";
    return false;
  }
  w.u32(static_cast<uint32_t>(count));
  return true;
}

bool write_u32_vector(Writer& w, const std::vector<uint32_t>& values, std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (uint32_t value : values) {
    w.u32(value);
  }
  return true;
}

bool read_u32_vector(Reader& r, std::vector<uint32_t>& values, std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.resize(count);
  for (uint32_t& value : values) {
    if (!r.u32(value)) {
      return false;
    }
  }
  return true;
}

bool write_string_vector(Writer& w, const std::vector<std::string>& values, std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (const auto& value : values) {
    if (!w.string(value, error)) {
      return false;
    }
  }
  return true;
}

bool read_string_vector(Reader& r, std::vector<std::string>& values, std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.resize(count);
  for (auto& value : values) {
    if (!r.string(value)) {
      return false;
    }
  }
  return true;
}

bool write_u32_pair_vector(
    Writer& w,
    const std::vector<std::pair<uint32_t, uint32_t>>& values,
    std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (const auto& pair : values) {
    w.u32(pair.first);
    w.u32(pair.second);
  }
  return true;
}

bool read_u32_pair_vector(
    Reader& r,
    std::vector<std::pair<uint32_t, uint32_t>>& values,
    std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.resize(count);
  for (auto& pair : values) {
    if (!r.u32(pair.first) || !r.u32(pair.second)) {
      return false;
    }
  }
  return true;
}

bool write_string_u32_pair_vector(
    Writer& w,
    const std::vector<std::pair<std::string, uint32_t>>& values,
    std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (const auto& pair : values) {
    if (!w.string(pair.first, error)) {
      return false;
    }
    w.u32(pair.second);
  }
  return true;
}

bool read_string_u32_pair_vector(
    Reader& r,
    std::vector<std::pair<std::string, uint32_t>>& values,
    std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.resize(count);
  for (auto& pair : values) {
    if (!r.string(pair.first) || !r.u32(pair.second)) {
      return false;
    }
  }
  return true;
}

bool write_nested_u32_vectors(Writer& w, const std::vector<std::vector<uint32_t>>& values, std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (const auto& value : values) {
    if (!write_u32_vector(w, value, error)) {
      return false;
    }
  }
  return true;
}

bool read_nested_u32_vectors(Reader& r, std::vector<std::vector<uint32_t>>& values, std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.resize(count);
  for (auto& value : values) {
    if (!read_u32_vector(r, value, error)) {
      return false;
    }
  }
  return true;
}

bool write_nested_dict_items(
    Writer& w,
    const std::vector<std::vector<std::pair<uint32_t, uint32_t>>>& values,
    std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (const auto& value : values) {
    if (!write_u32_pair_vector(w, value, error)) {
      return false;
    }
  }
  return true;
}

bool read_nested_dict_items(
    Reader& r,
    std::vector<std::vector<std::pair<uint32_t, uint32_t>>>& values,
    std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.resize(count);
  for (auto& value : values) {
    if (!read_u32_pair_vector(r, value, error)) {
      return false;
    }
  }
  return true;
}

bool write_nested_class_attrs(
    Writer& w,
    const std::vector<std::vector<std::pair<std::string, uint32_t>>>& values,
    std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (const auto& value : values) {
    if (!write_string_u32_pair_vector(w, value, error)) {
      return false;
    }
  }
  return true;
}

bool read_nested_class_attrs(
    Reader& r,
    std::vector<std::vector<std::pair<std::string, uint32_t>>>& values,
    std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.resize(count);
  for (auto& value : values) {
    if (!read_string_u32_pair_vector(r, value, error)) {
      return false;
    }
  }
  return true;
}

bool write_nested_string_vectors(Writer& w, const std::vector<std::vector<std::string>>& values, std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (const auto& value : values) {
    if (!write_string_vector(w, value, error)) {
      return false;
    }
  }
  return true;
}

bool read_nested_string_vectors(Reader& r, std::vector<std::vector<std::string>>& values, std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.resize(count);
  for (auto& value : values) {
    if (!read_string_vector(r, value, error)) {
      return false;
    }
  }
  return true;
}

bool write_params(Writer& w, const std::vector<Param>& params, std::string& error) {
  if (!write_count(w, params.size(), error)) {
    return false;
  }
  for (const auto& param : params) {
    if (!w.string(param.name, error)) {
      return false;
    }
    w.u8(static_cast<uint8_t>(param.kind));
    w.u32(param.default_reg);
  }
  return true;
}

bool read_params(Reader& r, std::vector<Param>& params, std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  params.resize(count);
  for (auto& param : params) {
    uint8_t kind = 0;
    if (!r.string(param.name) || !r.u8(kind) || !r.u32(param.default_reg)) {
      return false;
    }
    if (kind > static_cast<uint8_t>(ParamKind::KwArgs)) {
      error = "IR parameter kind is invalid";
      return false;
    }
    param.kind = static_cast<ParamKind>(kind);
  }
  return true;
}

bool write_call_keyword_args(Writer& w, const std::vector<CallKeywordArg>& values, std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (const auto& value : values) {
    if (!w.string(value.name, error)) {
      return false;
    }
    w.u32(value.value_reg);
  }
  return true;
}

bool read_call_keyword_args(Reader& r, std::vector<CallKeywordArg>& values, std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.resize(count);
  for (auto& value : values) {
    if (!r.string(value.name) || !r.u32(value.value_reg)) {
      return false;
    }
  }
  return true;
}

bool write_call_specs(Writer& w, const std::vector<CallSpec>& specs, std::string& error) {
  if (!write_count(w, specs.size(), error)) {
    return false;
  }
  for (const auto& spec : specs) {
    if (!write_u32_vector(w, spec.positional, error) ||
        !write_call_keyword_args(w, spec.keywords, error)) {
      return false;
    }
    w.u32(spec.star_arg);
    w.u32(spec.kw_star_arg);
  }
  return true;
}

bool read_call_specs(Reader& r, std::vector<CallSpec>& specs, std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  specs.resize(count);
  for (auto& spec : specs) {
    if (!read_u32_vector(r, spec.positional, error) ||
        !read_call_keyword_args(r, spec.keywords, error) ||
        !r.u32(spec.star_arg) ||
        !r.u32(spec.kw_star_arg)) {
      return false;
    }
  }
  return true;
}

bool write_value(Writer& w, const Value& value, std::string& error) {
  switch (value.tag) {
    case ValueTag::None:
      w.u8(static_cast<uint8_t>(ConstTag::None));
      return true;
    case ValueTag::Bool:
      w.u8(static_cast<uint8_t>(ConstTag::Bool));
      w.u8(value.as.b ? 1 : 0);
      return true;
    case ValueTag::Int64:
      w.u8(static_cast<uint8_t>(ConstTag::Int64));
      w.i64(value.as.i64);
      return true;
    case ValueTag::Double:
      w.u8(static_cast<uint8_t>(ConstTag::Double));
      w.f64(value.as.f64);
      return true;
    case ValueTag::Object:
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::String) {
        w.u8(static_cast<uint8_t>(ConstTag::String));
        return w.string(string_object_to_string(*reinterpret_cast<StringObject*>(value.as.obj)), error);
      }
      if (value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Bytes) {
        w.u8(static_cast<uint8_t>(ConstTag::Bytes));
        return w.string(bytes_object_to_string(*reinterpret_cast<BytesObject*>(value.as.obj)), error);
      }
      break;
    default:
      break;
  }
  error = "IR constant type is not serializable";
  return false;
}

bool read_value(Reader& r, Value& value) {
  uint8_t tag = 0;
  if (!r.u8(tag)) {
    return false;
  }
  switch (static_cast<ConstTag>(tag)) {
    case ConstTag::None:
      value = Value::none();
      return true;
    case ConstTag::Bool: {
      uint8_t b = 0;
      if (!r.u8(b)) {
        return false;
      }
      value = Value::boolean(b != 0);
      return true;
    }
    case ConstTag::Int64: {
      int64_t i = 0;
      if (!r.i64(i)) {
        return false;
      }
      value = Value::int64(i);
      return true;
    }
    case ConstTag::Double: {
      double d = 0;
      if (!r.f64(d)) {
        return false;
      }
      value = Value::number(d);
      return true;
    }
    case ConstTag::String: {
      std::string s;
      if (!r.string(s)) {
        return false;
      }
      value = Value::string(std::move(s));
      return true;
    }
    case ConstTag::Bytes: {
      std::string s;
      if (!r.string(s)) {
        return false;
      }
      value = Value::bytes(std::move(s));
      return true;
    }
  }
  return false;
}

bool write_values(Writer& w, const std::vector<Value>& values, std::string& error) {
  if (!write_count(w, values.size(), error)) {
    return false;
  }
  for (const auto& value : values) {
    if (!write_value(w, value, error)) {
      return false;
    }
  }
  return true;
}

bool read_values(Reader& r, std::vector<Value>& values, std::string& error) {
  uint32_t count = 0;
  if (!r.u32(count) || !check_count(count, error)) {
    return false;
  }
  values.clear();
  values.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    Value value;
    if (!read_value(r, value)) {
      return false;
    }
    values.push_back(std::move(value));
  }
  return true;
}

bool write_function(Writer& w, const Function& fn, std::string& error) {
  if (!w.string(fn.name, error)) {
    return false;
  }
  w.u8(fn.is_generator ? 1 : 0);
  if (!write_string_vector(w, fn.params, error) ||
      !write_params(w, fn.signature, error) ||
      !write_string_vector(w, fn.locals, error) ||
      !write_u32_vector(w, fn.cell_slots, error) ||
      !write_string_vector(w, fn.free_vars, error)) {
    return false;
  }
  w.u32(fn.register_count);
  if (!write_values(w, fn.constants, error) ||
      !write_string_vector(w, fn.names, error)) {
    return false;
  }
  if (!write_count(w, fn.raw_blocks.size(), error)) {
    return false;
  }
  for (const auto& block : fn.raw_blocks) {
    if (!w.string(block.language, error) ||
        !w.string(block.provider, error) ||
        !w.string(block.body, error)) {
      return false;
    }
  }
  if (!write_nested_u32_vectors(w, fn.call_args, error) ||
      !write_call_specs(w, fn.call_specs, error) ||
      !write_nested_u32_vectors(w, fn.function_defaults, error) ||
      !write_nested_class_attrs(w, fn.function_annotations, error) ||
      !write_nested_u32_vectors(w, fn.tuple_items, error) ||
      !write_nested_u32_vectors(w, fn.list_items, error) ||
      !write_nested_u32_vectors(w, fn.set_items, error) ||
      !write_nested_dict_items(w, fn.dict_items, error) ||
      !write_nested_u32_vectors(w, fn.function_closures, error) ||
      !write_nested_class_attrs(w, fn.class_attrs, error) ||
      !write_nested_string_vectors(w, fn.class_instance_slots, error) ||
      !write_u32_pair_vector(w, fn.range_specs, error) ||
      !write_u32_pair_vector(w, fn.string_replace_specs, error)) {
    return false;
  }
  if (!write_count(w, fn.code.size(), error)) {
    return false;
  }
  for (const auto& instr : fn.code) {
    w.u16(static_cast<uint16_t>(instr.op));
    w.u32(instr.dst);
    w.u32(instr.a);
    w.u32(instr.b);
    w.u32(instr.c);
  }
  if (!write_u32_vector(w, fn.source_lines, error)) {
    return false;
  }
  return true;
}

bool read_function(Reader& r, Function& fn, std::string& error) {
  uint8_t is_generator = 0;
  if (!r.string(fn.name) ||
      !r.u8(is_generator) ||
      !read_string_vector(r, fn.params, error) ||
      !read_params(r, fn.signature, error) ||
      !read_string_vector(r, fn.locals, error) ||
      !read_u32_vector(r, fn.cell_slots, error) ||
      !read_string_vector(r, fn.free_vars, error) ||
      !r.u32(fn.register_count) ||
      !read_values(r, fn.constants, error) ||
      !read_string_vector(r, fn.names, error)) {
    return false;
  }
  fn.is_generator = is_generator != 0;
  uint32_t raw_count = 0;
  if (!r.u32(raw_count) || !check_count(raw_count, error)) {
    return false;
  }
  fn.raw_blocks.resize(raw_count);
  for (auto& block : fn.raw_blocks) {
    if (!r.string(block.language) || !r.string(block.provider) || !r.string(block.body)) {
      return false;
    }
  }
  if (!read_nested_u32_vectors(r, fn.call_args, error) ||
      !read_call_specs(r, fn.call_specs, error) ||
      !read_nested_u32_vectors(r, fn.function_defaults, error) ||
      !read_nested_class_attrs(r, fn.function_annotations, error) ||
      !read_nested_u32_vectors(r, fn.tuple_items, error) ||
      !read_nested_u32_vectors(r, fn.list_items, error) ||
      !read_nested_u32_vectors(r, fn.set_items, error) ||
      !read_nested_dict_items(r, fn.dict_items, error) ||
      !read_nested_u32_vectors(r, fn.function_closures, error) ||
      !read_nested_class_attrs(r, fn.class_attrs, error) ||
      !read_nested_string_vectors(r, fn.class_instance_slots, error) ||
      !read_u32_pair_vector(r, fn.range_specs, error) ||
      !read_u32_pair_vector(r, fn.string_replace_specs, error)) {
    return false;
  }
  uint32_t code_count = 0;
  if (!r.u32(code_count) || !check_count(code_count, error)) {
    return false;
  }
  fn.code.resize(code_count);
  for (auto& instr : fn.code) {
    uint16_t op = 0;
    if (!r.u16(op) || !r.u32(instr.dst) || !r.u32(instr.a) || !r.u32(instr.b) || !r.u32(instr.c)) {
      return false;
    }
    instr.op = static_cast<Op>(op);
  }
  if (!read_u32_vector(r, fn.source_lines, error)) {
    return false;
  }
  return true;
}

} // namespace

uint64_t source_hash64(const uint8_t* data, std::size_t size) {
  uint64_t hash = 14695981039346656037ull;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

bool encode_module(const Module& module, uint64_t source_hash, EncodedModule& out, std::string& error) {
  Writer w;
  w.u32(kMagic);
  w.u32(kVersion);
  w.u64(source_hash);
  w.u32(module.entry);
  if (!write_string_vector(w, module.global_slots, error)) {
    return false;
  }
  if (!write_count(w, module.functions.size(), error)) {
    return false;
  }
  for (const auto& fn : module.functions) {
    if (!write_function(w, fn, error)) {
      return false;
    }
  }
  out.source_hash = source_hash;
  out.bytes = std::move(w.bytes);
  return true;
}

bool decode_module(const uint8_t* data, std::size_t size, uint64_t expected_source_hash, Module& out, std::string& error) {
  Reader r{data, size, 0};
  uint32_t magic = 0;
  uint32_t version = 0;
  uint64_t source_hash = 0;
  if (!r.u32(magic) || !r.u32(version) || !r.u64(source_hash)) {
    error = "IR cache is truncated";
    return false;
  }
  if (magic != kMagic || version != kVersion) {
    error = "IR cache has unsupported format";
    return false;
  }
  if (source_hash != expected_source_hash) {
    error = "IR cache source hash mismatch";
    return false;
  }
  Module module;
  uint32_t function_count = 0;
  if (!r.u32(module.entry) ||
      !read_string_vector(r, module.global_slots, error) ||
      !r.u32(function_count) ||
      !check_count(function_count, error)) {
    error = error.empty() ? "IR cache is malformed" : error;
    return false;
  }
  module.functions.resize(function_count);
  for (auto& fn : module.functions) {
    if (!read_function(r, fn, error)) {
      error = error.empty() ? "IR cache is malformed" : error;
      return false;
    }
  }
  if (module.entry >= module.functions.size() || r.pos != r.size) {
    error = "IR cache is malformed";
    return false;
  }
  out = std::move(module);
  return true;
}

} // namespace xlang3::ir
