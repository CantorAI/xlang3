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
#include "xlang3/functional_iterators.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

namespace xlang3 {

namespace {

Value g_struct_error_class;
Value g_struct_class;

struct FormatItem {
  char code = '\0';
  uint32_t count = 1;
  uint32_t size = 0;
};

struct ParsedFormat {
  bool little_endian = true;
  bool native = true;
  std::vector<FormatItem> items;
  uint32_t size = 0;
  uint32_t arg_count = 0;
};

bool make_struct_error(Runtime& runtime, const std::string& message, Value& out) {
  Value klass = g_struct_error_class;
  if (klass.tag != ValueTag::Object || value_as_class(klass) == nullptr) {
    klass = runtime.find_builtin("Exception") != nullptr ? *runtime.find_builtin("Exception") : Value::class_object("error", {});
  }
  out = Value::instance(klass);
  std::string ignored;
  object_set_attr(out, "message", Value::string(message), ignored);
  object_set_attr(out, "args", Value::tuple({Value::string(message)}), ignored);
  return true;
}

bool struct_fail(Runtime& runtime, const std::string& message, std::string& error) {
  Value exception;
  make_struct_error(runtime, message, exception);
  runtime.set_pending_exception(std::move(exception));
  error = message;
  return false;
}

bool get_format(const Value& value, std::string& out, std::string& error) {
  if (auto* text = value_as_string(value)) {
    out = string_object_to_string(*text);
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  error = "struct format must be str or bytes";
  return false;
}

uint32_t primitive_size(char ch) {
  switch (ch) {
  case 'x':
  case 'c':
  case 'b':
  case 'B':
  case '?':
    return 1;
  case 'h':
  case 'H':
  case 'e':
    return 2;
  case 'i':
  case 'I':
  case 'l':
  case 'L':
  case 'f':
    return 4;
  case 'q':
  case 'Q':
  case 'd':
  case 'F':
    return 8;
  case 'D':
    return 16;
  case 'P':
  case 'n':
  case 'N':
    return static_cast<uint32_t>(sizeof(void*));
  default:
    return 0;
  }
}

bool parse_format(Runtime& runtime, const std::string& format, ParsedFormat& parsed, std::string& error) {
  uint64_t repeat = 0;
  bool repeat_seen = false;
  bool prefix_seen = false;
  for (char ch : format) {
    if (ch == '\0') {
      return struct_fail(runtime, "embedded null character", error);
    }
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
    if (ch == '@' || ch == '=' || ch == '<' || ch == '>' || ch == '!') {
      if (prefix_seen || !parsed.items.empty() || repeat_seen) {
        return struct_fail(runtime, "bad char in struct format", error);
      }
      prefix_seen = true;
      parsed.little_endian = !(ch == '>' || ch == '!');
      parsed.native = ch == '@';
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      const uint64_t digit = static_cast<uint64_t>(ch - '0');
      if (repeat > (static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) - digit) / 10) {
        return struct_fail(runtime, "total struct size too long", error);
      }
      repeat = repeat * 10 + digit;
      repeat_seen = true;
      continue;
    }
    const uint64_t wide_count = repeat_seen ? repeat : 1;
    repeat = 0;
    repeat_seen = false;
    if (wide_count > std::numeric_limits<uint32_t>::max()) {
      return struct_fail(runtime, "total struct size too long", error);
    }
    const uint32_t count = static_cast<uint32_t>(wide_count);
    if (ch == 's' || ch == 'p') {
      if (count > std::numeric_limits<uint32_t>::max() - parsed.size) {
        return struct_fail(runtime, "total struct size too long", error);
      }
      parsed.items.push_back({ch, count, 1});
      parsed.size += count;
      parsed.arg_count += 1;
      continue;
    }
    const uint32_t item_size = primitive_size(ch);
    if (item_size == 0) {
      return struct_fail(runtime, "bad char in struct format", error);
    }
    if ((ch == 'P' || ch == 'n' || ch == 'N') && !parsed.native) {
      return struct_fail(runtime, "bad char in struct format", error);
    }
    if (count != 0 && item_size > (std::numeric_limits<uint32_t>::max() - parsed.size) / count) {
      return struct_fail(runtime, "total struct size too long", error);
    }
    parsed.items.push_back({ch, count, item_size});
    parsed.size += count * item_size;
    if (ch != 'x') {
      parsed.arg_count += count;
    }
  }
  if (repeat_seen) {
    return struct_fail(runtime, "repeat count given without format specifier", error);
  }
  return true;
}

bool calcsize_text(Runtime& runtime, const std::string& format, int64_t& size, std::string& error) {
  ParsedFormat parsed;
  if (!parse_format(runtime, format, parsed, error)) {
    return false;
  }
  size = parsed.size;
  return true;
}

bool get_bytes_like(Runtime& runtime, const Value& value, std::string_view& out, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_view(*bytes);
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    out = memoryview_object_view(*view);
    if (out.data()) return true;
  }
  if (value_as_instance(value) != nullptr) {
    Value payload;
    std::string ignored;
    if (object_get_attr(value, "__xlang3_bytes_value__", payload, ignored)) {
      if (auto* bytes = value_as_bytes(payload)) { out = bytes_object_view(*bytes); return true; }
      if (auto* bytes = value_as_bytearray(payload)) { out = bytes->value; return true; }
    }
  }
  error = "a bytes-like object is required";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool get_pack_bytes_arg(const Value& value, std::string& out, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_to_string(*bytes);
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  if (auto* text = value_as_string(value)) {
    out = string_object_to_string(*text);
    return true;
  }
  error = "argument for 's', 'p', or 'c' must be bytes-like";
  return false;
}

bool get_i64_arg(const Value& value, int64_t& out, std::string& error) {
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
    return true;
  }
  if (value_as_bigint(value) != nullptr && value_bigint_to_i64(value, out)) return true;
  error = "required argument is not an integer";
  return false;
}

bool get_f64_arg(const Value& value, double& out, std::string& error) {
  if (value.tag == ValueTag::Double) {
    out = value.as.f64;
    return true;
  }
  if (value.tag == ValueTag::Int64) {
    out = static_cast<double>(value.as.i64);
    return true;
  }
  error = "required argument is not a float";
  return false;
}

void append_uint(std::string& out, uint64_t value, uint32_t width, bool little) {
  for (uint32_t i = 0; i < width; ++i) {
    const uint32_t shift = little ? i * 8 : (width - 1 - i) * 8;
    out.push_back(static_cast<char>((value >> shift) & 0xffu));
  }
}

uint64_t read_uint(std::string_view input, size_t offset, uint32_t width, bool little) {
  uint64_t value = 0;
  for (uint32_t i = 0; i < width; ++i) {
    const uint32_t shift = little ? i * 8 : (width - 1 - i) * 8;
    value |= static_cast<uint64_t>(static_cast<unsigned char>(input[offset + i])) << shift;
  }
  return value;
}

bool float_to_half(double number, uint16_t& out) {
  const bool negative = std::signbit(number);
  const uint16_t sign = negative ? 0x8000u : 0u;
  const double magnitude = std::fabs(number);
  if (std::isnan(number)) { out = static_cast<uint16_t>(sign | 0x7e00u); return true; }
  if (std::isinf(number)) { out = static_cast<uint16_t>(sign | 0x7c00u); return true; }
  if (magnitude > 65504.0) return false;
  const float value = static_cast<float>(magnitude);
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  int exponent = static_cast<int>((bits >> 23u) & 0xffu) - 127 + 15;
  uint32_t mantissa = bits & 0x7fffffu;
  if (exponent <= 0) {
    if (exponent < -10) { out = sign; return true; }
    mantissa |= 0x800000u;
    const int shift = 14 - exponent;
    uint32_t rounded = mantissa >> shift;
    const uint32_t remainder = mantissa & ((1u << shift) - 1u);
    const uint32_t halfway = 1u << (shift - 1);
    if (remainder > halfway || (remainder == halfway && (rounded & 1u))) ++rounded;
    out = static_cast<uint16_t>(sign | rounded);
    return true;
  }
  uint32_t rounded = mantissa >> 13u;
  const uint32_t remainder = mantissa & 0x1fffu;
  if (remainder > 0x1000u || (remainder == 0x1000u && (rounded & 1u))) {
    if (++rounded == 0x400u) { rounded = 0; ++exponent; }
  }
  if (exponent >= 31) return false;
  out = static_cast<uint16_t>(sign | (static_cast<uint16_t>(exponent) << 10u) | rounded);
  return true;
}

bool get_integer_arg(Runtime& runtime, const Value& value, bool is_signed, uint32_t width,
                     uint64_t& out, std::string& error) {
  Value converted = value;
  if (value.tag != ValueTag::Int64 && value.tag != ValueTag::Bool && value_as_bigint(value) == nullptr) {
    Value index_method;
    std::string lookup_error;
    if (!object_get_attr(value, "__index__", index_method, lookup_error) ||
        !runtime_call_callable(runtime, index_method, nullptr, 0, converted, error)) {
      return struct_fail(runtime, "required argument is not an integer", error);
    }
  }
  const uint32_t bits = width * 8;
  if (is_signed) {
    int64_t number = 0;
    if (converted.tag == ValueTag::Int64) number = converted.as.i64;
    else if (converted.tag == ValueTag::Bool) number = converted.as.b ? 1 : 0;
    else if (!value_bigint_to_i64(converted, number)) return struct_fail(runtime, "argument out of range", error);
    if (bits < 64) {
      const int64_t minimum = -(int64_t{1} << (bits - 1));
      const int64_t maximum = (int64_t{1} << (bits - 1)) - 1;
      if (number < minimum || number > maximum) return struct_fail(runtime, "argument out of range", error);
    }
    out = static_cast<uint64_t>(number);
    return true;
  }
  if (converted.tag == ValueTag::Int64) {
    if (converted.as.i64 < 0) return struct_fail(runtime, "argument out of range", error);
    out = static_cast<uint64_t>(converted.as.i64);
  } else if (converted.tag == ValueTag::Bool) {
    out = converted.as.b ? 1 : 0;
  } else if (!value_bigint_to_u64(converted, out)) {
    return struct_fail(runtime, "argument out of range", error);
  }
  if (bits < 64 && out >= (uint64_t{1} << bits)) return struct_fail(runtime, "argument out of range", error);
  return true;
}

double half_to_double(uint16_t half) {
  const double sign = (half & 0x8000u) ? -1.0 : 1.0;
  const uint16_t exponent = static_cast<uint16_t>((half >> 10u) & 0x1fu);
  const uint16_t mantissa = static_cast<uint16_t>(half & 0x3ffu);
  if (exponent == 0) return sign * std::ldexp(static_cast<double>(mantissa), -24);
  if (exponent == 31) return mantissa == 0 ? sign * std::numeric_limits<double>::infinity()
                                           : std::numeric_limits<double>::quiet_NaN();
  return sign * std::ldexp(1.0 + static_cast<double>(mantissa) / 1024.0,
                           static_cast<int>(exponent) - 15);
}

int64_t sign_extend(uint64_t value, uint32_t width) {
  if (width >= 8) {
    return static_cast<int64_t>(value);
  }
  const uint64_t sign_bit = 1ull << (width * 8 - 1);
  const uint64_t mask = (~0ull) << (width * 8);
  if ((value & sign_bit) != 0) {
    value |= mask;
  }
  return static_cast<int64_t>(value);
}

bool pack_values(Runtime& runtime, const ParsedFormat& parsed, const Value* values, uint32_t value_count, std::string& out, std::string& error) {
  if (value_count != parsed.arg_count) {
    return struct_fail(runtime, "pack expected " + std::to_string(parsed.arg_count) + " items for packing", error);
  }
  uint32_t arg_index = 0;
  out.clear();
  out.reserve(parsed.size);
  for (const auto& item : parsed.items) {
    if (item.code == 'x') {
      out.append(item.count, '\0');
      continue;
    }
    if (item.code == 's') {
      std::string bytes;
      if (!get_pack_bytes_arg(values[arg_index++], bytes, error)) return false;
      if (bytes.size() >= item.count) {
        out.append(bytes.data(), item.count);
      } else {
        out += bytes;
        out.append(item.count - bytes.size(), '\0');
      }
      continue;
    }
    if (item.code == 'p') {
      std::string bytes;
      if (!get_pack_bytes_arg(values[arg_index++], bytes, error)) return false;
      const size_t payload = item.count == 0 ? 0 : std::min<size_t>(bytes.size(), item.count - 1);
      if (item.count != 0) {
        out.push_back(static_cast<char>(std::min<size_t>(payload, 255)));
        out.append(bytes.data(), payload);
        out.append(item.count - 1 - payload, '\0');
      }
      continue;
    }
    for (uint32_t i = 0; i < item.count; ++i) {
      if (item.code == 'c') {
        std::string bytes;
        if (!get_pack_bytes_arg(values[arg_index++], bytes, error)) return false;
        if (bytes.size() != 1) return struct_fail(runtime, "char format requires a bytes object of length 1", error);
        out.push_back(bytes[0]);
      } else if (item.code == 'F' || item.code == 'D') {
        auto* number = value_as_complex(values[arg_index++]);
        if (number == nullptr) return struct_fail(runtime, "required argument is not a complex", error);
        const uint32_t component_size = item.code == 'F' ? 4u : 8u;
        for (double component : {number->real, number->imag}) {
          if (component_size == 4) {
            const float packed = static_cast<float>(component);
            if (std::isfinite(component) && !std::isfinite(packed)) {
              error = "float too large to pack with F format";
              runtime.raise_class_error("OverflowError", error);
              return false;
            }
            uint32_t bits = 0;
            std::memcpy(&bits, &packed, sizeof(bits));
            append_uint(out, bits, component_size, parsed.little_endian);
          } else {
            uint64_t bits = 0;
            std::memcpy(&bits, &component, sizeof(bits));
            append_uint(out, bits, component_size, parsed.little_endian);
          }
        }
      } else if (item.code == '?') {
        bool truth = false;
        if (!runtime_truthy(runtime, values[arg_index++], truth, error)) return false;
        append_uint(out, truth ? 1 : 0, item.size, parsed.little_endian);
      } else if (item.code == 'f') {
        double number = 0;
        if (!get_f64_arg(values[arg_index++], number, error)) return struct_fail(runtime, error, error);
        float f = static_cast<float>(number);
        if (std::isfinite(number) && !std::isfinite(f)) {
          error = "float too large to pack with f format";
          runtime.raise_class_error("OverflowError", error);
          return false;
        }
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        append_uint(out, bits, item.size, parsed.little_endian);
      } else if (item.code == 'e') {
        double number = 0;
        if (!get_f64_arg(values[arg_index++], number, error)) return struct_fail(runtime, error, error);
        uint16_t bits = 0;
        if (!float_to_half(number, bits)) {
          error = "float too large to pack with e format";
          runtime.raise_class_error("OverflowError", error);
          return false;
        }
        append_uint(out, bits, item.size, parsed.little_endian);
      } else if (item.code == 'd') {
        double number = 0;
        if (!get_f64_arg(values[arg_index++], number, error)) return struct_fail(runtime, error, error);
        uint64_t bits = 0;
        std::memcpy(&bits, &number, sizeof(bits));
        append_uint(out, bits, item.size, parsed.little_endian);
      } else {
        const bool is_signed = item.code == 'b' || item.code == 'h' || item.code == 'i' ||
                               item.code == 'l' || item.code == 'q' || item.code == 'n';
        uint64_t number = 0;
        if (!get_integer_arg(runtime, values[arg_index++], is_signed, item.size, number, error)) return false;
        append_uint(out, number, item.size, parsed.little_endian);
      }
    }
  }
  return true;
}

bool unpack_values(Runtime& runtime, const ParsedFormat& parsed, std::string_view buffer, size_t offset, Value& out, std::string& error) {
  if (offset > buffer.size() || buffer.size() - offset < parsed.size) {
    return struct_fail(runtime, "unpack requires a buffer of " + std::to_string(parsed.size) + " bytes", error);
  }
  std::vector<Value> values;
  values.reserve(parsed.arg_count);
  size_t cursor = offset;
  for (const auto& item : parsed.items) {
    if (item.code == 'x') {
      cursor += item.count;
      continue;
    }
    if (item.code == 's') {
      values.push_back(Value::bytes(buffer.substr(cursor, item.count)));
      cursor += item.count;
      continue;
    }
    if (item.code == 'p') {
      size_t payload = item.count == 0 ? 0 : static_cast<unsigned char>(buffer[cursor]);
      payload = std::min(payload, item.count == 0 ? size_t{0} : item.count - 1);
      values.push_back(Value::bytes(buffer.substr(cursor + (item.count == 0 ? 0 : 1), payload)));
      cursor += item.count;
      continue;
    }
    for (uint32_t i = 0; i < item.count; ++i) {
      if (item.code == 'c') {
        values.push_back(Value::bytes(buffer.substr(cursor, 1)));
      } else if (item.code == 'F' || item.code == 'D') {
        const uint32_t component_size = item.code == 'F' ? 4u : 8u;
        double components[2] = {};
        for (uint32_t component = 0; component < 2; ++component) {
          const uint64_t bits = read_uint(
              buffer, cursor + component * component_size, component_size, parsed.little_endian);
          if (component_size == 4) {
            const uint32_t narrow = static_cast<uint32_t>(bits);
            float unpacked = 0;
            std::memcpy(&unpacked, &narrow, sizeof(unpacked));
            components[component] = unpacked;
          } else {
            std::memcpy(&components[component], &bits, sizeof(bits));
          }
        }
        values.push_back(Value::complex(components[0], components[1]));
      } else if (item.code == '?') {
        values.push_back(Value::boolean(read_uint(buffer, cursor, item.size, parsed.little_endian) != 0));
      } else if (item.code == 'f') {
        uint32_t bits = static_cast<uint32_t>(read_uint(buffer, cursor, item.size, parsed.little_endian));
        float f = 0;
        std::memcpy(&f, &bits, sizeof(f));
        values.push_back(Value::number(static_cast<double>(f)));
      } else if (item.code == 'd') {
        uint64_t bits = read_uint(buffer, cursor, item.size, parsed.little_endian);
        double d = 0;
        std::memcpy(&d, &bits, sizeof(d));
        values.push_back(Value::number(d));
      } else if (item.code == 'e') {
        values.push_back(Value::number(half_to_double(
            static_cast<uint16_t>(read_uint(buffer, cursor, item.size, parsed.little_endian)))));
      } else {
        const bool is_signed = item.code == 'b' || item.code == 'h' || item.code == 'i' || item.code == 'l' || item.code == 'q' || item.code == 'n';
        const uint64_t raw = read_uint(buffer, cursor, item.size, parsed.little_endian);
        if (!is_signed && item.size == 8 && raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
          values.push_back(value_bigint_from_u64(raw));
        } else {
          values.push_back(Value::int64(is_signed ? sign_extend(raw, item.size) : static_cast<int64_t>(raw)));
        }
      }
      cursor += item.size;
    }
  }
  out = Value::tuple(std::move(values));
  return true;
}

bool struct_calcsize(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "struct.calcsize() expected format";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) return false;
  int64_t size = 0;
  if (!calcsize_text(runtime, format, size, error)) return false;
  value_set_int64(out, size);
  return true;
}

bool struct_pack(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "struct.pack() expected format";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) return false;
  ParsedFormat parsed;
  if (!parse_format(runtime, format, parsed, error)) return false;
  std::string bytes;
  if (!pack_values(runtime, parsed, args + 1, argc - 1, bytes, error)) return false;
  out = Value::bytes(std::move(bytes));
  return true;
}

bool write_into_buffer(Runtime& runtime, Value& target, size_t offset, const std::string& bytes, std::string& error) {
  if (auto* bytearray = value_as_bytearray(target)) {
    if (offset > bytearray->value.size() || bytearray->value.size() - offset < bytes.size()) {
      return struct_fail(runtime, "pack_into requires a buffer large enough", error);
    }
    std::copy(bytes.begin(), bytes.end(), bytearray->value.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
  }
  if (auto* view = value_as_memoryview(target)) {
    if (view->readonly) {
      return struct_fail(runtime, "cannot modify read-only memory", error);
    }
    if (offset > view->size || view->size - offset < bytes.size()) {
      return struct_fail(runtime, "pack_into requires a buffer large enough", error);
    }
    if (char* data = memoryview_object_writable_data(*view)) {
      std::copy(bytes.begin(), bytes.end(), data + offset);
      return true;
    }
  }
  if (value_as_instance(target) != nullptr) {
    Value payload;
    std::string ignored;
    if (object_get_attr(target, "__xlang3_bytes_value__", payload, ignored)) {
      if (auto* bytearray = value_as_bytearray(payload)) {
        if (offset > bytearray->value.size() || bytearray->value.size() - offset < bytes.size()) {
          return struct_fail(runtime, "pack_into requires a buffer large enough", error);
        }
        std::copy(bytes.begin(), bytes.end(), bytearray->value.begin() + static_cast<std::ptrdiff_t>(offset));
        return true;
      }
    }
  }
  error = "argument must be read-write bytes-like object";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool struct_pack_into(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3) {
    error = "struct.pack_into() expected format, buffer, offset, values";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) return false;
  int64_t offset = 0;
  if (!get_i64_arg(args[2], offset, error)) {
    runtime.raise_class_error(value_as_bigint(args[2]) != nullptr ? "OverflowError" : "TypeError", error);
    return false;
  }
  ParsedFormat parsed;
  if (!parse_format(runtime, format, parsed, error)) return false;
  std::string bytes;
  if (!pack_values(runtime, parsed, args + 3, argc - 3, bytes, error)) return false;
  Value target = args[1];
  size_t target_size = 0;
  bool writable_buffer = false;
  if (auto* value = value_as_bytearray(target)) { target_size = value->value.size(); writable_buffer = true; }
  else if (auto* value = value_as_memoryview(target)) { target_size = value->size; writable_buffer = !value->readonly; }
  else if (value_as_instance(target) != nullptr) {
    Value payload;
    std::string ignored;
    if (object_get_attr(target, "__xlang3_bytes_value__", payload, ignored)) {
      if (auto* value = value_as_bytearray(payload)) { target_size = value->value.size(); writable_buffer = true; }
    }
  }
  const int64_t original_offset = offset;
  if (offset < 0) {
    if (offset < -static_cast<int64_t>(target_size)) {
      return struct_fail(runtime, "offset " + std::to_string(offset) + " out of range for " +
          std::to_string(target_size) + "-byte buffer", error);
    }
    offset += static_cast<int64_t>(target_size);
  }
  if (writable_buffer && (static_cast<uint64_t>(offset) > target_size ||
      bytes.size() > target_size - static_cast<size_t>(offset))) {
    if (original_offset < 0) {
      return struct_fail(runtime, "no space to pack " + std::to_string(bytes.size()) +
          " bytes at offset " + std::to_string(original_offset), error);
    }
    return struct_fail(runtime, "pack_into requires a buffer of at least " +
        std::to_string(static_cast<uint64_t>(offset) + bytes.size()) + " bytes for packing " +
        std::to_string(bytes.size()) + " bytes at offset " + std::to_string(original_offset) +
        " (actual buffer size is " + std::to_string(target_size) + ")", error);
  }
  if (!write_into_buffer(runtime, target, static_cast<size_t>(offset), bytes, error)) return false;
  value_set_none(out);
  return true;
}

bool struct_unpack(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "struct.unpack() expected format and buffer";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) return false;
  std::string_view buffer;
  if (!get_bytes_like(runtime, args[1], buffer, error)) return false;
  ParsedFormat parsed;
  if (!parse_format(runtime, format, parsed, error)) return false;
  if (buffer.size() != parsed.size) {
    return struct_fail(runtime, "unpack requires a buffer of " + std::to_string(parsed.size) + " bytes", error);
  }
  return unpack_values(runtime, parsed, buffer, 0, out, error);
}

bool struct_unpack_from(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "struct.unpack_from() expected format, buffer, optional offset";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) return false;
  std::string_view buffer;
  if (!get_bytes_like(runtime, args[1], buffer, error)) return false;
  int64_t offset = 0;
  if (argc == 3 && !get_i64_arg(args[2], offset, error)) {
    runtime.raise_class_error(value_as_bigint(args[2]) != nullptr ? "OverflowError" : "TypeError", error);
    return false;
  }
  const int64_t original_offset = offset;
  if (offset < 0) {
    if (offset < -static_cast<int64_t>(buffer.size())) {
      return struct_fail(runtime, "offset " + std::to_string(offset) + " out of range for " +
          std::to_string(buffer.size()) + "-byte buffer", error);
    }
    offset += static_cast<int64_t>(buffer.size());
  }
  ParsedFormat parsed;
  if (!parse_format(runtime, format, parsed, error)) return false;
  if (static_cast<uint64_t>(offset) > buffer.size() || parsed.size > buffer.size() - static_cast<size_t>(offset)) {
    if (original_offset < 0) {
      return struct_fail(runtime, "not enough data to unpack " + std::to_string(parsed.size) +
          " bytes at offset " + std::to_string(original_offset), error);
    }
    return struct_fail(runtime, "unpack_from requires a buffer of at least " +
        std::to_string(static_cast<uint64_t>(offset) + parsed.size) + " bytes for unpacking " +
        std::to_string(parsed.size) + " bytes at offset " + std::to_string(original_offset) +
        " (actual buffer size is " + std::to_string(buffer.size()) + ")", error);
  }
  return unpack_values(runtime, parsed, buffer, static_cast<size_t>(offset), out, error);
}

bool struct_iter_unpack(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "struct.iter_unpack() expected format and buffer";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) return false;
  std::string_view buffer;
  if (!get_bytes_like(runtime, args[1], buffer, error)) return false;
  ParsedFormat parsed;
  if (!parse_format(runtime, format, parsed, error)) return false;
  if (parsed.size == 0 || (buffer.size() % parsed.size) != 0) {
    return struct_fail(runtime, "iterative unpacking requires a buffer of a multiple of format size", error);
  }
  std::vector<Value> rows;
  for (size_t offset = 0; offset < buffer.size(); offset += parsed.size) {
    Value row;
    if (!unpack_values(runtime, parsed, buffer, offset, row, error)) return false;
    rows.push_back(std::move(row));
  }
  out = Value::sequence_iterator(Value::list(std::move(rows)), 0);
  return true;
}

bool struct_class_new(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || value_as_class(args[0]) == nullptr) {
    error = "Struct.__new__() requires the Struct type";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::instance(args[0]);
  std::string ignored;
  object_set_attr(out, "__xlang3_struct_size__", Value::int64(-1), ignored);
  return true;
}

bool struct_format_get(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || !object_get_attr(args[0], "__xlang3_struct_format__", out, error)) {
    error = "uninitialized Struct object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  return true;
}

bool struct_size_get(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "Struct.size getter expected self"; return false; }
  if (!object_get_attr(args[0], "__xlang3_struct_size__", out, error)) {
    out = Value::int64(-1);
    error.clear();
  }
  return true;
}

bool struct_method_sizeof(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value format;
  if (argc != 1 || !object_get_attr(args[0], "__xlang3_struct_format__", format, error)) {
    error = "uninitialized Struct object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  out = Value::int64(static_cast<int64_t>(sizeof(InstanceObject)));
  return true;
}

bool struct_class_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Struct() expected format";
    return false;
  }
  std::string format;
  if (!get_format(args[1], format, error)) return false;
  if (value_as_string(args[1]) != nullptr &&
      std::any_of(format.begin(), format.end(), [](unsigned char ch) { return ch >= 0x80; })) {
    error = "'ascii' codec can't encode character in struct format";
    runtime.raise_class_error("UnicodeEncodeError", error);
    return false;
  }
  ParsedFormat parsed;
  if (!parse_format(runtime, format, parsed, error)) return false;
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "__xlang3_struct_format__", Value::string(format), ignored);
  object_set_attr(self, "__xlang3_struct_size__", Value::int64(parsed.size), ignored);
  value_set_none(out);
  return true;
}

bool struct_method_repr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Struct.__repr__() expected self";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "__xlang3_struct_format__", format, error)) {
    error = "uninitialized Struct object";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  out = Value::string("Struct(" + value_to_repr(format) + ")");
  return true;
}

bool struct_method_calcsize(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 1) {
    error = "Struct.calcsize() expected self";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "__xlang3_struct_format__", format, error)) return false;
  return struct_calcsize(runtime, &format, 1, out, error, data);
}

bool struct_method_pack(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc < 1) {
    error = "Struct.pack() expected self";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "__xlang3_struct_format__", format, error)) return false;
  std::vector<Value> call_args;
  call_args.reserve(argc);
  call_args.push_back(format);
  for (uint32_t i = 1; i < argc; ++i) call_args.push_back(args[i]);
  return struct_pack(runtime, call_args.data(), static_cast<uint32_t>(call_args.size()), out, error, data);
}

bool struct_method_unpack(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 2) {
    error = "Struct.unpack() expected buffer";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "__xlang3_struct_format__", format, error)) return false;
  Value call_args[2] = {format, args[1]};
  return struct_unpack(runtime, call_args, 2, out, error, data);
}

bool struct_method_pack_into(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc < 3) {
    error = "Struct.pack_into() expected buffer, offset, values";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "__xlang3_struct_format__", format, error)) return false;
  std::vector<Value> call_args;
  call_args.reserve(argc + 1);
  call_args.push_back(format);
  for (uint32_t i = 1; i < argc; ++i) call_args.push_back(args[i]);
  return struct_pack_into(runtime, call_args.data(), static_cast<uint32_t>(call_args.size()), out, error, data);
}

bool struct_method_unpack_from(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc < 2 || argc > 3) {
    error = "Struct.unpack_from() expected buffer and optional offset";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "__xlang3_struct_format__", format, error)) return false;
  Value call_args[3] = {format, args[1], argc == 3 ? args[2] : Value::int64(0)};
  return struct_unpack_from(runtime, call_args, argc == 3 ? 3 : 2, out, error, data);
}

bool struct_method_unpack_from_kw(Runtime& runtime, const Value* args, uint32_t argc,
                                  const NativeKeywordArg* kwargs, uint32_t kwargc,
                                  Value& out, std::string& error, void* data) {
  if (argc < 1 || argc > 3) {
    error = "Struct.unpack_from() invalid arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> values(args, args + argc);
  bool have_buffer = argc >= 2;
  bool have_offset = argc >= 3;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (kwargs[i].value == nullptr || (name != "buffer" && name != "offset")) {
      error = "Struct.unpack_from() got an unexpected keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (name == "buffer") {
      if (have_buffer) { error = "multiple values for buffer"; runtime.raise_class_error("TypeError", error); return false; }
      if (values.size() == 1) values.push_back(*kwargs[i].value); else values[1] = *kwargs[i].value;
      have_buffer = true;
    } else {
      if (have_offset) { error = "multiple values for offset"; runtime.raise_class_error("TypeError", error); return false; }
      if (!have_buffer) { error = "missing buffer"; runtime.raise_class_error("TypeError", error); return false; }
      if (values.size() == 2) values.push_back(*kwargs[i].value); else values[2] = *kwargs[i].value;
      have_offset = true;
    }
  }
  return struct_method_unpack_from(runtime, values.data(), static_cast<uint32_t>(values.size()), out, error, data);
}

bool struct_method_iter_unpack(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 2) {
    error = "Struct.iter_unpack() expected buffer";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "__xlang3_struct_format__", format, error)) return false;
  Value call_args[2] = {format, args[1]};
  return struct_iter_unpack(runtime, call_args, 2, out, error, data);
}

bool struct_clearcache(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "_struct._clearcache() expected no arguments";
    return false;
  }
  value_set_none(out);
  return true;
}

} // namespace

void register_struct_module(Runtime& runtime) {
  g_struct_error_class = Value::class_object(
      "error",
      {
          {"__module__", Value::string("struct")},
      },
      runtime.find_builtin("Exception") != nullptr ? *runtime.find_builtin("Exception") : Value::invalid());
  g_struct_class = Value::class_object(
      "Struct",
      {
          {"__module__", Value::string("struct")},
          {"__new__", Value::static_method(runtime.make_native_function("struct.Struct.__new__", struct_class_new))},
          {"__init__", runtime.make_native_function("struct.Struct.__init__", struct_class_init)},
          {"__repr__", runtime.make_native_function("struct.Struct.__repr__", struct_method_repr)},
          {"__sizeof__", runtime.make_native_function("struct.Struct.__sizeof__", struct_method_sizeof)},
          {"format", Value::property(runtime.make_native_function("struct.Struct.format.__get__", struct_format_get),
              Value::none(), Value::none(), Value::none())},
          {"size", Value::property(runtime.make_native_function("struct.Struct.size.__get__", struct_size_get),
              Value::none(), Value::none(), Value::none())},
          {"calcsize", runtime.make_native_function("struct.Struct.calcsize", struct_method_calcsize)},
          {"pack", runtime.make_native_function("struct.Struct.pack", struct_method_pack)},
          {"pack_into", runtime.make_native_function("struct.Struct.pack_into", struct_method_pack_into)},
          {"unpack", runtime.make_native_function("struct.Struct.unpack", struct_method_unpack)},
          {"unpack_from", runtime.make_native_function("struct.Struct.unpack_from", struct_method_unpack_from,
              nullptr, nullptr, nullptr, false, struct_method_unpack_from_kw)},
          {"iter_unpack", runtime.make_native_function("struct.Struct.iter_unpack", struct_method_iter_unpack)},
      });
  NativeModuleBuilder builder(runtime, "_struct");
  builder.value("__doc__", Value::string("Functions to convert between Python values and C structs."))
      .function("calcsize", struct_calcsize)
      .function("pack", struct_pack)
      .function("pack_into", struct_pack_into)
      .function("unpack", struct_unpack)
      .function("unpack_from", struct_unpack_from)
      .function("iter_unpack", struct_iter_unpack)
      .function("_clearcache", struct_clearcache)
      .value("Struct", g_struct_class)
      .value("error", g_struct_error_class);
  runtime.register_module("_struct", builder.finish());
}

} // namespace xlang3
