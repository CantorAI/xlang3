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
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

namespace xlang3 {

namespace {

bool value_sequence_to_vector(const Value& value, std::vector<Value>& out, std::string& error) {
  Value iterator;
  if (!sequence_get_iter(value, iterator, error)) {
    return false;
  }
  while (true) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    out.push_back(std::move(item));
  }
}

struct FormatChunk {
  std::string literal;
  std::string field;
  std::string spec;
  std::string conversion;
};

bool parse_format_chunks(const std::string& text, std::vector<FormatChunk>& chunks, std::string& error) {
  std::string literal;
  for (size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '{') {
      if (i + 1 < text.size() && text[i + 1] == '{') {
        literal.push_back('{');
        ++i;
        continue;
      }
      const size_t close = text.find('}', i + 1);
      if (close == std::string::npos) {
        error = "Single '{' encountered in format string";
        return false;
      }
      std::string field_text = text.substr(i + 1, close - i - 1);
      FormatChunk chunk;
      chunk.literal = literal;
      literal.clear();
      const size_t bang = field_text.find('!');
      const size_t colon = field_text.find(':');
      size_t field_end = field_text.size();
      if (bang != std::string::npos) field_end = std::min(field_end, bang);
      if (colon != std::string::npos) field_end = std::min(field_end, colon);
      chunk.field = field_text.substr(0, field_end);
      if (bang != std::string::npos) {
        const size_t conv_end = colon == std::string::npos ? field_text.size() : colon;
        chunk.conversion = field_text.substr(bang + 1, conv_end - bang - 1);
      }
      if (colon != std::string::npos) {
        chunk.spec = field_text.substr(colon + 1);
      }
      chunks.push_back(std::move(chunk));
      i = close;
      continue;
    }
    if (ch == '}') {
      if (i + 1 < text.size() && text[i + 1] == '}') {
        literal.push_back('}');
        ++i;
        continue;
      }
      error = "Single '}' encountered in format string";
      return false;
    }
    literal.push_back(ch);
  }
  if (!literal.empty() || chunks.empty()) {
    FormatChunk chunk;
    chunk.literal = literal;
    chunks.push_back(std::move(chunk));
  }
  return true;
}

bool formatter_parser(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_string.formatter_parser() expected format string";
    return false;
  }
  auto* text = value_as_string(args[0]);
  if (text == nullptr) {
    error = "_string.formatter_parser() argument must be str";
    return false;
  }
  std::vector<FormatChunk> chunks;
  if (!parse_format_chunks(string_object_to_string(*text), chunks, error)) {
    return false;
  }
  std::vector<Value> result;
  for (const auto& chunk : chunks) {
    result.push_back(Value::tuple({
        Value::string(chunk.literal),
        chunk.field.empty() ? Value::none() : Value::string(chunk.field),
        chunk.spec.empty() ? Value::none() : Value::string(chunk.spec),
        chunk.conversion.empty() ? Value::none() : Value::string(chunk.conversion),
    }));
  }
  out = Value::list(std::move(result));
  return true;
}

bool formatter_field_name_split(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_string.formatter_field_name_split() expected field name";
    return false;
  }
  auto* text = value_as_string(args[0]);
  if (text == nullptr) {
    error = "_string.formatter_field_name_split() argument must be str";
    return false;
  }
  const std::string field = string_object_to_string(*text);
  size_t split = field.size();
  for (size_t i = 0; i < field.size(); ++i) {
    if (field[i] == '.' || field[i] == '[') {
      split = i;
      break;
    }
  }
  std::vector<Value> lookups;
  for (size_t i = split; i < field.size();) {
    if (field[i] == '.') {
      size_t j = i + 1;
      while (j < field.size() && field[j] != '.' && field[j] != '[') {
        ++j;
      }
      lookups.push_back(Value::tuple({Value::boolean(true), Value::string(field.substr(i + 1, j - i - 1))}));
      i = j;
    } else if (field[i] == '[') {
      const size_t j = field.find(']', i + 1);
      if (j == std::string::npos) {
        error = "_string.formatter_field_name_split() malformed field";
        return false;
      }
      lookups.push_back(Value::tuple({Value::boolean(false), Value::string(field.substr(i + 1, j - i - 1))}));
      i = j + 1;
    } else {
      ++i;
    }
  }
  out = Value::tuple({Value::string(field.substr(0, split)), Value::list(std::move(lookups))});
  return true;
}

bool formatter_format(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "Formatter.format() expected format_string";
    return false;
  }
  Value method;
  if (!attribute_get(args[1], "format", method, error)) {
    return false;
  }
  return runtime_call_callable(runtime, method, argc > 2 ? args + 2 : nullptr, argc > 2 ? argc - 2 : 0, out, error);
}

bool formatter_vformat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 4) {
    error = "Formatter.vformat() expected format_string, args, kwargs";
    return false;
  }
  Value method;
  if (!attribute_get(args[1], "format", method, error)) {
    return false;
  }
  std::vector<Value> call_args;
  if (!value_sequence_to_vector(args[2], call_args, error)) {
    return false;
  }
  return runtime_call_callable(runtime, method, call_args.empty() ? nullptr : call_args.data(), static_cast<uint32_t>(call_args.size()), out, error);
}

bool formatter_parse(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  if (argc != 2) {
    error = "Formatter.parse() expected format_string";
    return false;
  }
  return formatter_parser(runtime, args + 1, 1, out, error, data);
}

bool formatter_get_value(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "Formatter.get_value() expected key, args, kwargs";
    return false;
  }
  if (args[1].tag == ValueTag::Int64) {
    return sequence_get_item(args[2], args[1], out, error);
  }
  if (auto* dict = value_as_dict(args[3])) {
    for (const auto& entry : dict->entries) {
      if (value_to_string(entry.first) == value_to_string(args[1])) {
        out = entry.second;
        return true;
      }
    }
  }
  error = "Formatter.get_value() key not found";
  return false;
}

bool formatter_format_field(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "Formatter.format_field() expected value and optional format_spec";
    return false;
  }
  const Value* format_builtin = runtime.find_builtin("format");
  if (format_builtin == nullptr) {
    out = Value::string(value_to_string(args[1]));
    return true;
  }
  Value call_args[2] = {args[1], argc == 3 ? args[2] : Value::string("")};
  return runtime_call_callable(runtime, *format_builtin, call_args, argc == 3 ? 2 : 1, out, error);
}

bool formatter_convert_field(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "Formatter.convert_field() expected value and conversion";
    return false;
  }
  if (auto* conversion = value_as_string(args[2])) {
    const std::string conv = string_object_to_string(*conversion);
    if (conv == "s" || conv == "r" || conv == "a") {
      out = Value::string(value_to_string(args[1]));
      return true;
    }
  }
  out = args[1];
  return true;
}

} // namespace

void register_string_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_string");
  builder.function("formatter_parser", formatter_parser)
      .function("formatter_field_name_split", formatter_field_name_split);
  runtime.register_module("_string", builder.finish());

  NativeModuleBuilder public_builder(runtime, "string");
  Value formatter_type = Value::class_object(
      "Formatter",
      {
          {"__module__", Value::string("string")},
          {"format", runtime.make_native_function("string.Formatter.format", formatter_format)},
          {"vformat", runtime.make_native_function("string.Formatter.vformat", formatter_vformat)},
          {"parse", runtime.make_native_function("string.Formatter.parse", formatter_parse)},
          {"get_value", runtime.make_native_function("string.Formatter.get_value", formatter_get_value)},
          {"format_field", runtime.make_native_function("string.Formatter.format_field", formatter_format_field)},
          {"convert_field", runtime.make_native_function("string.Formatter.convert_field", formatter_convert_field)},
      });
  public_builder.value("ascii_lowercase", Value::string("abcdefghijklmnopqrstuvwxyz"))
      .value("ascii_uppercase", Value::string("ABCDEFGHIJKLMNOPQRSTUVWXYZ"))
      .value("ascii_letters", Value::string("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"))
      .value("digits", Value::string("0123456789"))
      .value("hexdigits", Value::string("0123456789abcdefABCDEF"))
      .value("octdigits", Value::string("01234567"))
      .value("punctuation", Value::string("!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~"))
      .value("whitespace", Value::string(" \t\n\r\v\f"))
      .value("printable", Value::string("0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~ \t\n\r\v\f"))
      .value("Formatter", formatter_type);
  runtime.register_module("string", public_builder.finish());
}

} // namespace xlang3
