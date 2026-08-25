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

#include <algorithm>
#include <cctype>
#include <cstring>

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
  auto* text = value_as_string(value);
  if (text == nullptr) {
    error = "struct format must be str";
    return false;
  }
  out = string_object_to_string(*text);
  return true;
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
    return 8;
  case 'P':
  case 'n':
  case 'N':
    return static_cast<uint32_t>(sizeof(void*));
  default:
    return 0;
  }
}

bool parse_format(Runtime& runtime, const std::string& format, ParsedFormat& parsed, std::string& error) {
  uint32_t repeat = 0;
  bool prefix_seen = false;
  for (char ch : format) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
    if (ch == '@' || ch == '=' || ch == '<' || ch == '>' || ch == '!') {
      if (prefix_seen || !parsed.items.empty() || repeat != 0) {
        return struct_fail(runtime, "bad char in struct format", error);
      }
      prefix_seen = true;
      parsed.little_endian = !(ch == '>' || ch == '!');
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      repeat = repeat * 10 + static_cast<uint32_t>(ch - '0');
      continue;
    }
    const uint32_t count = repeat == 0 ? 1 : repeat;
    repeat = 0;
    if (ch == 's' || ch == 'p') {
      parsed.items.push_back({ch, count, 1});
      parsed.size += count;
      parsed.arg_count += 1;
      continue;
    }
    const uint32_t item_size = primitive_size(ch);
    if (item_size == 0) {
      return struct_fail(runtime, "bad char in struct format", error);
    }
    parsed.items.push_back({ch, count, item_size});
    parsed.size += count * item_size;
    if (ch != 'x') {
      parsed.arg_count += count;
    }
  }
  if (repeat != 0) {
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

bool get_bytes_like(const Value& value, std::string& out, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_to_string(*bytes);
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    if (auto* owner_bytes = value_as_bytes(view->owner)) {
      const auto text = bytes_object_to_string(*owner_bytes);
      out.assign(text.data() + view->offset, view->size);
      return true;
    }
    if (auto* owner_array = value_as_bytearray(view->owner)) {
      out.assign(owner_array->value.data() + view->offset, view->size);
      return true;
    }
  }
  error = "a bytes-like object is required";
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

uint64_t read_uint(const std::string& input, size_t offset, uint32_t width, bool little) {
  uint64_t value = 0;
  for (uint32_t i = 0; i < width; ++i) {
    const uint32_t shift = little ? i * 8 : (width - 1 - i) * 8;
    value |= static_cast<uint64_t>(static_cast<unsigned char>(input[offset + i])) << shift;
  }
  return value;
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
      } else if (item.code == '?') {
        append_uint(out, value_truthy(values[arg_index++]) ? 1 : 0, item.size, parsed.little_endian);
      } else if (item.code == 'f') {
        double number = 0;
        if (!get_f64_arg(values[arg_index++], number, error)) return false;
        float f = static_cast<float>(number);
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(bits));
        append_uint(out, bits, item.size, parsed.little_endian);
      } else if (item.code == 'd') {
        double number = 0;
        if (!get_f64_arg(values[arg_index++], number, error)) return false;
        uint64_t bits = 0;
        std::memcpy(&bits, &number, sizeof(bits));
        append_uint(out, bits, item.size, parsed.little_endian);
      } else {
        int64_t number = 0;
        if (!get_i64_arg(values[arg_index++], number, error)) return false;
        append_uint(out, static_cast<uint64_t>(number), item.size, parsed.little_endian);
      }
    }
  }
  return true;
}

bool unpack_values(Runtime& runtime, const ParsedFormat& parsed, const std::string& buffer, size_t offset, Value& out, std::string& error) {
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
      } else {
        const bool is_signed = item.code == 'b' || item.code == 'h' || item.code == 'i' || item.code == 'l' || item.code == 'q' || item.code == 'n';
        const uint64_t raw = read_uint(buffer, cursor, item.size, parsed.little_endian);
        values.push_back(Value::int64(is_signed ? sign_extend(raw, item.size) : static_cast<int64_t>(raw)));
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
    if (auto* owner = value_as_bytearray(view->owner)) {
      std::copy(bytes.begin(), bytes.end(), owner->value.begin() + static_cast<std::ptrdiff_t>(view->offset + offset));
      return true;
    }
  }
  return struct_fail(runtime, "argument must be read-write bytes-like object", error);
}

bool struct_pack_into(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3) {
    error = "struct.pack_into() expected format, buffer, offset, values";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) return false;
  int64_t offset = 0;
  if (!get_i64_arg(args[2], offset, error)) return false;
  if (offset < 0) return struct_fail(runtime, "offset must be non-negative", error);
  ParsedFormat parsed;
  if (!parse_format(runtime, format, parsed, error)) return false;
  std::string bytes;
  if (!pack_values(runtime, parsed, args + 3, argc - 3, bytes, error)) return false;
  Value target = args[1];
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
  std::string buffer;
  if (!get_bytes_like(args[1], buffer, error)) return false;
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
  std::string buffer;
  if (!get_bytes_like(args[1], buffer, error)) return false;
  int64_t offset = 0;
  if (argc == 3 && !get_i64_arg(args[2], offset, error)) return false;
  if (offset < 0) return struct_fail(runtime, "offset must be non-negative", error);
  ParsedFormat parsed;
  if (!parse_format(runtime, format, parsed, error)) return false;
  return unpack_values(runtime, parsed, buffer, static_cast<size_t>(offset), out, error);
}

bool struct_iter_unpack(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "struct.iter_unpack() expected format and buffer";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) return false;
  std::string buffer;
  if (!get_bytes_like(args[1], buffer, error)) return false;
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
  out = Value::list(std::move(rows));
  return true;
}

bool struct_class_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Struct() expected format";
    return false;
  }
  std::string format;
  if (!get_format(args[1], format, error)) return false;
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "format", Value::string(format), ignored);
  value_set_none(out);
  return true;
}

bool struct_method_calcsize(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 1) {
    error = "Struct.calcsize() expected self";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "format", format, error)) return false;
  return struct_calcsize(runtime, &format, 1, out, error, data);
}

bool struct_method_pack(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc < 1) {
    error = "Struct.pack() expected self";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "format", format, error)) return false;
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
  if (!object_get_attr(args[0], "format", format, error)) return false;
  Value call_args[2] = {format, args[1]};
  return struct_unpack(runtime, call_args, 2, out, error, data);
}

bool struct_method_pack_into(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc < 3) {
    error = "Struct.pack_into() expected buffer, offset, values";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "format", format, error)) return false;
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
  if (!object_get_attr(args[0], "format", format, error)) return false;
  Value call_args[3] = {format, args[1], argc == 3 ? args[2] : Value::int64(0)};
  return struct_unpack_from(runtime, call_args, argc == 3 ? 3 : 2, out, error, data);
}

bool struct_method_iter_unpack(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 2) {
    error = "Struct.iter_unpack() expected buffer";
    return false;
  }
  Value format;
  if (!object_get_attr(args[0], "format", format, error)) return false;
  Value call_args[2] = {format, args[1]};
  return struct_iter_unpack(runtime, call_args, 2, out, error, data);
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
          {"__init__", runtime.make_native_function("struct.Struct.__init__", struct_class_init)},
          {"calcsize", runtime.make_native_function("struct.Struct.calcsize", struct_method_calcsize)},
          {"pack", runtime.make_native_function("struct.Struct.pack", struct_method_pack)},
          {"pack_into", runtime.make_native_function("struct.Struct.pack_into", struct_method_pack_into)},
          {"unpack", runtime.make_native_function("struct.Struct.unpack", struct_method_unpack)},
          {"unpack_from", runtime.make_native_function("struct.Struct.unpack_from", struct_method_unpack_from)},
          {"iter_unpack", runtime.make_native_function("struct.Struct.iter_unpack", struct_method_iter_unpack)},
      });
  NativeModuleBuilder builder(runtime, "struct");
  builder.function("calcsize", struct_calcsize)
      .function("pack", struct_pack)
      .function("pack_into", struct_pack_into)
      .function("unpack", struct_unpack)
      .function("unpack_from", struct_unpack_from)
      .function("iter_unpack", struct_iter_unpack)
      .value("Struct", g_struct_class)
      .value("error", g_struct_error_class);
  runtime.register_module("struct", builder.finish());
}

} // namespace xlang3
