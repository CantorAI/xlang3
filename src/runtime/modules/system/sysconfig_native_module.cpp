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
#include "xlang3/runtime.h"

namespace xlang3 {

namespace {

bool sysconfig_config_vars(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "_sysconfig.config_vars() takes no arguments";
    return false;
  }
  out = Value::dict({
      {Value::string("EXT_SUFFIX"), Value::string(".cp314-win_amd64.pyd")},
      {Value::string("SOABI"), Value::string("cp314-win_amd64")},
      {Value::string("Py_GIL_DISABLED"), Value::int64(0)},
      {Value::string("Py_DEBUG"), Value::int64(0)},
  });
  return true;
}

} // namespace

void register_sysconfig_native_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_sysconfig");
  builder.function("config_vars", sysconfig_config_vars);
  runtime.register_module("_sysconfig", builder.finish());
}

} // namespace xlang3
