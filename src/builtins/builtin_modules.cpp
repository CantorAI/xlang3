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

void copy_builtin(Runtime& runtime, Value& module, const char* name) {
  std::string error;
  if (const auto* value = runtime.find_builtin(name)) {
    module_set_attr(module, name, *value, error);
  }
}

} // namespace

void register_builtin_modules(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_builtins");
  auto builtins = builder.finish();
  copy_builtin(runtime, builtins, "print");
  copy_builtin(runtime, builtins, "len");
  copy_builtin(runtime, builtins, "range");
  copy_builtin(runtime, builtins, "BaseException");
  copy_builtin(runtime, builtins, "Exception");
  copy_builtin(runtime, builtins, "RuntimeError");
  copy_builtin(runtime, builtins, "TypeError");
  copy_builtin(runtime, builtins, "ValueError");
  copy_builtin(runtime, builtins, "ImportError");
  runtime.register_module("_builtins", std::move(builtins));
}

} // namespace xlang3
