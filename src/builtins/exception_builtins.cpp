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

#include "xlang3/object_model.h"

#include <string_view>

namespace xlang3 {

namespace {

bool exception_is_os_error_family(const Value& self) {
  auto* instance = value_as_instance(self);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  return klass != nullptr && (klass->name == "OSError" || class_has_builtin_base_name(klass, "OSError"));
}

void initialize_os_error_attrs(Value& self, const Value* args, uint32_t argc) {
  std::string ignored;
  const bool has_errno_arg = argc >= 2 && (args[1].tag == ValueTag::Int64 || args[1].tag == ValueTag::None);
  object_set_attr(self, "errno", has_errno_arg ? args[1] : Value::none(), ignored);
  object_set_attr(self, "strerror", argc >= 3 ? args[2] : Value::none(), ignored);
  object_set_attr(self, "filename", argc >= 4 ? args[3] : Value::none(), ignored);
  object_set_attr(self, "filename2", argc >= 5 ? args[4] : Value::none(), ignored);
  object_set_attr(self, "winerror", Value::none(), ignored);
}

void remap_exact_os_error(Runtime& runtime, Value& self, const Value* args, uint32_t argc) {
  auto* instance = value_as_instance(self);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (klass == nullptr || klass->name != "OSError" || argc < 2 || args[1].tag != ValueTag::Int64) {
    return;
  }

  const char* mapped_name = nullptr;
  switch (args[1].as.i64) {
    case 2:
      mapped_name = "FileNotFoundError";
      break;
    case 13:
      mapped_name = "PermissionError";
      break;
    case 17:
      mapped_name = "FileExistsError";
      break;
    default:
      break;
  }
  if (mapped_name == nullptr) {
    return;
  }
  if (const Value* mapped = runtime.find_builtin(mapped_name)) {
    instance->klass = *mapped;
  }
}

bool exception_init(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "Exception.__init__() self is missing";
    return false;
  }
  std::vector<Value> exception_args;
  exception_args.reserve(argc - 1);
  const bool is_os_error = exception_is_os_error_family(args[0]);
  const uint32_t stored_argc = is_os_error && argc >= 4 ? 3 : argc;
  for (uint32_t i = 1; i < stored_argc; ++i) {
    exception_args.push_back(args[i]);
  }
  Value args_tuple = Value::tuple(exception_args);
  Value message = Value::string("");
  if (argc == 2) {
    value_assign_fast(message, args[1]);
  } else if (argc > 2) {
    value_assign_fast(message, args_tuple);
  }
  std::string ignored;
  if (!object_set_attr(const_cast<Value&>(args[0]), "message", message, ignored)) {
    error = "Exception.__init__() self is invalid";
    return false;
  }
  object_set_attr(const_cast<Value&>(args[0]), "args", std::move(args_tuple), ignored);
  if (auto* instance = value_as_instance(args[0])) {
    if (auto* klass = value_as_class(instance->klass)) {
      if (klass->name == "StopIteration" || class_has_builtin_base_name(klass, "StopIteration")) {
        Value value = Value::none();
        if (argc >= 2) {
          value_assign_fast(value, args[1]);
        }
        object_set_attr(const_cast<Value&>(args[0]), "value", value, ignored);
      }
      if (klass->name == "SystemExit" || class_has_builtin_base_name(klass, "SystemExit")) {
        Value code = Value::none();
        if (argc == 2) {
          value_assign_fast(code, args[1]);
        } else if (argc > 2) {
          value_assign_fast(code, message);
        }
        object_set_attr(const_cast<Value&>(args[0]), "code", code, ignored);
      }
    }
  }
  if (is_os_error) {
    remap_exact_os_error(runtime, const_cast<Value&>(args[0]), args, argc);
    initialize_os_error_attrs(const_cast<Value&>(args[0]), args, argc);
  }
  object_set_attr(const_cast<Value&>(args[0]), "__traceback__", Value::none(), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "__cause__", Value::none(), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "__context__", Value::none(), ignored);
  object_set_attr(const_cast<Value&>(args[0]), "__suppress_context__", Value::boolean(false), ignored);
  value_set_none(out);
  return true;
}

bool exception_with_traceback(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "BaseException.with_traceback() expected traceback";
    return false;
  }
  std::string ignored;
  if (!object_set_attr(const_cast<Value&>(args[0]), "__traceback__", args[1], ignored)) {
    error = "BaseException.with_traceback() self is invalid";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

void register_exception_class(Runtime& runtime, const char* name, Value base = Value::invalid()) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.emplace_back("__module__", Value::string("builtins"));
  attrs.emplace_back("__qualname__", Value::string(name));
  attrs.emplace_back("__init__", runtime.make_native_function(std::string(name) + ".__init__", exception_init));
  if (std::string_view(name) == "BaseException") {
    attrs.emplace_back(
        "with_traceback",
        runtime.make_native_function("BaseException.with_traceback", exception_with_traceback));
  }
  runtime.register_builtin(name, Value::class_object(name, std::move(attrs), std::move(base)));
}

} // namespace

void register_exception_builtins(Runtime& runtime) {
  Value object_base = runtime.find_builtin("object") != nullptr ? *runtime.find_builtin("object") : Value::invalid();
  register_exception_class(runtime, "BaseException", std::move(object_base));
  register_exception_class(runtime, "BaseExceptionGroup", *runtime.find_builtin("BaseException"));
  register_exception_class(runtime, "GeneratorExit", *runtime.find_builtin("BaseException"));
  register_exception_class(runtime, "KeyboardInterrupt", *runtime.find_builtin("BaseException"));
  register_exception_class(runtime, "SystemExit", *runtime.find_builtin("BaseException"));

  register_exception_class(runtime, "Exception", *runtime.find_builtin("BaseException"));
  register_exception_class(runtime, "ArithmeticError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "FloatingPointError", *runtime.find_builtin("ArithmeticError"));
  register_exception_class(runtime, "OverflowError", *runtime.find_builtin("ArithmeticError"));
  register_exception_class(runtime, "ZeroDivisionError", *runtime.find_builtin("ArithmeticError"));
  register_exception_class(runtime, "AssertionError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "AttributeError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "BufferError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "EOFError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "ExceptionGroup", *runtime.find_builtin("BaseExceptionGroup"));
  {
    std::string ignored;
    Value exception_group = *runtime.find_builtin("ExceptionGroup");
    class_set_base(exception_group, *runtime.find_builtin("Exception"), ignored);
  }
  register_exception_class(runtime, "ImportError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "ModuleNotFoundError", *runtime.find_builtin("ImportError"));
  register_exception_class(runtime, "LookupError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "IndexError", *runtime.find_builtin("LookupError"));
  register_exception_class(runtime, "KeyError", *runtime.find_builtin("LookupError"));
  register_exception_class(runtime, "MemoryError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "NameError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "UnboundLocalError", *runtime.find_builtin("NameError"));

  register_exception_class(runtime, "OSError", *runtime.find_builtin("Exception"));
  runtime.register_builtin("EnvironmentError", *runtime.find_builtin("OSError"));
  runtime.register_builtin("IOError", *runtime.find_builtin("OSError"));
  runtime.register_builtin("WindowsError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "BlockingIOError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "ChildProcessError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "ConnectionError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "BrokenPipeError", *runtime.find_builtin("ConnectionError"));
  register_exception_class(runtime, "ConnectionAbortedError", *runtime.find_builtin("ConnectionError"));
  register_exception_class(runtime, "ConnectionRefusedError", *runtime.find_builtin("ConnectionError"));
  register_exception_class(runtime, "ConnectionResetError", *runtime.find_builtin("ConnectionError"));
  register_exception_class(runtime, "FileExistsError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "FileNotFoundError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "InterruptedError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "IsADirectoryError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "NotADirectoryError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "PermissionError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "ProcessLookupError", *runtime.find_builtin("OSError"));
  register_exception_class(runtime, "TimeoutError", *runtime.find_builtin("OSError"));

  register_exception_class(runtime, "ReferenceError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "RuntimeError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "NotImplementedError", *runtime.find_builtin("RuntimeError"));
  register_exception_class(runtime, "PythonFinalizationError", *runtime.find_builtin("RuntimeError"));
  register_exception_class(runtime, "RecursionError", *runtime.find_builtin("RuntimeError"));
  register_exception_class(runtime, "StopAsyncIteration", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "StopIteration", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "SyntaxError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "_IncompleteInputError", *runtime.find_builtin("SyntaxError"));
  register_exception_class(runtime, "IndentationError", *runtime.find_builtin("SyntaxError"));
  register_exception_class(runtime, "TabError", *runtime.find_builtin("IndentationError"));
  register_exception_class(runtime, "SystemError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "TypeError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "ValueError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "UnicodeError", *runtime.find_builtin("ValueError"));
  register_exception_class(runtime, "UnicodeDecodeError", *runtime.find_builtin("UnicodeError"));
  register_exception_class(runtime, "UnicodeEncodeError", *runtime.find_builtin("UnicodeError"));
  register_exception_class(runtime, "UnicodeTranslateError", *runtime.find_builtin("UnicodeError"));

  register_exception_class(runtime, "Warning", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "BytesWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "DeprecationWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "EncodingWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "FutureWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "ImportWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "PendingDeprecationWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "ResourceWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "RuntimeWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "SyntaxWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "UnicodeWarning", *runtime.find_builtin("Warning"));
  register_exception_class(runtime, "UserWarning", *runtime.find_builtin("Warning"));
}

} // namespace xlang3
