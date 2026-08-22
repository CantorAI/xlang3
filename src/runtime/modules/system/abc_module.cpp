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

bool abc_return_none(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool abc_get_cache_token(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "_abc.get_cache_token() expected no arguments";
    return false;
  }
  out = Value::int64(0);
  return true;
}

bool abc_register(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "_abc._abc_register() expected class and subclass";
    return false;
  }
  value_assign_fast(out, args[1]);
  return true;
}

bool abc_bool_result(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_bool(out, false);
  return true;
}

bool abc_get_dump(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::tuple({Value::set({}), Value::set({}), Value::set({}), Value::int64(0)});
  return true;
}

} // namespace

void register_abc_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_abc");
  builder.function("get_cache_token", abc_get_cache_token)
      .function("_abc_init", abc_return_none)
      .function("_abc_register", abc_register)
      .function("_abc_instancecheck", abc_bool_result)
      .function("_abc_subclasscheck", abc_bool_result)
      .function("_get_dump", abc_get_dump)
      .function("_reset_registry", abc_return_none)
      .function("_reset_caches", abc_return_none);
  runtime.register_module("_abc", builder.finish());
}

} // namespace xlang3
