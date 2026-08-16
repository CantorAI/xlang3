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

namespace xlang3 {

namespace {

bool exception_init(
    Runtime&,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    error = "Exception.__init__() expected 0 or 1 arguments";
    return false;
  }
  Value message = argc == 2 ? args[1] : Value::string("");
  std::string ignored;
  if (!object_set_attr(const_cast<Value&>(args[0]), "message", message, ignored)) {
    error = "Exception.__init__() self is invalid";
    return false;
  }
  object_set_attr(const_cast<Value&>(args[0]), "args", argc == 2 ? Value::tuple({message}) : Value::tuple({}), ignored);
  value_set_none(out);
  return true;
}

void register_exception_class(Runtime& runtime, const char* name, Value base = Value::invalid()) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.emplace_back("__init__", runtime.make_native_function(std::string(name) + ".__init__", exception_init));
  runtime.register_builtin(name, Value::class_object(name, std::move(attrs), std::move(base)));
}

} // namespace

void register_exception_builtins(Runtime& runtime) {
  register_exception_class(runtime, "BaseException");
  register_exception_class(runtime, "Exception", *runtime.find_builtin("BaseException"));
  register_exception_class(runtime, "RuntimeError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "TypeError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "ValueError", *runtime.find_builtin("Exception"));
  register_exception_class(runtime, "ImportError", *runtime.find_builtin("Exception"));
}

} // namespace xlang3
