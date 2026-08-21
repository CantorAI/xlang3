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

namespace xlang3 {

namespace {

std::string class_name_from_value(const Value& value) {
  if (auto* klass = value_as_class(value)) {
    return klass->name;
  }
  if (auto* instance = value_as_instance(value)) {
    if (auto* klass = value_as_class(instance->klass)) {
      return klass->name;
    }
  }
  return value_to_string(value);
}

std::string exception_message(const Value& exception) {
  Value message;
  std::string ignored;
  if (object_get_attr(exception, "message", message, ignored)) {
    return value_to_string(message);
  }
  return value_to_string(exception);
}

std::string format_exception_line(Runtime& runtime, const Value& type, const Value& value) {
  Value effective_type = type;
  if (effective_type.tag == ValueTag::None || effective_type.tag == ValueTag::Invalid) {
    effective_type = runtime.exception_type(value);
  }
  std::string line = class_name_from_value(effective_type);
  const std::string message = exception_message(value);
  if (!message.empty() && message != "None") {
    line += ": " + message;
  }
  line += "\n";
  return line;
}

Value format_exception_list(Runtime& runtime, const Value& type, const Value& value) {
  std::vector<Value> lines;
  lines.push_back(Value::string("Traceback (most recent call last):\n"));
  lines.push_back(Value::string(format_exception_line(runtime, type, value)));
  return Value::list(std::move(lines));
}

bool traceback_format_exception(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc == 1) {
    out = format_exception_list(runtime, Value::none(), args[0]);
    return true;
  }
  if (argc < 2 || argc > 3) {
    error = "traceback.format_exception() expected exception or type, value, traceback";
    return false;
  }
  out = format_exception_list(runtime, args[0], args[1]);
  return true;
}

bool traceback_format_exception_only(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "traceback.format_exception_only() expected type and value";
    return false;
  }
  out = Value::list({Value::string(format_exception_line(runtime, args[0], args[1]))});
  return true;
}

bool traceback_format_exc(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "traceback.format_exc() expected no arguments";
    return false;
  }
  const Value& exception = runtime.active_exception();
  if (exception.tag == ValueTag::Invalid) {
    out = Value::string("NoneType: None\n");
    return true;
  }
  Value lines = format_exception_list(runtime, runtime.exception_type(exception), exception);
  auto* list = value_as_list(lines);
  std::string text;
  if (list != nullptr) {
    for (const auto& line : list->items) {
      text += value_to_string(line);
    }
  }
  out = Value::string(std::move(text));
  return true;
}

bool traceback_print_exception(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value lines;
  if (!traceback_format_exception(runtime, args, argc, lines, error, nullptr)) {
    return false;
  }
  auto* list = value_as_list(lines);
  if (list != nullptr) {
    for (const auto& line : list->items) {
      runtime.write_output(value_to_string(line));
    }
  }
  value_set_none(out);
  return true;
}

} // namespace

void register_traceback_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "traceback");
  builder.function("format_exception", traceback_format_exception)
      .function("format_exception_only", traceback_format_exception_only)
      .function("format_exc", traceback_format_exc)
      .function("print_exception", traceback_print_exception);
  runtime.register_module("traceback", builder.finish());
}

} // namespace xlang3
