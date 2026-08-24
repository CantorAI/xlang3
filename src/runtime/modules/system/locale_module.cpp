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

#include <clocale>
#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

namespace {

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
  auto* text = value_as_string(args[0]);
  if (text == nullptr) {
    error = "locale name must be str";
    return false;
  }
  out = Value::string(string_object_to_string(*text));
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
      .function("localeconv", locale_localeconv);
  runtime.register_module("locale", builder.finish());
}

} // namespace xlang3
