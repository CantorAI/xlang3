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

bool ctypes_create_unicode_buffer(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ctypes.create_unicode_buffer() expected init or size";
    return false;
  }
  out = Value::string("");
  return true;
}

} // namespace

void register_ctypes_module(Runtime& runtime) {
  NativeModuleBuilder wintypes_builder(runtime, "ctypes.wintypes");
  wintypes_builder.value("MAX_PATH", Value::int64(260))
      .value("LPCWSTR", Value::int64(0))
      .value("LPWSTR", Value::int64(0))
      .value("DWORD", Value::int64(0));
  Value wintypes = wintypes_builder.finish();
  runtime.register_module("ctypes.wintypes", wintypes);

  NativeModuleBuilder builder(runtime, "ctypes");
  builder.function("create_unicode_buffer", ctypes_create_unicode_buffer)
      .value("wintypes", wintypes);
  runtime.register_module("ctypes", builder.finish());
}

} // namespace xlang3
