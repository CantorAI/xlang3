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

namespace xlang3 {

namespace {

bool return_none(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool return_false(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::boolean(false);
  return true;
}

bool return_empty_list(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::list({});
  return true;
}

bool return_arg0(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "expected at least one argument";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool pydevd_to_string(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "to_string() expected one argument";
    return false;
  }
  out = Value::string(value_to_string(args[0]));
  return true;
}

bool pydevd_quote_smart(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "quote_smart() expected one argument";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool pydevd_hasattr_checked(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "hasattr_checked() expected object and name";
    return false;
  }
  auto* name = value_as_string(args[1]);
  if (name == nullptr) {
    out = Value::boolean(false);
    return true;
  }
  Value ignored_value;
  std::string ignored_error;
  out = Value::boolean(object_get_attr(args[0], string_object_to_string(*name), ignored_value, ignored_error));
  return true;
}

bool pydevd_dir_checked(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::list({});
  return true;
}

bool pydevd_isinstance_checked(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "isinstance_checked() expected object and type";
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool pydevd_import_attr_from_module(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "import_attr_from_module() expected module name and attr";
    return false;
  }
  auto* module_name = value_as_string(args[0]);
  auto* attr_name = value_as_string(args[1]);
  if (module_name == nullptr || attr_name == nullptr) {
    error = "module name and attr must be str";
    return false;
  }
  Value module;
  if (!runtime.import_module(string_object_to_string(*module_name), module, error)) {
    return false;
  }
  if (!module_get_attr(module, string_object_to_string(*attr_name), out, error)) {
    return false;
  }
  return true;
}

bool pydevd_runpy_return_globals(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::dict({});
  return true;
}

bool debugpy_return_address(Runtime&, const Value* args, uint32_t argc, Value& out, std::string&, void*) {
  if (argc > 0) {
    value_assign_fast(out, args[0]);
  } else {
    out = Value::tuple({Value::string("127.0.0.1"), Value::int64(0)});
  }
  return true;
}

bool pydevd_timer_init(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  value_set_none(out);
  return true;
}

bool pydevd_timer_context(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "Timer context method expected self";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

Value make_timer_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("pydevd_utils.Timer.__init__", pydevd_timer_init)});
  attrs.push_back({"__enter__", runtime.make_native_function("pydevd_utils.Timer.__enter__", pydevd_timer_context)});
  attrs.push_back({"__exit__", runtime.make_native_function("pydevd_utils.Timer.__exit__", return_false)});
  return Value::class_object("Timer", std::move(attrs));
}

Value make_simple_class(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("pydevd_utils.simple.__init__", return_none)});
  return Value::class_object(name, std::move(attrs));
}

} // namespace

void register_pydevd_compat_modules(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_pydevd_bundle.pydevd_utils");
  builder.function("save_main_module", return_none)
      .function("is_current_thread_main_thread", return_false)
      .function("get_main_thread", return_none)
      .function("get_non_pydevd_threads", return_empty_list)
      .function("dump_threads", return_none)
      .function("interrupt_main_thread", return_none)
      .function("notify_about_gevent_if_needed", return_false)
      .function("quote_smart", pydevd_quote_smart)
      .function("to_string", pydevd_to_string)
      .function("is_string", return_false)
      .function("hasattr_checked", pydevd_hasattr_checked)
      .function("dir_checked", pydevd_dir_checked)
      .function("isinstance_checked", pydevd_isinstance_checked)
      .function("get_clsname_for_code", return_none)
      .function("import_attr_from_module", pydevd_import_attr_from_module)
      .function("convert_dap_log_message_to_expression", pydevd_to_string)
      .value("Timer", make_timer_class(runtime))
      .value("DAPGrouper", make_simple_class(runtime, "DAPGrouper"))
      .value("ScopeRequest", make_simple_class(runtime, "ScopeRequest"));
  runtime.register_module("_pydevd_bundle.pydevd_utils", builder.finish());

  NativeModuleBuilder runpy(runtime, "_pydevd_bundle.pydevd_runpy");
  runpy.function("run_module", pydevd_runpy_return_globals)
      .function("run_path", pydevd_runpy_return_globals)
      .function("_run_module_as_main", pydevd_runpy_return_globals);
  runtime.register_module("_pydevd_bundle.pydevd_runpy", runpy.finish());

  NativeModuleBuilder console(runtime, "_pydev_bundle.pydev_console_utils");
  console.value("BaseStdIn", make_simple_class(runtime, "BaseStdIn"))
      .value("StdIn", make_simple_class(runtime, "StdIn"))
      .value("DebugConsoleStdIn", make_simple_class(runtime, "DebugConsoleStdIn"));
  runtime.register_module("_pydev_bundle.pydev_console_utils", console.finish());

  NativeModuleBuilder thread_info(runtime, "_pydevd_bundle.pydevd_additional_thread_info");
  thread_info.value("PyDBAdditionalThreadInfo", make_simple_class(runtime, "PyDBAdditionalThreadInfo"))
      .function("set_additional_thread_info", return_none)
      .function("_set_additional_thread_info_lock", return_none)
      .function("remove_additional_info", return_none)
      .function("any_thread_stepping", return_false);
  runtime.register_module("_pydevd_bundle.pydevd_additional_thread_info", thread_info.finish());

  NativeModuleBuilder breakpoints(runtime, "_pydevd_bundle.pydevd_breakpoints");
  breakpoints.value("ExceptionBreakpoint", make_simple_class(runtime, "ExceptionBreakpoint"))
      .value("LineBreakpoint", make_simple_class(runtime, "LineBreakpoint"))
      .value("FunctionBreakpoint", make_simple_class(runtime, "FunctionBreakpoint"))
      .function("get_exception_breakpoint", return_none)
      .function("stop_on_unhandled_exception", return_none);
  runtime.register_module("_pydevd_bundle.pydevd_breakpoints", breakpoints.finish());

  NativeModuleBuilder force_pydevd(runtime, "debugpy._vendored.force_pydevd");
  force_pydevd.value("__doc__", Value::string("XLang3 native debugpy vendored pydevd bootstrap shim"));
  runtime.register_module("debugpy._vendored.force_pydevd", force_pydevd.finish());

  NativeModuleBuilder plugins(runtime, "pydevd_plugins");
  plugins.value("__path__", Value::list({}));
  runtime.register_module("pydevd_plugins", plugins.finish());

  NativeModuleBuilder api(runtime, "debugpy.server.api");
  api.function("configure", return_none)
      .function("log_to", return_none)
      .function("listen", debugpy_return_address)
      .function("connect", debugpy_return_address)
      .function("wait_for_client", return_none)
      .function("is_client_connected", return_false)
      .function("breakpoint", return_none)
      .function("debug_this_thread", return_none)
      .function("trace_this_thread", return_none)
      .function("get_cli_options", return_empty_list);
  runtime.register_module("debugpy.server.api", api.finish());

  Value options = Value::instance(Value::class_object("Options", {}));
  std::string ignored;
  object_set_attr(options, "mode", Value::none(), ignored);
  object_set_attr(options, "address", Value::none(), ignored);
  object_set_attr(options, "target", Value::none(), ignored);
  object_set_attr(options, "target_kind", Value::none(), ignored);
  object_set_attr(options, "wait_for_client", Value::boolean(false), ignored);
  object_set_attr(options, "config", Value::dict({}), ignored);

  NativeModuleBuilder cli(runtime, "debugpy.server.cli");
  cli.value("options", options)
      .function("parse_args", return_none)
      .function("main", return_none)
      .function("debugpy_run_file", return_none)
      .function("debugpy_run_module", return_none)
      .function("debugpy_run_code", return_none);
  runtime.register_module("debugpy.server.cli", cli.finish());
}

} // namespace xlang3
