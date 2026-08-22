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

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

bool types_module_type(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ModuleType() expected name and optional doc";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "ModuleType name", name, error)) {
    return false;
  }
  out = Value::module(name);
  if (argc == 2) {
    module_set_attr(out, "__doc__", args[1], error);
  }
  return true;
}

bool types_method_type(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "MethodType() expected function and instance";
    return false;
  }
  if (value_as_function(args[0]) == nullptr && value_as_native_function(args[0]) == nullptr) {
    error = "MethodType() first argument must be callable";
    return false;
  }
  out = Value::bound_method(args[1], args[0]);
  return true;
}

bool types_mapping_proxy_type(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "MappingProxyType() expected mapping";
    return false;
  }
  out = args[0];
  return true;
}

bool types_simple_namespace(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 0) {
    error = "SimpleNamespace() expected keyword arguments only";
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("types")});
  Value klass = Value::class_object("SimpleNamespace", std::move(attrs));
  out = Value::instance(klass);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "SimpleNamespace keyword argument is invalid";
      return false;
    }
    if (!object_set_attr(out, kwargs[i].name, *kwargs[i].value, error)) {
      return false;
    }
  }
  return true;
}

bool types_simple_namespace_positional(Runtime&, const Value*, uint32_t argc, Value&, std::string& error, void*) {
  if (argc != 0) {
    error = "SimpleNamespace() expected keyword arguments only";
    return false;
  }
  error = "SimpleNamespace() requires keyword-call path";
  return false;
}

Value make_types_class(const std::string& name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("types")});
  return Value::class_object(name, std::move(attrs));
}

} // namespace

void register_types_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "types");
  builder.function("ModuleType", types_module_type)
      .function("MethodType", types_method_type)
      .function("MappingProxyType", types_mapping_proxy_type);
  builder.value("FrameType", make_types_class("FrameType"))
      .value("CodeType", make_types_class("CodeType"))
      .value("FunctionType", make_types_class("FunctionType"))
      .value("LambdaType", make_types_class("FunctionType"))
      .value("BuiltinFunctionType", make_types_class("BuiltinFunctionType"))
      .value("BuiltinMethodType", make_types_class("BuiltinFunctionType"))
      .value("MethodWrapperType", make_types_class("MethodWrapperType"))
      .value("TracebackType", make_types_class("TracebackType"))
      .value("GeneratorType", make_types_class("GeneratorType"))
      .value("CoroutineType", make_types_class("CoroutineType"))
      .value("DynamicClassAttribute", make_types_class("DynamicClassAttribute"));
  builder.value(
      "SimpleNamespace",
      runtime.make_native_function(
          "types.SimpleNamespace",
          types_simple_namespace_positional,
          nullptr,
          nullptr,
          nullptr,
          false,
          types_simple_namespace));
  runtime.register_module("types", builder.finish());
}

} // namespace xlang3
