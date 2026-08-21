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

constexpr const char* kTypingAliasNativeType = "typing._Alias";

bool typing_alias_getitem(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "typing alias expected one subscript";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool typing_identity_decorator(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "typing decorator expected one function";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

Value make_alias(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("typing")});
  attrs.push_back({"__getitem__", runtime.make_native_function("typing._Alias.__getitem__", typing_alias_getitem)});
  Value klass = Value::class_object("_Alias", std::move(attrs));
  Value instance = Value::instance(klass);
  std::string error;
  object_set_attr(instance, "__name__", Value::string(name), error);
  instance_set_native_data(instance, kTypingAliasNativeType, reinterpret_cast<void*>(1), nullptr, error);
  return instance;
}

bool type_check_only(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "type_check_only() expected optional function";
    return false;
  }
  if (argc == 1) {
    value_assign_fast(out, args[0]);
    return true;
  }
  out = runtime.make_native_function("typing.type_check_only.<decorator>", typing_identity_decorator);
  return true;
}

bool cast_entry(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "cast() expected type and value";
    return false;
  }
  value_assign_fast(out, args[1]);
  return true;
}

} // namespace

void register_typing_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "typing");
  builder.value("Any", make_alias(runtime, "Any"))
      .value("Literal", make_alias(runtime, "Literal"))
      .value("Optional", make_alias(runtime, "Optional"))
      .value("Tuple", make_alias(runtime, "Tuple"))
      .value("TextIO", make_alias(runtime, "TextIO"))
      .value("Union", make_alias(runtime, "Union"))
      .value("Callable", make_alias(runtime, "Callable"))
      .value("Dict", make_alias(runtime, "Dict"))
      .value("List", make_alias(runtime, "List"))
      .value("TYPE_CHECKING", Value::boolean(false))
      .function("cast", cast_entry)
      .function("type_check_only", type_check_only);
  runtime.register_module("typing", builder.finish());
}

} // namespace xlang3
