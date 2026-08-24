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

namespace xlang3 {

namespace {

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
  out = Value::list({Value::tuple({
      Value::string(string_object_to_string(*text)),
      Value::none(),
      Value::none(),
      Value::none(),
  })});
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
  out = Value::tuple({Value::string(string_object_to_string(*text)), Value::list({})});
  return true;
}

} // namespace

void register_string_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_string");
  builder.function("formatter_parser", formatter_parser)
      .function("formatter_field_name_split", formatter_field_name_split);
  runtime.register_module("_string", builder.finish());

  NativeModuleBuilder public_builder(runtime, "string");
  Value formatter_type = Value::class_object("Formatter", {});
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
