/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
*/
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"

#include <array>
#include <string>

namespace xlang3 {
namespace {

constexpr int64_t kLcCtype = 0;
constexpr int64_t kLcNumeric = 1;
constexpr int64_t kLcTime = 2;
constexpr int64_t kLcCollate = 3;
constexpr int64_t kLcMonetary = 4;
constexpr int64_t kLcAll = 6;

std::array<std::string, 6>& locale_names() {
  static std::array<std::string, 6> names = {"C", "C", "C", "C", "C", "C"};
  return names;
}

bool valid_category(int64_t category) {
  return category == kLcAll || (category >= kLcCtype && category <= kLcMonetary);
}

bool canonical_locale_name(const std::string& requested, std::string& canonical) {
  if (requested.empty() || requested == "C" || requested == "POSIX") {
    canonical = "C";
  } else if (requested == "C.UTF-8" || requested == "C.utf8") {
    canonical = "C.UTF-8";
  } else if (requested == "English.000000000001252" || requested == "English.1252") {
    canonical = "English.1252";
  } else if (requested == "en_US.UTF-8") {
    canonical = "en_US.UTF-8";
  } else {
    return false;
  }
  return true;
}

std::string all_locale_name(const std::array<std::string, 6>& names) {
  bool identical = true;
  for (size_t i = 1; i < names.size(); ++i) identical = identical && names[i] == names[0];
  return identical ? names[0] : "LC_CTYPE=" + names[0] + ";LC_NUMERIC=" + names[1] +
      ";LC_TIME=" + names[2] + ";LC_COLLATE=" + names[3] + ";LC_MONETARY=" + names[4];
}

bool locale_setlocale(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                      std::string& error, void*) {
  if (argc < 1 || argc > 2 || args[0].tag != ValueTag::Int64) {
    error = "setlocale() argument 1 must be int";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int64_t category = args[0].as.i64;
  if (!valid_category(category)) {
    error = "invalid locale category";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  auto& names = locale_names();
  if (argc == 1 || args[1].tag == ValueTag::None) {
    if (category != kLcAll) {
      out = Value::string(names[static_cast<size_t>(category)]);
      return true;
    }
    out = Value::string(all_locale_name(names));
    return true;
  }
  auto* locale_value = value_as_string(args[1]);
  if (locale_value == nullptr) {
    error = "setlocale() argument 2 must be str or None";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string requested = string_object_to_string(*locale_value);
  std::string canonical;
  if (category == kLcAll && requested.rfind("LC_CTYPE=", 0) == 0) {
    auto updated = names;
    size_t start = 0;
    while (start < requested.size()) {
      const size_t end = requested.find(';', start);
      const std::string part = requested.substr(start, end == std::string::npos ? std::string::npos : end - start);
      const size_t equals = part.find('=');
      if (equals == std::string::npos) {
        canonical.clear();
        break;
      }
      const std::string key = part.substr(0, equals);
      std::string value;
      if (!canonical_locale_name(part.substr(equals + 1), value)) {
        canonical.clear();
        break;
      }
      size_t index = 99;
      if (key == "LC_CTYPE") index = 0;
      else if (key == "LC_NUMERIC") index = 1;
      else if (key == "LC_TIME") index = 2;
      else if (key == "LC_COLLATE") index = 3;
      else if (key == "LC_MONETARY") index = 4;
      if (index == 99) {
        canonical.clear();
        break;
      }
      updated[index] = std::move(value);
      canonical = "composite";
      if (end == std::string::npos) break;
      start = end + 1;
    }
    if (canonical == "composite") {
      names = std::move(updated);
      out = Value::string(all_locale_name(names));
      return true;
    }
  } else if (canonical_locale_name(requested, canonical)) {
    // handled below
  }
  if (canonical.empty()) {
    error = "unsupported locale setting";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (category == kLcAll) {
    names.fill(canonical);
  } else {
    names[static_cast<size_t>(category)] = canonical;
  }
  out = Value::string(canonical);
  return true;
}

bool locale_localeconv(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "localeconv() takes no arguments";
    return false;
  }
  out = Value::dict({
      {Value::string("decimal_point"), Value::string(".")},
      {Value::string("thousands_sep"), Value::string("")},
      {Value::string("grouping"), Value::list({Value::int64(127)})},
      {Value::string("int_curr_symbol"), Value::string("")},
      {Value::string("currency_symbol"), Value::string("")},
      {Value::string("mon_decimal_point"), Value::string("")},
      {Value::string("mon_thousands_sep"), Value::string("")},
      {Value::string("mon_grouping"), Value::list({})},
      {Value::string("positive_sign"), Value::string("")},
      {Value::string("negative_sign"), Value::string("")},
      {Value::string("int_frac_digits"), Value::int64(127)},
      {Value::string("frac_digits"), Value::int64(127)},
      {Value::string("p_cs_precedes"), Value::int64(127)},
      {Value::string("p_sep_by_space"), Value::int64(127)},
      {Value::string("n_cs_precedes"), Value::int64(127)},
      {Value::string("n_sep_by_space"), Value::int64(127)},
      {Value::string("p_sign_posn"), Value::int64(127)},
      {Value::string("n_sign_posn"), Value::int64(127)},
  });
  return true;
}

bool locale_strcoll(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                    std::string& error, void*) {
  if (argc != 2 || value_as_string(args[0]) == nullptr || value_as_string(args[1]) == nullptr) {
    error = "strcoll() arguments must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string left = string_object_to_string(*value_as_string(args[0]));
  const std::string right = string_object_to_string(*value_as_string(args[1]));
  if (left.find('\0') != std::string::npos || right.find('\0') != std::string::npos) {
    error = "embedded null character";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::int64(left < right ? -1 : (left > right ? 1 : 0));
  return true;
}

bool locale_strxfrm(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                    std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "strxfrm() argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string text = string_object_to_string(*value_as_string(args[0]));
  if (text.find('\0') != std::string::npos) {
    error = "embedded null character";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::string(text);
  return true;
}

bool locale_getencoding(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "getencoding() takes no arguments";
    return false;
  }
  out = Value::string("UTF-8");
  return true;
}

}  // namespace

void register_locale_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_locale");
  builder.function("setlocale", locale_setlocale)
      .function("localeconv", locale_localeconv)
      .function("strcoll", locale_strcoll)
      .function("strxfrm", locale_strxfrm)
      .function("getencoding", locale_getencoding)
      .value("CHAR_MAX", Value::int64(127))
      .value("LC_CTYPE", Value::int64(kLcCtype))
      .value("LC_NUMERIC", Value::int64(kLcNumeric))
      .value("LC_TIME", Value::int64(kLcTime))
      .value("LC_COLLATE", Value::int64(kLcCollate))
      .value("LC_MONETARY", Value::int64(kLcMonetary))
      .value("LC_ALL", Value::int64(kLcAll));
  if (const Value* value_error = runtime.find_builtin("ValueError")) {
    builder.value("Error", *value_error);
  }
  runtime.register_module("_locale", builder.finish());
}

}  // namespace xlang3
