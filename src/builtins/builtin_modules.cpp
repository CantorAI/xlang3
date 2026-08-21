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

bool sys_settrace(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys.settrace expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.set_trace_function(args[0]);
  value_set_none(out);
  return true;
}

bool sys_gettrace(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys.gettrace expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (runtime.trace_function().tag == ValueTag::Invalid) {
    value_set_none(out);
  } else {
    value_assign_fast(out, runtime.trace_function());
  }
  return true;
}

bool sys_xlang3_debug_set_hook(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sys._xlang3_debug_set_hook expected 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[0].tag != ValueTag::None && value_as_function(args[0]) == nullptr) {
    error = "sys._xlang3_debug_set_hook expected function or None";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.set_debug_hook(args[0]);
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_add_breakpoint(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "sys._xlang3_debug_add_breakpoint expected 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* file = value_as_string(args[0]);
  if (file == nullptr || args[1].tag != ValueTag::Int64 || args[1].as.i64 <= 0) {
    error = "sys._xlang3_debug_add_breakpoint expected file and positive line";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_add_breakpoint(string_object_to_string(*file), static_cast<uint32_t>(args[1].as.i64));
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_clear_breakpoints(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_clear_breakpoints expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_clear_breakpoints();
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_step_into(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_step_into expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_step_into();
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_continue(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_continue expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_continue();
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_poll_needed(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_poll_needed expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::boolean(runtime.debug_poll_needed());
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
  copy_builtin(runtime, builtins, "open");
  runtime.register_module("_builtins", std::move(builtins));

  NativeModuleBuilder sys_builder(runtime, "sys");
  auto sys = sys_builder.finish();
  std::string error;
  Value modules_ref;
  value_borrow_assign_fast(modules_ref, runtime.module_registry_dict());
  module_set_attr(sys, "modules", modules_ref, error);
  module_set_attr(sys, "argv", Value::list({}), error);
  module_set_attr(sys, "exc_info", runtime.make_native_function("sys.exc_info", sys_exc_info), error);
  module_set_attr(sys, "settrace", runtime.make_native_function("sys.settrace", sys_settrace), error);
  module_set_attr(sys, "gettrace", runtime.make_native_function("sys.gettrace", sys_gettrace), error);
  module_set_attr(
      sys,
      "_xlang3_debug_set_hook",
      runtime.make_native_function("sys._xlang3_debug_set_hook", sys_xlang3_debug_set_hook),
      error);
  module_set_attr(
      sys,
      "_xlang3_debug_add_breakpoint",
      runtime.make_native_function("sys._xlang3_debug_add_breakpoint", sys_xlang3_debug_add_breakpoint),
      error);
  module_set_attr(
      sys,
      "_xlang3_debug_clear_breakpoints",
      runtime.make_native_function("sys._xlang3_debug_clear_breakpoints", sys_xlang3_debug_clear_breakpoints),
      error);
  module_set_attr(
      sys,
      "_xlang3_debug_step_into",
      runtime.make_native_function("sys._xlang3_debug_step_into", sys_xlang3_debug_step_into),
      error);
  module_set_attr(
      sys,
      "_xlang3_debug_continue",
      runtime.make_native_function("sys._xlang3_debug_continue", sys_xlang3_debug_continue),
      error);
  module_set_attr(
      sys,
      "_xlang3_debug_poll_needed",
      runtime.make_native_function("sys._xlang3_debug_poll_needed", sys_xlang3_debug_poll_needed),
      error);
  runtime.register_module("sys", std::move(sys));
}

} // namespace xlang3
