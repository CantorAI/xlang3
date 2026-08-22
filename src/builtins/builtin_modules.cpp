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

#include <cstdint>
#include <limits>

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

bool sys_getframe(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "sys._getframe expected at most 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("sys")});
  Value frame_type = Value::class_object("frame", std::move(attrs));
  out = Value::instance(frame_type);
  object_set_attr(out, "f_lineno", Value::int64(0), error);
  object_set_attr(out, "f_code", Value::none(), error);
  object_set_attr(out, "f_globals", runtime.current_globals_module(), error);
  object_set_attr(out, "f_locals", runtime.current_globals_module(), error);
  object_set_attr(out, "f_back", Value::none(), error);
  return true;
}

bool sys_current_frames(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._current_frames expected 0 arguments";
    return false;
  }
  out = Value::dict({});
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

bool sys_xlang3_debug_step_over(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2 || args[0].tag != ValueTag::Int64 || args[0].as.i64 <= 0 ||
      args[1].tag != ValueTag::Int64 || args[1].as.i64 <= 0) {
    error = "sys._xlang3_debug_step_over expected frame_count and line";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_step_over(static_cast<size_t>(args[0].as.i64), static_cast<uint32_t>(args[1].as.i64));
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_step_out(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64 || args[0].as.i64 <= 0) {
    error = "sys._xlang3_debug_step_out expected frame_count";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_step_out(static_cast<size_t>(args[0].as.i64));
  value_set_none(out);
  return true;
}

bool sys_xlang3_debug_request_pause(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sys._xlang3_debug_request_pause expected 0 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  runtime.debug_request_pause();
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
  copy_builtin(runtime, builtins, "classmethod");
  copy_builtin(runtime, builtins, "staticmethod");
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
  copy_builtin(runtime, builtins, "AttributeError");
  copy_builtin(runtime, builtins, "NameError");
  copy_builtin(runtime, builtins, "IndexError");
  copy_builtin(runtime, builtins, "KeyError");
  copy_builtin(runtime, builtins, "ZeroDivisionError");
  copy_builtin(runtime, builtins, "StopIteration");
  copy_builtin(runtime, builtins, "OSError");
  copy_builtin(runtime, builtins, "FileNotFoundError");
  copy_builtin(runtime, builtins, "PermissionError");
  copy_builtin(runtime, builtins, "IsADirectoryError");
  copy_builtin(runtime, builtins, "NotADirectoryError");
  copy_builtin(runtime, builtins, "BlockingIOError");
  copy_builtin(runtime, builtins, "ConnectionError");
  copy_builtin(runtime, builtins, "BrokenPipeError");
  copy_builtin(runtime, builtins, "ConnectionAbortedError");
  copy_builtin(runtime, builtins, "ConnectionRefusedError");
  copy_builtin(runtime, builtins, "ConnectionResetError");
  copy_builtin(runtime, builtins, "TimeoutError");
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
  module_set_attr(sys, "version_info", Value::tuple({Value::int64(3), Value::int64(14), Value::int64(0), Value::string("final"), Value::int64(0)}), error);
  module_set_attr(sys, "version", Value::string("3.14.0 (XLang3)"), error);
  module_set_attr(sys, "platform", Value::string(
#if defined(_WIN32)
                                      "win32"
#elif defined(__APPLE__)
                                      "darwin"
#elif defined(__EMSCRIPTEN__)
                                      "emscripten"
#else
                                      "linux"
#endif
                                      ),
      error);
  module_set_attr(sys, "maxsize", Value::int64(std::numeric_limits<int64_t>::max()), error);
  module_set_attr(sys, "byteorder", Value::string("little"), error);
  module_set_attr(sys, "dont_write_bytecode", Value::boolean(true), error);
  module_set_attr(sys, "flags", Value::tuple({}), error);
  module_set_attr(sys, "builtin_module_names", Value::tuple({}), error);
  module_set_attr(sys, "implementation", Value::instance(Value::class_object("SimpleNamespace", {})), error);
  Value implementation;
  module_get_attr(sys, "implementation", implementation, error);
  object_set_attr(implementation, "name", Value::string("xlang3"), error);
  object_set_attr(implementation, "version", Value::tuple({Value::int64(3), Value::int64(14), Value::int64(0), Value::string("final"), Value::int64(0)}), error);
  module_set_attr(sys, "executable", Value::string(""), error);
  module_set_attr(sys, "prefix", Value::string(""), error);
  module_set_attr(sys, "base_prefix", Value::string(""), error);
  module_set_attr(sys, "exc_info", runtime.make_native_function("sys.exc_info", sys_exc_info), error);
  module_set_attr(sys, "settrace", runtime.make_native_function("sys.settrace", sys_settrace), error);
  module_set_attr(sys, "gettrace", runtime.make_native_function("sys.gettrace", sys_gettrace), error);
  module_set_attr(sys, "_getframe", runtime.make_native_function("sys._getframe", sys_getframe), error);
  module_set_attr(sys, "_current_frames", runtime.make_native_function("sys._current_frames", sys_current_frames), error);
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
      "_xlang3_debug_step_over",
      runtime.make_native_function("sys._xlang3_debug_step_over", sys_xlang3_debug_step_over),
      error);
  module_set_attr(
      sys,
      "_xlang3_debug_step_out",
      runtime.make_native_function("sys._xlang3_debug_step_out", sys_xlang3_debug_step_out),
      error);
  module_set_attr(
      sys,
      "_xlang3_debug_request_pause",
      runtime.make_native_function("sys._xlang3_debug_request_pause", sys_xlang3_debug_request_pause),
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
