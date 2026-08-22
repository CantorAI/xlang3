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

bool signal_signal(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "signal.signal() expected signal number and handler";
    return false;
  }
  value_set_none(out);
  return true;
}

bool getsignal(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "signal.getsignal() expected signal number";
    return false;
  }
  value_set_none(out);
  return true;
}

void fill_signal_module(NativeModuleBuilder& builder) {
  builder.function("signal", signal_signal)
      .function("getsignal", getsignal)
      .value("SIG_DFL", Value::int64(0))
      .value("SIG_IGN", Value::int64(1))
      .value("SIGINT", Value::int64(2))
      .value("SIGILL", Value::int64(4))
      .value("SIGABRT", Value::int64(22))
      .value("SIGFPE", Value::int64(8))
      .value("SIGSEGV", Value::int64(11))
      .value("SIGTERM", Value::int64(15))
      .value("CTRL_C_EVENT", Value::int64(0))
      .value("CTRL_BREAK_EVENT", Value::int64(1));
}

} // namespace

void register_signal_module(Runtime& runtime) {
  NativeModuleBuilder public_module(runtime, "signal");
  fill_signal_module(public_module);
  runtime.register_module("signal", public_module.finish());

  NativeModuleBuilder private_module(runtime, "_signal");
  fill_signal_module(private_module);
  runtime.register_module("_signal", private_module.finish());
}

} // namespace xlang3
