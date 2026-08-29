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
#include "xlang3/sequence.h"

#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kCatchWarningsType = "warnings.catch_warnings";

struct CatchWarningsState {
  bool record = false;
  Value records = Value::list({});
};

thread_local std::vector<CatchWarningsState*> warning_record_stack;

void catch_warnings_cleanup(void* data) {
  delete static_cast<CatchWarningsState*>(data);
}

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

bool warnings_warn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "warnings.warn() expected message";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!warning_record_stack.empty()) {
    auto* state = warning_record_stack.back();
    if (state != nullptr && state->record) {
      Value warning = Value::instance(Value::class_object("WarningMessage", {}));
      object_set_attr(warning, "message", args[0], error);
      Value category = Value::none();
      if (argc >= 2) {
        category = args[1];
      } else if (const auto* user_warning = runtime.find_builtin("UserWarning")) {
        category = *user_warning;
      }
      object_set_attr(warning, "category", category, error);
      object_set_attr(warning, "filename", Value::string("<xlang3>"), error);
      object_set_attr(warning, "lineno", Value::int64(0), error);
      object_set_attr(warning, "line", Value::none(), error);
      auto* records = value_as_list(state->records);
      if (records != nullptr) {
        records->items.push_back(std::move(warning));
      }
    }
  }
  value_set_none(out);
  return true;
}

bool warnings_warn_keywords(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void* user_data) {
  return warnings_warn(runtime, args, argc, out, error, user_data);
}

bool catch_warnings_enter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "catch_warnings.__enter__ expected self";
    return false;
  }
  auto* state = static_cast<CatchWarningsState*>(instance_get_native_data(args[0], kCatchWarningsType));
  if (state == nullptr) {
    error = "catch_warnings state is invalid";
    return false;
  }
  warning_record_stack.push_back(state);
  if (state->record) {
    value_assign_fast(out, state->records);
  } else {
    value_set_none(out);
  }
  return true;
}

bool catch_warnings_exit(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  if (!warning_record_stack.empty()) {
    warning_record_stack.pop_back();
  }
  out = Value::boolean(false);
  return true;
}

bool catch_warnings_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "catch_warnings() expected self";
    return false;
  }
  auto* state = new CatchWarningsState();
  if (argc >= 2) {
    state->record = value_truthy(args[1]);
  }
  if (!instance_set_native_data(args[0], kCatchWarningsType, state, catch_warnings_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool catch_warnings_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (!catch_warnings_init(runtime, args, argc, out, error, user_data)) {
    return false;
  }
  auto* state = static_cast<CatchWarningsState*>(instance_get_native_data(args[0], kCatchWarningsType));
  if (state == nullptr) {
    error = "catch_warnings state is invalid";
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string(kwargs[i].name) == "record" && kwargs[i].value != nullptr) {
      state->record = value_truthy(*kwargs[i].value);
    }
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
  attrs.push_back({"__init__", runtime.make_native_function(
                                   "warnings.catch_warnings.__init__",
                                   catch_warnings_init,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   false,
                                   catch_warnings_init_kw)});
  attrs.push_back({"__enter__", runtime.make_native_function("warnings.catch_warnings.__enter__", catch_warnings_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("warnings.catch_warnings.__exit__", catch_warnings_exit)});
  return Value::class_object("catch_warnings", std::move(attrs));
}

Value make_warnings_module(Runtime& runtime, const char* module_name) {
  NativeModuleBuilder builder(runtime, module_name);
  builder.value("warn", runtime.make_native_function("warnings.warn", warnings_warn, nullptr, nullptr, nullptr, false, warnings_warn_keywords))
      .value("warn_explicit", runtime.make_native_function("warnings.warn_explicit", warnings_warn, nullptr, nullptr, nullptr, false, warnings_warn_keywords))
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
  runtime.register_module("_warnings", make_warnings_module(runtime, "_warnings"));
}

} // namespace xlang3
