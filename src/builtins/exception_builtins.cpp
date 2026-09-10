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

#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

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

bool exception_is_syntax_error_family(const Value& self) {
  auto* instance = value_as_instance(self);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  return klass != nullptr && (klass->name == "SyntaxError" || class_has_builtin_base_name(klass, "SyntaxError"));
}

bool exception_is_import_error_family(const Value& self) {
  auto* instance = value_as_instance(self);
  if (instance == nullptr) {
    return false;
  }
  auto* klass = value_as_class(instance->klass);
  return klass != nullptr &&
      (klass->name == "ImportError" || class_has_builtin_base_name(klass, "ImportError"));
}

bool exception_is_group_family(const Value& self, bool& exception_only) {
  auto* instance = value_as_instance(self);
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (klass == nullptr) return false;
  exception_only = klass->name == "ExceptionGroup" || class_has_builtin_base_name(klass, "ExceptionGroup");
  return exception_only || klass->name == "BaseExceptionGroup" ||
      class_has_builtin_base_name(klass, "BaseExceptionGroup");
}

void initialize_syntax_error_attrs(Value& self, const Value* args, uint32_t argc) {
  Value filename = Value::none();
  Value lineno = Value::none();
  Value offset = Value::none();
  Value text = Value::none();
  Value end_lineno = Value::none();
  Value end_offset = Value::none();
  if (argc >= 3) {
    if (const auto* details = value_as_tuple(args[2])) {
      if (details->items.size() >= 4) {
        value_assign_fast(filename, details->items[0]);
        value_assign_fast(lineno, details->items[1]);
        value_assign_fast(offset, details->items[2]);
        value_assign_fast(text, details->items[3]);
      }
      if (details->items.size() >= 6) {
        value_assign_fast(end_lineno, details->items[4]);
        value_assign_fast(end_offset, details->items[5]);
      }
    }
  }
  std::string ignored;
  object_set_attr(self, "msg", argc >= 2 ? args[1] : Value::none(), ignored);
  object_set_attr(self, "filename", filename, ignored);
  object_set_attr(self, "lineno", lineno, ignored);
  object_set_attr(self, "offset", offset, ignored);
  object_set_attr(self, "text", text, ignored);
  object_set_attr(self, "end_lineno", end_lineno, ignored);
  object_set_attr(self, "end_offset", end_offset, ignored);
  object_set_attr(self, "print_file_and_line", Value::none(), ignored);
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
  const bool is_syntax_error = exception_is_syntax_error_family(args[0]);
  bool exception_group_requires_exception = false;
  const bool is_exception_group = exception_is_group_family(args[0], exception_group_requires_exception);
  std::vector<Value> group_exceptions;
  if (is_exception_group) {
    if (argc != 3 || value_as_string(args[1]) == nullptr) {
      error = "BaseExceptionGroup.__new__() requires a string message and a sequence of exceptions";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (!runtime_collect_iterable(runtime, args[2], group_exceptions, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (group_exceptions.empty()) {
      error = "second argument (exceptions) must be a non-empty sequence";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    for (const auto& exception : group_exceptions) {
      auto* exception_instance = value_as_instance(exception);
      auto* exception_class = exception_instance == nullptr ? nullptr : value_as_class(exception_instance->klass);
      const bool is_base_exception = exception_class != nullptr &&
          (exception_class->name == "BaseException" || class_has_builtin_base_name(exception_class, "BaseException"));
      const bool is_exception = exception_class != nullptr &&
          (exception_class->name == "Exception" || class_has_builtin_base_name(exception_class, "Exception"));
      if (!is_base_exception || (exception_group_requires_exception && !is_exception)) {
        error = "Item 0 of second argument (exceptions) is not an exception";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    }
  }
  const uint32_t stored_argc = is_os_error && argc >= 4 ? 3 : argc;
  for (uint32_t i = 1; i < stored_argc; ++i) {
    exception_args.push_back(args[i]);
  }
  Value args_tuple = Value::tuple(exception_args);
  Value message = Value::string("");
  if (is_exception_group) {
    value_assign_fast(message, args[1]);
  } else if (argc == 2) {
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
  if (is_exception_group) {
    object_set_attr(const_cast<Value&>(args[0]), "message", args[1], ignored);
    object_set_attr(
        const_cast<Value&>(args[0]), "exceptions", Value::tuple(std::move(group_exceptions)), ignored);
  }
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
  if (is_syntax_error) {
    initialize_syntax_error_attrs(const_cast<Value&>(args[0]), args, argc);
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

bool exception_add_note(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "BaseException.add_note() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_string(args[1]) == nullptr) {
    std::string type_name = "object";
    Value type;
    if (runtime_type_of_value(runtime, args[1], type)) {
      if (auto* klass = value_as_class(type)) type_name = klass->name;
    }
    error = "note must be a str, not '" + type_name + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value notes;
  std::string ignored;
  if (!object_get_attr(args[0], "__notes__", notes, ignored)) {
    notes = Value::list({});
    if (!object_set_attr(const_cast<Value&>(args[0]), "__notes__", notes, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  auto* list = value_as_list(notes);
  if (list == nullptr) {
    error = "Cannot add note: __notes__ is not a list";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  list->items.push_back(args[1]);
  value_set_none(out);
  return true;
}

bool exception_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1) {
    error = "Exception.__init__() self is missing";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!exception_is_import_error_family(args[0]) && kwargc != 0) {
    error = "Exception.__init__() takes no keyword arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value name = Value::none();
  Value path = Value::none();
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view keyword(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (keyword == "name") {
      value_assign_fast(name, *kwargs[i].value);
    } else if (keyword == "path") {
      value_assign_fast(path, *kwargs[i].value);
    } else {
      error = "ImportError() got an unexpected keyword argument '" +
          std::string(keyword) + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (!exception_init(runtime, args, argc, out, error, user_data)) {
    return false;
  }
  if (exception_is_import_error_family(args[0])) {
    std::string ignored;
    object_set_attr(const_cast<Value&>(args[0]), "name", name, ignored);
    object_set_attr(const_cast<Value&>(args[0]), "path", path, ignored);
  }
  return true;
}

bool exception_repr(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  auto* instance = argc == 1 ? value_as_instance(args[0]) : nullptr;
  auto* klass = instance == nullptr ? nullptr : value_as_class(instance->klass);
  if (klass == nullptr) {
    error = "BaseException.__repr__() expected an exception instance";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value stored_args;
  std::string ignored;
  if (!object_get_attr(args[0], "args", stored_args, ignored)) {
    stored_args = Value::tuple({});
  }
  auto* tuple = value_as_tuple(stored_args);
  if (tuple == nullptr) {
    error = "exception args must be a tuple";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string text = klass->name + "(";
  const Value* repr_function = runtime.find_builtin("repr");
  for (size_t i = 0; i < tuple->items.size(); ++i) {
    if (i != 0) text += ", ";
    Value item_repr;
    if (repr_function == nullptr ||
        !runtime_call_callable(runtime, *repr_function, &tuple->items[i], 1, item_repr, error)) {
      return false;
    }
    auto* item_text = value_as_string(item_repr);
    if (item_text == nullptr) {
      error = "repr() returned non-string";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    text += string_object_to_string(*item_text);
  }
  text += ")";
  out = Value::string(std::move(text));
  return true;
}

bool exception_str(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  auto* instance = argc == 1 ? value_as_instance(args[0]) : nullptr;
  if (instance == nullptr) {
    error = "BaseException.__str__() expected an exception instance";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool exception_only = false;
  if (exception_is_group_family(args[0], exception_only)) {
    Value message;
    Value exceptions;
    std::string ignored;
    object_get_attr(args[0], "message", message, ignored);
    object_get_attr(args[0], "exceptions", exceptions, ignored);
    const auto* text = value_as_string(message);
    const auto* items = value_as_tuple(exceptions);
    const size_t count = items == nullptr ? 0 : items->items.size();
    out = Value::string(
        (text == nullptr ? value_to_string(message) : string_object_to_string(*text)) +
        " (" + std::to_string(count) + " sub-exception" + (count == 1 ? "" : "s") + ")");
    return true;
  }
  if (exception_is_syntax_error_family(args[0])) {
    Value message;
    std::string ignored;
    if (object_get_attr(args[0], "msg", message, ignored)) {
      out = Value::string(value_to_string(message));
      return true;
    }
  }
  Value stored_args;
  std::string ignored;
  if (!object_get_attr(args[0], "args", stored_args, ignored)) {
    stored_args = Value::tuple({});
  }
  auto* tuple = value_as_tuple(stored_args);
  if (tuple == nullptr) {
    error = "exception args must be a tuple";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (tuple->items.empty()) {
    out = Value::string("");
    return true;
  }
  const char* builtin_name = tuple->items.size() == 1 ? "str" : "repr";
  const Value* formatter = runtime.find_builtin(builtin_name);
  const Value& value = tuple->items.size() == 1 ? tuple->items[0] : stored_args;
  if (formatter == nullptr || !runtime_call_callable(runtime, *formatter, &value, 1, out, error)) {
    return false;
  }
  if (value_as_string(out) == nullptr) {
    error = std::string(builtin_name) + "() returned non-string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

void register_exception_class(Runtime& runtime, const char* name, Value base = Value::invalid()) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.emplace_back("__module__", Value::string("builtins"));
  attrs.emplace_back("__qualname__", Value::string(name));
  attrs.emplace_back(
      "__init__",
      runtime.make_native_function(
          std::string(name) + ".__init__",
          exception_init,
          nullptr,
          nullptr,
          nullptr,
          false,
          exception_init_kw));
  if (std::string_view(name) == "BaseException") {
    attrs.emplace_back(
        "__repr__",
        runtime.make_native_function("BaseException.__repr__", exception_repr));
    attrs.emplace_back(
        "__str__",
        runtime.make_native_function("BaseException.__str__", exception_str));
    attrs.emplace_back(
        "with_traceback",
        runtime.make_native_function("BaseException.with_traceback", exception_with_traceback));
    attrs.emplace_back(
        "add_note",
        runtime.make_native_function("BaseException.add_note", exception_add_note));
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
