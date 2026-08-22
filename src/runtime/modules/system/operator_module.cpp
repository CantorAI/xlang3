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
#include "xlang3/sequence.h"

namespace xlang3 {

namespace {

bool operator_index(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "'operator.index' expected int";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool operator_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "operator.getitem() expected object and key";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!sequence_get_item(args[0], args[1], out, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

} // namespace

void register_operator_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "operator");
  builder.function("index", operator_index)
      .function("getitem", operator_getitem);
  runtime.register_module("operator", builder.finish());
}

} // namespace xlang3
