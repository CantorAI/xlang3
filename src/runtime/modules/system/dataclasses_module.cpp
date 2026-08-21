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

namespace xlang3 {

namespace {

bool dataclass_apply(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "dataclass decorator expected one class";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool dataclass_entry(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void*) {
  if (argc > 1) {
    error = "dataclass() expected optional class";
    return false;
  }
  if (argc == 1) {
    value_assign_fast(out, args[0]);
    return true;
  }
  out = runtime.make_native_function("dataclasses.dataclass.<decorator>", dataclass_apply);
  return true;
}

bool dataclass_entry_positional(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return dataclass_entry(runtime, args, argc, nullptr, 0, out, error, nullptr);
}

bool field_entry(
    Runtime&,
    const Value*,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    error = "field() expected keyword arguments";
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("dataclasses")});
  Value klass = Value::class_object("Field", std::move(attrs));
  out = Value::instance(klass);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "field() keyword is invalid";
      return false;
    }
    if (!object_set_attr(out, kwargs[i].name, *kwargs[i].value, error)) {
      return false;
    }
  }
  return true;
}

bool field_entry_positional(Runtime&, const Value*, uint32_t argc, Value&, std::string& error, void*) {
  if (argc != 0) {
    error = "field() expected keyword arguments";
    return false;
  }
  error = "field() requires keyword-call path";
  return false;
}

} // namespace

void register_dataclasses_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "dataclasses");
  builder.value(
      "dataclass",
      runtime.make_native_function(
          "dataclasses.dataclass",
          dataclass_entry_positional,
          nullptr,
          nullptr,
          nullptr,
          false,
          dataclass_entry));
  builder.value(
      "field",
      runtime.make_native_function(
          "dataclasses.field",
          field_entry_positional,
          nullptr,
          nullptr,
          nullptr,
          false,
          field_entry));
  runtime.register_module("dataclasses", builder.finish());
}

} // namespace xlang3
