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

bool code_compile_command(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "code.compile_command() expected source and optional filename/symbol";
    return false;
  }
  value_set_none(out);
  return true;
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
