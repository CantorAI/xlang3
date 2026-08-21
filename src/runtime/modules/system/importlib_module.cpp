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

Value make_module_spec(const std::string& name, const Value& module) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("importlib")});
  Value klass = Value::class_object("ModuleSpec", std::move(attrs));
  Value spec = Value::instance(klass);
  std::string ignored;
  object_set_attr(spec, "name", Value::string(name), ignored);
  object_set_attr(spec, "loader", Value::none(), ignored);
  object_set_attr(spec, "origin", Value::string("built-in"), ignored);
  object_set_attr(spec, "cached", Value::none(), ignored);
  object_set_attr(spec, "parent", Value::string(""), ignored);
  if (auto* module_object = value_as_module(module)) {
    Value file;
    if (module_get_attr(module, "__file__", file, ignored)) {
      object_set_attr(spec, "origin", file, ignored);
    }
    const auto dot = module_object->name.rfind('.');
    if (dot != std::string::npos) {
      object_set_attr(spec, "parent", Value::string(module_object->name.substr(0, dot)), ignored);
    }
  }
  return spec;
}

bool importlib_import_module(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "importlib.import_module() expected name and optional package";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "importlib.import_module name", name, error)) {
    return false;
  }
  if (!runtime.import_module(name, out, error)) {
    return false;
  }
  return true;
}

bool importlib_invalidate_caches(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "importlib.invalidate_caches() expected no arguments";
    return false;
  }
  value_set_none(out);
  return true;
}

bool importlib_find_spec(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "importlib.util.find_spec() expected name and optional package";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "importlib.util.find_spec name", name, error)) {
    return false;
  }
  Value module;
  std::string import_error;
  if (runtime.import_module(name, module, import_error)) {
    out = make_module_spec(name, module);
    return true;
  }
  value_set_none(out);
  return true;
}

} // namespace

void register_importlib_module(Runtime& runtime) {
  NativeModuleBuilder util_builder(runtime, "importlib.util");
  util_builder.function("find_spec", importlib_find_spec);
  Value util = util_builder.finish();
  runtime.register_module("importlib.util", util);

  NativeModuleBuilder builder(runtime, "importlib");
  builder.function("import_module", importlib_import_module)
      .function("invalidate_caches", importlib_invalidate_caches)
      .value("util", util);
  runtime.register_module("importlib", builder.finish());
}

} // namespace xlang3
