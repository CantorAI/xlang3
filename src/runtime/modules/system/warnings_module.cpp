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
#include "xlang3/module_object.h"

namespace xlang3 {

namespace {

bool none_entry(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

const Value* find_builtin_or_error(Runtime& runtime, const char* name, std::string& error) {
  const Value* value = runtime.find_builtin(name);
  if (value == nullptr) {
    error = std::string("missing builtin '") + name + "'";
    runtime.raise_class_error("RuntimeError", error);
  }
  return value;
}

bool warnings_warn_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    const char* fallback_name) {
  Value fallback_module;
  if (!runtime.import_module("_py_warnings", fallback_module, error)) {
    return false;
  }
  Value fallback;
  if (!module_get_attr(fallback_module, fallback_name, fallback, error)) {
    return false;
  }
  std::vector<std::pair<std::string, Value>> keyword_values;
  keyword_values.reserve(kwargc);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = std::string(fallback_name) + "() received invalid keyword argument";
      return false;
    }
    keyword_values.emplace_back(kwargs[i].name, *kwargs[i].value);
  }
  return runtime_call_callable_kw(runtime, fallback, args, argc, keyword_values, out, error);
}

bool warnings_warn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return warnings_warn_impl(runtime, args, argc, nullptr, 0, out, error,
                            user_data == nullptr ? "warn" : "warn_explicit");
}

bool warnings_warn_keywords(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  return warnings_warn_impl(runtime, args, argc, kwargs, kwargc, out, error,
                            user_data == nullptr ? "warn" : "warn_explicit");
}

Value make_warnings_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_warnings");
  builder.value("warn", runtime.make_native_function("_warnings.warn", warnings_warn, nullptr, nullptr, nullptr, false, warnings_warn_keywords))
      .value("warn_explicit", runtime.make_native_function("_warnings.warn_explicit", warnings_warn, reinterpret_cast<void*>(1), nullptr, nullptr, false, warnings_warn_keywords))
      .function("_acquire_lock", none_entry)
      .function("_release_lock", none_entry)
      .function("_filters_mutated_lock_held", none_entry)
      .value("filters", Value::list({}))
      .value("_defaultaction", Value::string("default"))
      .value("_onceregistry", Value::dict({}))
      .value("_warnings_context", Value::none());
  return builder.finish();
}

} // namespace

void register_warnings_module(Runtime& runtime) {
  runtime.register_module("_warnings", make_warnings_module(runtime));
}

} // namespace xlang3
