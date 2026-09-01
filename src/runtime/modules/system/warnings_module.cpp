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
    std::string& error) {
  if (argc < 1) {
    error = "warnings.warn() expected message";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  Value category;
  if (argc >= 2 && args[1].tag != ValueTag::None) {
    value_assign_fast(category, args[1]);
  } else if (const Value* user_warning = find_builtin_or_error(runtime, "UserWarning", error)) {
    value_assign_fast(category, *user_warning);
  } else {
    return false;
  }

  Value source = Value::none();
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string key(kwargs[i].name);
    if (kwargs[i].value == nullptr) {
      continue;
    }
    if (key == "category" && kwargs[i].value->tag != ValueTag::None) {
      value_assign_fast(category, *kwargs[i].value);
    } else if (key == "source") {
      value_assign_fast(source, *kwargs[i].value);
    }
  }

  Value warnings_module;
  if (!runtime.import_module("warnings", warnings_module, error)) {
    return false;
  }

  Value warning_message_class;
  if (!module_get_attr(warnings_module, "WarningMessage", warning_message_class, error)) {
    return false;
  }

  Value showwarnmsg;
  if (!module_get_attr(warnings_module, "_showwarnmsg", showwarnmsg, error)) {
    return false;
  }

  Value filename = Value::string("<string>");
  Value lineno = Value::int64(1);
  Value file = Value::none();
  Value line = Value::none();
  Value message_args[] = {args[0], category, filename, lineno, file, line, source};
  Value warning_message;
  if (!runtime_call_callable(runtime, warning_message_class, message_args, 7, warning_message, error)) {
    return false;
  }

  if (!runtime_call_callable(runtime, showwarnmsg, &warning_message, 1, out, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool warnings_warn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return warnings_warn_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool warnings_warn_keywords(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return warnings_warn_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

Value make_warnings_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_warnings");
  builder.value("warn", runtime.make_native_function("_warnings.warn", warnings_warn, nullptr, nullptr, nullptr, false, warnings_warn_keywords))
      .value("warn_explicit", runtime.make_native_function("_warnings.warn_explicit", warnings_warn, nullptr, nullptr, nullptr, false, warnings_warn_keywords))
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
