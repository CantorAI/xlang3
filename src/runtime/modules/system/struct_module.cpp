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

#include <cctype>

namespace xlang3 {

namespace {

bool get_format(const Value& value, std::string& out, std::string& error) {
  auto* text = value_as_string(value);
  if (text == nullptr) {
    error = "struct format must be str";
    return false;
  }
  out = string_object_to_string(*text);
  return true;
}

int64_t primitive_size(char ch) {
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
    return static_cast<int64_t>(sizeof(void*));
  default:
    return -1;
  }
}

bool calcsize_text(const std::string& format, int64_t& size, std::string& error) {
  size = 0;
  int64_t repeat = 0;
  for (char ch : format) {
    if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
    if (ch == '@' || ch == '=' || ch == '<' || ch == '>' || ch == '!') {
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      repeat = repeat * 10 + (ch - '0');
      continue;
    }
    const int64_t count = repeat == 0 ? 1 : repeat;
    repeat = 0;
    if (ch == 's' || ch == 'p') {
      size += count;
      continue;
    }
    const int64_t item_size = primitive_size(ch);
    if (item_size < 0) {
      error = "bad char in struct format";
      return false;
    }
    size += count * item_size;
  }
  return true;
}

bool struct_calcsize(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "struct.calcsize() expected format";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) {
    return false;
  }
  int64_t size = 0;
  if (!calcsize_text(format, size, error)) {
    return false;
  }
  value_set_int64(out, size);
  return true;
}

bool struct_pack(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "struct.pack() expected format";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) {
    return false;
  }
  int64_t size = 0;
  if (!calcsize_text(format, size, error)) {
    return false;
  }
  out = Value::bytes(std::string(static_cast<size_t>(size), '\0'));
  return true;
}

bool struct_unpack(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "struct.unpack() expected format and buffer";
    return false;
  }
  std::string format;
  if (!get_format(args[0], format, error)) {
    return false;
  }
  int64_t size = 0;
  if (!calcsize_text(format, size, error)) {
    return false;
  }
  (void)size;
  out = Value::tuple({});
  return true;
}

} // namespace

void register_struct_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "struct");
  builder.function("calcsize", struct_calcsize)
      .function("pack", struct_pack)
      .function("unpack", struct_unpack)
      .value("error", Value::class_object("error", {}));
  runtime.register_module("struct", builder.finish());
}

} // namespace xlang3
