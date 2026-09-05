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

#include <string>

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
  copy_builtin(runtime, builtins, "_identity");
  copy_builtin(runtime, builtins, "classmethod");
  copy_builtin(runtime, builtins, "staticmethod");
  copy_builtin(runtime, builtins, "super");
  copy_builtin(runtime, builtins, "len");
  copy_builtin(runtime, builtins, "next");
  copy_builtin(runtime, builtins, "ord");
  copy_builtin(runtime, builtins, "str");
  copy_builtin(runtime, builtins, "repr");
  copy_builtin(runtime, builtins, "hash");
  copy_builtin(runtime, builtins, "chr");
  copy_builtin(runtime, builtins, "bin");
  copy_builtin(runtime, builtins, "oct");
  copy_builtin(runtime, builtins, "hex");
  copy_builtin(runtime, builtins, "pow");
  copy_builtin(runtime, builtins, "divmod");
  copy_builtin(runtime, builtins, "all");
  copy_builtin(runtime, builtins, "any");
  copy_builtin(runtime, builtins, "range");
  copy_builtin(runtime, builtins, "object");
  copy_builtin(runtime, builtins, "type");
  copy_builtin(runtime, builtins, "Ellipsis");
  copy_builtin(runtime, builtins, "NotImplemented");
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
  copy_builtin(runtime, builtins, "frozenset");
  copy_builtin(runtime, builtins, "BaseException");
  copy_builtin(runtime, builtins, "BaseExceptionGroup");
  copy_builtin(runtime, builtins, "GeneratorExit");
  copy_builtin(runtime, builtins, "KeyboardInterrupt");
  copy_builtin(runtime, builtins, "SystemExit");
  copy_builtin(runtime, builtins, "Exception");
  copy_builtin(runtime, builtins, "ArithmeticError");
  copy_builtin(runtime, builtins, "FloatingPointError");
  copy_builtin(runtime, builtins, "OverflowError");
  copy_builtin(runtime, builtins, "ZeroDivisionError");
  copy_builtin(runtime, builtins, "AssertionError");
  copy_builtin(runtime, builtins, "AttributeError");
  copy_builtin(runtime, builtins, "BufferError");
  copy_builtin(runtime, builtins, "EOFError");
  copy_builtin(runtime, builtins, "ExceptionGroup");
  copy_builtin(runtime, builtins, "ImportError");
  copy_builtin(runtime, builtins, "ModuleNotFoundError");
  copy_builtin(runtime, builtins, "LookupError");
  copy_builtin(runtime, builtins, "IndexError");
  copy_builtin(runtime, builtins, "KeyError");
  copy_builtin(runtime, builtins, "MemoryError");
  copy_builtin(runtime, builtins, "NameError");
  copy_builtin(runtime, builtins, "UnboundLocalError");
  copy_builtin(runtime, builtins, "OSError");
  copy_builtin(runtime, builtins, "EnvironmentError");
  copy_builtin(runtime, builtins, "IOError");
  copy_builtin(runtime, builtins, "WindowsError");
  copy_builtin(runtime, builtins, "BlockingIOError");
  copy_builtin(runtime, builtins, "ChildProcessError");
  copy_builtin(runtime, builtins, "ConnectionError");
  copy_builtin(runtime, builtins, "BrokenPipeError");
  copy_builtin(runtime, builtins, "ConnectionAbortedError");
  copy_builtin(runtime, builtins, "ConnectionRefusedError");
  copy_builtin(runtime, builtins, "ConnectionResetError");
  copy_builtin(runtime, builtins, "FileExistsError");
  copy_builtin(runtime, builtins, "FileNotFoundError");
  copy_builtin(runtime, builtins, "InterruptedError");
  copy_builtin(runtime, builtins, "IsADirectoryError");
  copy_builtin(runtime, builtins, "NotADirectoryError");
  copy_builtin(runtime, builtins, "PermissionError");
  copy_builtin(runtime, builtins, "ProcessLookupError");
  copy_builtin(runtime, builtins, "TimeoutError");
  copy_builtin(runtime, builtins, "ReferenceError");
  copy_builtin(runtime, builtins, "RuntimeError");
  copy_builtin(runtime, builtins, "NotImplementedError");
  copy_builtin(runtime, builtins, "PythonFinalizationError");
  copy_builtin(runtime, builtins, "RecursionError");
  copy_builtin(runtime, builtins, "StopAsyncIteration");
  copy_builtin(runtime, builtins, "StopIteration");
  copy_builtin(runtime, builtins, "SyntaxError");
  copy_builtin(runtime, builtins, "_IncompleteInputError");
  copy_builtin(runtime, builtins, "IndentationError");
  copy_builtin(runtime, builtins, "TabError");
  copy_builtin(runtime, builtins, "SystemError");
  copy_builtin(runtime, builtins, "TypeError");
  copy_builtin(runtime, builtins, "ValueError");
  copy_builtin(runtime, builtins, "UnicodeError");
  copy_builtin(runtime, builtins, "UnicodeDecodeError");
  copy_builtin(runtime, builtins, "UnicodeEncodeError");
  copy_builtin(runtime, builtins, "UnicodeTranslateError");
  copy_builtin(runtime, builtins, "Warning");
  copy_builtin(runtime, builtins, "BytesWarning");
  copy_builtin(runtime, builtins, "DeprecationWarning");
  copy_builtin(runtime, builtins, "EncodingWarning");
  copy_builtin(runtime, builtins, "FutureWarning");
  copy_builtin(runtime, builtins, "ImportWarning");
  copy_builtin(runtime, builtins, "PendingDeprecationWarning");
  copy_builtin(runtime, builtins, "ResourceWarning");
  copy_builtin(runtime, builtins, "RuntimeWarning");
  copy_builtin(runtime, builtins, "SyntaxWarning");
  copy_builtin(runtime, builtins, "UnicodeWarning");
  copy_builtin(runtime, builtins, "UserWarning");
  copy_builtin(runtime, builtins, "locals");
  copy_builtin(runtime, builtins, "compile");
  copy_builtin(runtime, builtins, "eval");
  copy_builtin(runtime, builtins, "exec");
  copy_builtin(runtime, builtins, "open");
  copy_builtin(runtime, builtins, "register_remote_object");
  copy_builtin(runtime, builtins, "lrpc_listen");
  runtime.register_module("_builtins", builtins);
  runtime.register_module("builtins", std::move(builtins));

}

} // namespace xlang3
