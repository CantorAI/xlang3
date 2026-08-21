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
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"

namespace xlang3 {

namespace {

void copy_builtin(Runtime& runtime, Value& module, const char* name) {
  std::string error;
  if (const auto* value = runtime.find_builtin(name)) {
    module_set_attr(module, name, *value, error);
  }
}

bool sys_exc_info(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.exc_info expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value& exception = runtime.active_exception();
  if (exception.tag == ValueTag::Invalid) {
    out = Value::tuple({Value::none(), Value::none(), Value::none()});
    return true;
  }
  Value traceback = Value::none();
  std::string ignored;
  object_get_attr(exception, "__traceback__", traceback, ignored);
  out = Value::tuple({runtime.exception_type(exception), exception, traceback});
  return true;
}

} // namespace

void register_builtin_modules(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_builtins");
  auto builtins = builder.finish();
  copy_builtin(runtime, builtins, "print");
  copy_builtin(runtime, builtins, "_identity");
  copy_builtin(runtime, builtins, "len");
  copy_builtin(runtime, builtins, "next");
  copy_builtin(runtime, builtins, "ord");
  copy_builtin(runtime, builtins, "str");
  copy_builtin(runtime, builtins, "range");
  copy_builtin(runtime, builtins, "object");
  copy_builtin(runtime, builtins, "type");
  copy_builtin(runtime, builtins, "id");
  copy_builtin(runtime, builtins, "isinstance");
  copy_builtin(runtime, builtins, "issubclass");
  copy_builtin(runtime, builtins, "bool");
  copy_builtin(runtime, builtins, "int");
  copy_builtin(runtime, builtins, "float");
  copy_builtin(runtime, builtins, "bytes");
  copy_builtin(runtime, builtins, "bytearray");
  copy_builtin(runtime, builtins, "memoryview");
  copy_builtin(runtime, builtins, "property");
  copy_builtin(runtime, builtins, "tuple");
  copy_builtin(runtime, builtins, "list");
  copy_builtin(runtime, builtins, "dict");
  copy_builtin(runtime, builtins, "set");
  copy_builtin(runtime, builtins, "BaseException");
  copy_builtin(runtime, builtins, "Exception");
  copy_builtin(runtime, builtins, "RuntimeError");
  copy_builtin(runtime, builtins, "TypeError");
  copy_builtin(runtime, builtins, "ValueError");
  copy_builtin(runtime, builtins, "SyntaxError");
  copy_builtin(runtime, builtins, "ImportError");
  copy_builtin(runtime, builtins, "locals");
  copy_builtin(runtime, builtins, "compile");
  copy_builtin(runtime, builtins, "eval");
  copy_builtin(runtime, builtins, "exec");
  runtime.register_module("_builtins", std::move(builtins));

  NativeModuleBuilder sys_builder(runtime, "sys");
  auto sys = sys_builder.finish();
  std::string error;
  Value modules_ref;
  value_borrow_assign_fast(modules_ref, runtime.module_registry_dict());
  module_set_attr(sys, "modules", modules_ref, error);
  module_set_attr(sys, "exc_info", runtime.make_native_function("sys.exc_info", sys_exc_info), error);
  runtime.register_module("sys", std::move(sys));
}

} // namespace xlang3
