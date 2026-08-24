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

#include "xlang3/ir.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <vector>

namespace xlang3 {

namespace {

const CodeObject* code_from_value(const Value& value) {
  if (auto* code = value_as_code(value)) {
    return code;
  }
  if (auto* function = value_as_function(value)) {
    static thread_local CodeObject scratch;
    scratch.module = function->module;
    scratch.function_id = function->function_id;
    scratch.mode = "exec";
    return &scratch;
  }
  return nullptr;
}

const ir::Function* function_from_code(const CodeObject* code) {
  if (code == nullptr || code->module == nullptr || code->function_id >= code->module->functions.size()) {
    return nullptr;
  }
  return &code->module->functions[code->function_id];
}

bool dis_findlinestarts(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "dis.findlinestarts() expected one argument";
    return false;
  }
  const auto* fn = function_from_code(code_from_value(args[0]));
  if (fn == nullptr) {
    error = "dis.findlinestarts() expected code object or function";
    return false;
  }
  std::vector<Value> values;
  uint32_t previous = 0;
  for (size_t offset = 0; offset < fn->source_lines.size(); ++offset) {
    const uint32_t line = fn->source_lines[offset];
    if (line != 0 && line != previous) {
      values.push_back(Value::tuple({Value::int64(static_cast<int64_t>(offset)), Value::int64(static_cast<int64_t>(line))}));
      previous = line;
    }
  }
  out = Value::list(std::move(values));
  return true;
}

bool dis_bytecode(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "dis.Bytecode() expected code and optional arguments";
    return false;
  }
  const auto* fn = function_from_code(code_from_value(args[0]));
  if (fn == nullptr) {
    error = "dis.Bytecode() expected code object or function";
    return false;
  }
  std::vector<Value> values;
  values.reserve(fn->code.size());
  for (size_t offset = 0; offset < fn->code.size(); ++offset) {
    const uint32_t line = offset < fn->source_lines.size() ? fn->source_lines[offset] : 0;
    values.push_back(Value::tuple({
        Value::int64(static_cast<int64_t>(offset)),
        Value::int64(static_cast<int64_t>(fn->code[offset].op)),
        line == 0 ? Value::none() : Value::int64(static_cast<int64_t>(line)),
    }));
  }
  out = Value::list(std::move(values));
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
      .function("get_instructions", dis_bytecode)
      .function("dis", dis_dis)
      .function("stack_effect", dis_stack_effect)
      .value("opname", Value::list({}))
      .value("cmp_op", Value::list({}))
      .value("opmap", Value::dict({}))
      .value("HAVE_ARGUMENT", Value::int64(90));
  runtime.register_module("dis", builder.finish());
}

} // namespace xlang3
