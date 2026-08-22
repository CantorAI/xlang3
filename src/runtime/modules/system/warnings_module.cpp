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

constexpr const char* kCatchWarningsType = "warnings.catch_warnings";

bool none_entry(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool none_entry_keywords(
    Runtime&,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string&,
    void*) {
  value_set_none(out);
  return true;
}

bool catch_warnings_enter(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool catch_warnings_exit(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::boolean(false);
  return true;
}

bool catch_warnings_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "catch_warnings() expected self";
    return false;
  }
  if (!instance_set_native_data(args[0], kCatchWarningsType, reinterpret_cast<void*>(1), nullptr, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

Value make_warning_class(const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  return Value::class_object(name, std::move(attrs));
}

Value make_catch_warnings_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("warnings.catch_warnings.__init__", catch_warnings_init)});
  attrs.push_back({"__enter__", runtime.make_native_function("warnings.catch_warnings.__enter__", catch_warnings_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("warnings.catch_warnings.__exit__", catch_warnings_exit)});
  return Value::class_object("catch_warnings", std::move(attrs));
}

Value make_warnings_module(Runtime& runtime, const char* module_name) {
  NativeModuleBuilder builder(runtime, module_name);
  builder.value("warn", runtime.make_native_function("warnings.warn", none_entry, nullptr, nullptr, nullptr, false, none_entry_keywords))
      .value("warn_explicit", runtime.make_native_function("warnings.warn_explicit", none_entry, nullptr, nullptr, nullptr, false, none_entry_keywords))
      .value("simplefilter", runtime.make_native_function("warnings.simplefilter", none_entry, nullptr, nullptr, nullptr, false, none_entry_keywords))
      .value("filterwarnings", runtime.make_native_function("warnings.filterwarnings", none_entry, nullptr, nullptr, nullptr, false, none_entry_keywords))
      .value("resetwarnings", runtime.make_native_function("warnings.resetwarnings", none_entry, nullptr, nullptr, nullptr, false, none_entry_keywords))
      .value("catch_warnings", make_catch_warnings_class(runtime))
      .value("WarningMessage", make_warning_class("WarningMessage"))
      .value("filters", Value::list({}))
      .value("defaultaction", Value::string("default"))
      .value("onceregistry", Value::dict({}));
  return builder.finish();
}

} // namespace

void register_warnings_module(Runtime& runtime) {
  runtime.register_module("warnings", make_warnings_module(runtime, "warnings"));
  runtime.register_module("_warnings", make_warnings_module(runtime, "_warnings"));
}

} // namespace xlang3
