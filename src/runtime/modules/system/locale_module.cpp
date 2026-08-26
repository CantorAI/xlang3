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

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

namespace {

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  auto* text = value_as_string(value);
  if (text == nullptr) {
    error = std::string(name) + " must be str";
    return false;
  }
  out = string_object_to_string(*text);
  return true;
}

bool locale_setlocale(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2 || args[0].tag != ValueTag::Int64) {
    error = "locale.setlocale() expected category and optional locale";
    return false;
  }
  const char* locale_name = nullptr;
  std::string locale_storage;
  if (argc == 2) {
    if (args[1].tag == ValueTag::None) {
      locale_name = nullptr;
    } else if (auto* text = value_as_string(args[1])) {
      locale_storage = string_object_to_string(*text);
      locale_name = locale_storage.c_str();
    } else {
      error = "locale name must be str or None";
      return false;
    }
  }
  const char* result = std::setlocale(static_cast<int>(args[0].as.i64), locale_name);
  out = Value::string(result == nullptr ? "C" : result);
  return true;
}

bool locale_getpreferredencoding(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "locale.getpreferredencoding() expected optional do_setlocale";
    return false;
  }
  out = Value::string("utf-8");
  return true;
}

bool locale_getencoding(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "locale.getencoding() expected no arguments";
    return false;
  }
  out = Value::string("utf-8");
  return true;
}

bool locale_getlocale(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "locale.getlocale() expected optional category";
    return false;
  }
  int category = LC_CTYPE;
  if (argc == 1) {
    if (args[0].tag != ValueTag::Int64) {
      error = "locale category must be int";
      return false;
    }
    category = static_cast<int>(args[0].as.i64);
  }
  const char* current = std::setlocale(category, nullptr);
  if (current == nullptr || current[0] == 'C') {
    out = Value::tuple({Value::none(), Value::none()});
    return true;
  }
  out = Value::tuple({Value::string(current), Value::string("UTF-8")});
  return true;
}

bool locale_getdefaultlocale(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return locale_getlocale(runtime, args, argc, out, error, nullptr);
}

bool locale_normalize(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "locale.normalize() expected one locale name";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "locale name", text, error)) {
    return false;
  }
  out = Value::string(std::move(text));
  return true;
}

bool locale_localeconv(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "locale.localeconv() expected no arguments";
    return false;
  }
  std::vector<std::pair<Value, Value>> entries;
  entries.push_back({Value::string("decimal_point"), Value::string(".")});
  entries.push_back({Value::string("thousands_sep"), Value::string("")});
  entries.push_back({Value::string("currency_symbol"), Value::string("")});
  entries.push_back({Value::string("int_curr_symbol"), Value::string("")});
  entries.push_back({Value::string("frac_digits"), Value::int64(127)});
  out = Value::dict(std::move(entries));
  return true;
}

bool locale_strcoll(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "locale.strcoll() expected two strings";
    return false;
  }
  std::string left;
  std::string right;
  if (!get_string_arg(args[0], "string", left, error) || !get_string_arg(args[1], "string", right, error)) {
    return false;
  }
  const int cmp = std::strcoll(left.c_str(), right.c_str());
  value_set_int64(out, cmp < 0 ? -1 : cmp > 0 ? 1 : 0);
  return true;
}

bool locale_strxfrm(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "locale.strxfrm() expected one string";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "string", text, error)) {
    return false;
  }
  std::vector<char> buffer(text.size() * 2 + 16);
  const size_t needed = std::strxfrm(buffer.data(), text.c_str(), buffer.size());
  if (needed >= buffer.size()) {
    buffer.resize(needed + 1);
    std::strxfrm(buffer.data(), text.c_str(), buffer.size());
  }
  out = Value::string(std::string(buffer.data()));
  return true;
}

std::string delocalize_text(std::string text) {
  text.erase(std::remove(text.begin(), text.end(), ','), text.end());
  return text;
}

bool locale_delocalize(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "locale.delocalize() expected one string";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "string", text, error)) {
    return false;
  }
  out = Value::string(delocalize_text(std::move(text)));
  return true;
}

bool locale_localize(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "locale.localize() expected string and optional grouping";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "string", text, error)) {
    return false;
  }
  out = Value::string(std::move(text));
  return true;
}

bool locale_atof(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "locale.atof() expected string and optional function";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "string", text, error)) {
    return false;
  }
  std::string normalized_text = delocalize_text(std::move(text));
  Value normalized = Value::string(normalized_text);
  if (argc == 2) {
    return runtime_call_callable(runtime, args[1], &normalized, 1, out, error);
  }
  char* end = nullptr;
  const double parsed = std::strtod(normalized_text.c_str(), &end);
  if (end == nullptr || *end != '\0') {
    error = "could not convert string to float";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::number(parsed);
  return true;
}

bool locale_atoi(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "locale.atoi() expected one string";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[0], "string", text, error)) {
    return false;
  }
  const std::string normalized = delocalize_text(std::move(text));
  char* end = nullptr;
  const long long parsed = std::strtoll(normalized.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    error = "invalid literal for int()";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(parsed));
  (void)user_data;
  return true;
}

} // namespace

void register_locale_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "locale");
  builder.value("LC_ALL", Value::int64(LC_ALL))
      .value("LC_CTYPE", Value::int64(LC_CTYPE))
      .value("LC_NUMERIC", Value::int64(LC_NUMERIC))
      .value("LC_TIME", Value::int64(LC_TIME))
      .value("LC_COLLATE", Value::int64(LC_COLLATE))
      .value("LC_MONETARY", Value::int64(LC_MONETARY))
      .function("setlocale", locale_setlocale)
      .function("getpreferredencoding", locale_getpreferredencoding)
      .function("getencoding", locale_getencoding)
      .function("getlocale", locale_getlocale)
      .function("getdefaultlocale", locale_getdefaultlocale)
      .function("normalize", locale_normalize)
      .function("localeconv", locale_localeconv)
      .function("strcoll", locale_strcoll)
      .function("strxfrm", locale_strxfrm)
      .function("delocalize", locale_delocalize)
      .function("localize", locale_localize)
      .function("atof", locale_atof)
      .function("atoi", locale_atoi)
      .value("CHAR_MAX", Value::int64(127));
  runtime.register_module("locale", builder.finish());
}

} // namespace xlang3
