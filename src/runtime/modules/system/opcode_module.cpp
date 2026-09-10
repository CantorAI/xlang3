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

#include <algorithm>
#include <initializer_list>

namespace xlang3 {

namespace {

bool opcode_in_set(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    std::initializer_list<int64_t> values) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "opcode predicate requires one integer argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_set_bool(out, std::find(values.begin(), values.end(), args[0].as.i64) != values.end());
  return true;
}

bool opcode_has_arg(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "opcode predicate requires one integer argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int64_t op = args[0].as.i64;
  value_set_bool(
      out,
      op == 128 || op == 255 || (op >= 44 && op <= 120) || op == 237 || op == 239 ||
          (op >= 241 && op <= 245) || (op >= 247 && op <= 251) || op == 253 ||
          (op >= 257 && op <= 261) || (op >= 263 && op <= 266));
  return true;
}

#define XLANG3_OPCODE_PREDICATE(name, ...) \
  bool name(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) { \
    return opcode_in_set(runtime, args, argc, out, error, {__VA_ARGS__}); \
  }

XLANG3_OPCODE_PREDICATE(opcode_has_const, 82)
XLANG3_OPCODE_PREDICATE(opcode_has_name, 61, 64, 65, 72, 73, 80, 91, 92, 93, 96, 110, 115, 116, 249)
XLANG3_OPCODE_PREDICATE(opcode_has_jump, 68, 70, 75, 76, 77, 100, 101, 102, 103, 106, 237, 248, 257, 258, 259, 260)
XLANG3_OPCODE_PREDICATE(opcode_has_free, 62, 90, 97, 111)
XLANG3_OPCODE_PREDICATE(opcode_has_local, 63, 83, 84, 85, 86, 87, 88, 89, 112, 113, 114, 261, 266)
XLANG3_OPCODE_PREDICATE(opcode_has_exc, 263, 264, 265)

#undef XLANG3_OPCODE_PREDICATE

bool opcode_zero(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::int64(0);
  return true;
}

bool opcode_empty_list(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::list({});
  return true;
}

bool opcode_get_executor(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

} // namespace

void register_opcode_module(Runtime& runtime) {
  NativeModuleBuilder private_builder(runtime, "_opcode");
  private_builder.function("stack_effect", opcode_zero)
      .function("has_arg", opcode_has_arg)
      .function("has_const", opcode_has_const)
      .function("has_name", opcode_has_name)
      .function("has_jump", opcode_has_jump)
      .function("has_free", opcode_has_free)
      .function("has_local", opcode_has_local)
      .function("has_exc", opcode_has_exc)
      .function("get_intrinsic1_descs", opcode_empty_list)
      .function("get_intrinsic2_descs", opcode_empty_list)
      .function("get_special_method_names", opcode_empty_list)
      .function("get_nb_ops", opcode_empty_list)
      .function("get_executor", opcode_get_executor);
  runtime.register_module("_opcode", private_builder.finish());
}

} // namespace xlang3
