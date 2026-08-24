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

#include <string>

namespace xlang3 {

namespace {

bool string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

std::string trim_right(std::string value) {
  while (!value.empty()) {
    char ch = value.back();
    if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
      break;
    }
    value.pop_back();
  }
  return value;
}

bool looks_incomplete(const std::string& source) {
  std::string trimmed = trim_right(source);
  if (trimmed.empty()) {
    return false;
  }
  if (trimmed.back() == ':' || trimmed.back() == '\\') {
    return true;
  }
  int parens = 0;
  int brackets = 0;
  int braces = 0;
  bool in_single = false;
  bool in_double = false;
  bool escaped = false;
  for (char ch : source) {
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (!in_double && ch == '\'') {
      in_single = !in_single;
      continue;
    }
    if (!in_single && ch == '"') {
      in_double = !in_double;
      continue;
    }
    if (in_single || in_double) {
      continue;
    }
    if (ch == '(') {
      ++parens;
    } else if (ch == ')' && parens > 0) {
      --parens;
    } else if (ch == '[') {
      ++brackets;
    } else if (ch == ']' && brackets > 0) {
      --brackets;
    } else if (ch == '{') {
      ++braces;
    } else if (ch == '}' && braces > 0) {
      --braces;
    }
  }
  return in_single || in_double || parens > 0 || brackets > 0 || braces > 0;
}

bool code_compile_command(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "code.compile_command() expected source and optional filename/symbol";
    return false;
  }
  std::string source;
  if (!string_arg(args[0], "source", source, error)) {
    return false;
  }
  if (looks_incomplete(source)) {
    value_set_none(out);
    return true;
  }
  Value compile_args[3] = {
      args[0],
      argc >= 2 ? args[1] : Value::string("<input>"),
      argc >= 3 ? args[2] : Value::string("single"),
  };
  const Value* compile_builtin = runtime.find_builtin("compile");
  if (compile_builtin == nullptr) {
    error = "compile builtin is not registered";
    return false;
  }
  return runtime_call_callable(runtime, *compile_builtin, compile_args, 3, out, error);
}

} // namespace

void register_code_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "code");
  builder.function("compile_command", code_compile_command)
      .value("InteractiveInterpreter", Value::class_object("InteractiveInterpreter", {}))
      .value("InteractiveConsole", Value::class_object("InteractiveConsole", {}));
  runtime.register_module("code", builder.finish());
}

} // namespace xlang3
