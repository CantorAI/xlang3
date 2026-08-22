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

bool sysconfig_get_path(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 4) {
    error = "sysconfig.get_path() expected name and optional scheme/vars/expand";
    return false;
  }
  const auto roots = runtime.import_roots();
  if (!roots.empty()) {
    out = Value::string(roots.front().string());
  } else {
    out = Value::string(".");
  }
  return true;
}

bool sysconfig_get_path_names(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.get_path_names() expected no arguments";
    return false;
  }
  out = Value::tuple({
      Value::string("stdlib"),
      Value::string("platstdlib"),
      Value::string("purelib"),
      Value::string("platlib"),
  });
  return true;
}

bool sysconfig_get_config_var(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sysconfig.get_config_var() expected one argument";
    return false;
  }
  value_set_none(out);
  return true;
}

} // namespace

void register_sysconfig_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "sysconfig");
  builder.function("get_path", sysconfig_get_path)
      .function("get_path_names", sysconfig_get_path_names)
      .function("get_config_var", sysconfig_get_config_var);
  runtime.register_module("sysconfig", builder.finish());
}

} // namespace xlang3
