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

#include <cctype>
#include <cstdio>

namespace xlang3 {

namespace {

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

bool is_unreserved(unsigned char ch, const std::string& safe) {
  return std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~' ||
         safe.find(static_cast<char>(ch)) != std::string::npos;
}

std::string quote_impl(const std::string& text, const std::string& safe, bool plus_spaces) {
  std::string out;
  char hex[4] = {};
  for (unsigned char ch : text) {
    if (plus_spaces && ch == ' ') {
      out.push_back('+');
    } else if (is_unreserved(ch, safe)) {
      out.push_back(static_cast<char>(ch));
    } else {
      std::snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned int>(ch));
      out.append(hex);
    }
  }
  return out;
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

std::string unquote_plus_impl(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '+') {
      out.push_back(' ');
    } else if (text[i] == '%' && i + 2 < text.size()) {
      const int hi = hex_value(text[i + 1]);
      const int lo = hex_value(text[i + 2]);
      if (hi >= 0 && lo >= 0) {
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
      } else {
        out.push_back(text[i]);
      }
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

bool urllib_quote(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "urllib.parse.quote() expected string and optional safe";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "quote string", text, error)) {
    return false;
  }
  std::string safe = "/";
  if (argc == 2 && !get_string_arg(args[1], "quote safe", safe, error)) {
    return false;
  }
  out = Value::string(quote_impl(text, safe, false));
  return true;
}

bool urllib_quote_plus(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "urllib.parse.quote_plus() expected string and optional safe";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "quote_plus string", text, error)) {
    return false;
  }
  std::string safe;
  if (argc == 2 && !get_string_arg(args[1], "quote_plus safe", safe, error)) {
    return false;
  }
  out = Value::string(quote_impl(text, safe, true));
  return true;
}

bool urllib_unquote_plus(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "urllib.parse.unquote_plus() expected one string";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "unquote_plus string", text, error)) {
    return false;
  }
  out = Value::string(unquote_plus_impl(text));
  return true;
}

} // namespace

void register_urllib_module(Runtime& runtime) {
  NativeModuleBuilder parse_builder(runtime, "urllib.parse");
  parse_builder.function("quote", urllib_quote)
      .function("quote_plus", urllib_quote_plus)
      .function("unquote_plus", urllib_unquote_plus);
  Value parse = parse_builder.finish();
  runtime.register_module("urllib.parse", parse);

  NativeModuleBuilder builder(runtime, "urllib");
  builder.value("parse", parse);
  runtime.register_module("urllib", builder.finish());
}

} // namespace xlang3
