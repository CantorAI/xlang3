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

namespace xlang3 {

namespace {

bool builtin_print(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error) {
  (void)error;
  for (uint32_t i = 0; i < argc; ++i) {
    if (i != 0) {
      runtime.out() << " ";
    }
    runtime.out() << value_to_string(args[i]);
  }
  runtime.out() << "\n";
  value_set_none(out);
  return true;
}

} // namespace

void register_io_builtins(Runtime& runtime) {
  runtime.register_native_builtin("print", builtin_print);
}

} // namespace xlang3
