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

bool dis_findlinestarts(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "dis.findlinestarts() expected one argument";
    return false;
  }
  out = Value::list({});
  return true;
}

bool dis_bytecode(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "dis.Bytecode() expected code and optional arguments";
    return false;
  }
  out = Value::list({});
  return true;
}

bool dis_dis(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool dis_stack_effect(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "dis.stack_effect() expected opcode and optional oparg/jump";
    return false;
  }
  out = Value::int64(0);
  return true;
}

} // namespace

void register_dis_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "dis");
  builder.function("findlinestarts", dis_findlinestarts)
      .function("Bytecode", dis_bytecode)
      .function("dis", dis_dis)
      .function("stack_effect", dis_stack_effect)
      .value("opname", Value::list({}))
      .value("cmp_op", Value::list({}))
      .value("opmap", Value::dict({}))
      .value("HAVE_ARGUMENT", Value::int64(90));
  runtime.register_module("dis", builder.finish());
}

} // namespace xlang3
