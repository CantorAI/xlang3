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

bool winreg_missing_key(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "registry key not found";
  runtime.raise_class_error("OSError", error);
  return false;
}

} // namespace

void register_winreg_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "winreg");
  builder.value("HKEY_CURRENT_USER", Value::int64(0x80000001ll))
      .value("HKEY_LOCAL_MACHINE", Value::int64(0x80000002ll))
      .value("KEY_READ", Value::int64(0x20019))
      .value("OpenKey", runtime.make_native_function("winreg.OpenKey", winreg_missing_key))
      .value("QueryValue", runtime.make_native_function("winreg.QueryValue", winreg_missing_key))
      .value("CloseKey", runtime.make_native_function("winreg.CloseKey", winreg_missing_key));
  runtime.register_module("winreg", builder.finish());
}

} // namespace xlang3
