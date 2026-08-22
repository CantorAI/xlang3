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

bool marshal_not_implemented(Runtime&, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "marshal data decoding is not implemented yet";
  return false;
}

} // namespace

void register_marshal_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "marshal");
  builder.value("loads", runtime.make_native_function("marshal.loads", marshal_not_implemented))
      .value("dumps", runtime.make_native_function("marshal.dumps", marshal_not_implemented))
      .value("load", runtime.make_native_function("marshal.load", marshal_not_implemented))
      .value("dump", runtime.make_native_function("marshal.dump", marshal_not_implemented))
      .value("version", Value::int64(5));
  runtime.register_module("marshal", builder.finish());
}

} // namespace xlang3
