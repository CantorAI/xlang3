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
#include "xlang3/vfs.h"

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

Value make_module_spec_for_file(const std::string& name, const std::string& path, const Value& loader) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("importlib")});
  Value klass = Value::class_object("ModuleSpec", std::move(attrs));
  Value spec = Value::instance(klass);
  std::string ignored;
  object_set_attr(spec, "name", Value::string(name), ignored);
  object_set_attr(spec, "loader", loader, ignored);
  object_set_attr(spec, "origin", Value::string(path), ignored);
  object_set_attr(spec, "cached", Value::none(), ignored);
  const auto dot = name.rfind('.');
  object_set_attr(spec, "parent", Value::string(dot == std::string::npos ? "" : name.substr(0, dot)), ignored);
  return spec;
}

bool importlib_loader_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3) {
    error = "loader.__init__ expected self, name, path";
    return false;
  }
  std::string name;
  std::string path;
  if (!get_string_arg(args[1], "loader name", name, error) ||
      !get_string_arg(args[2], "loader path", path, error)) {
    return false;
  }
  std::string ignored;
  Value self = args[0];
  object_set_attr(self, "name", Value::string(name), ignored);
  object_set_attr(self, "path", Value::string(path), ignored);
  value_set_none(out);
  return true;
}

bool importlib_loader_create_module(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "loader.create_module expected self and spec";
    return false;
  }
  value_set_none(out);
  return true;
}

bool importlib_loader_get_filename(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "loader.get_filename expected self and optional fullname";
    return false;
  }
  Value path;
  std::string ignored;
  if (!object_get_attr(args[0], "path", path, ignored)) {
    error = "loader has no path";
    return false;
  }
  value_assign_fast(out, path);
  return true;
}

bool importlib_loader_get_data(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "loader.get_data expected self and path";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[1], "loader path", path, error)) {
    return false;
  }
  std::vector<uint8_t> data;
  if (!runtime.vfs().read_file(path, data, error)) {
    return false;
  }
  out = Value::bytes(std::string(reinterpret_cast<const char*>(data.data()), data.size()));
  return true;
}

bool importlib_loader_exec_module(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "loader.exec_module expected self and module";
    return false;
  }
  value_set_none(out);
  return true;
}

bool importlib_finder_find_spec(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "finder.find_spec expected fullname";
    return false;
  }
  value_set_none(out);
  return true;
}

Value make_simple_class(const std::string& name, std::vector<std::pair<std::string, Value>> attrs = {}) {
  attrs.push_back({"__module__", Value::string("importlib")});
  return Value::class_object(name, std::move(attrs));
}

Value make_loader_class(Runtime& runtime, const std::string& name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function(name + ".__init__", importlib_loader_init)});
  attrs.push_back({"create_module", runtime.make_native_function(name + ".create_module", importlib_loader_create_module)});
  attrs.push_back({"exec_module", runtime.make_native_function(name + ".exec_module", importlib_loader_exec_module)});
  attrs.push_back({"get_filename", runtime.make_native_function(name + ".get_filename", importlib_loader_get_filename)});
  attrs.push_back({"get_data", runtime.make_native_function(name + ".get_data", importlib_loader_get_data)});
  return make_simple_class(name, std::move(attrs));
}

Value make_finder_class(Runtime& runtime, const std::string& name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"find_spec", runtime.make_native_function(name + ".find_spec", importlib_finder_find_spec)});
  return make_simple_class(name, std::move(attrs));
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

bool importlib_spec_from_file_location(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "importlib.util.spec_from_file_location() expected name, location and optional loader";
    return false;
  }
  std::string name;
  std::string path;
  if (!get_string_arg(args[0], "spec name", name, error) || !get_string_arg(args[1], "spec location", path, error)) {
    return false;
  }
  out = make_module_spec_for_file(name, path, argc == 3 ? args[2] : Value::none());
  return true;
}

bool importlib_module_from_spec(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "importlib.util.module_from_spec() expected spec";
    return false;
  }
  Value name_value;
  std::string ignored;
  std::string name = "";
  if (object_get_attr(args[0], "name", name_value, ignored)) {
    if (auto* str = value_as_string(name_value)) {
      name = string_object_to_string(*str);
    }
  }
  out = Value::module(name);
  module_set_attr(out, "__name__", Value::string(name), ignored);
  module_set_attr(out, "__spec__", args[0], ignored);
  Value origin;
  if (object_get_attr(args[0], "origin", origin, ignored)) {
    module_set_attr(out, "__file__", origin, ignored);
  }
  Value parent;
  if (object_get_attr(args[0], "parent", parent, ignored)) {
    module_set_attr(out, "__package__", parent, ignored);
  }
  return true;
}

bool importlib_metadata_distributions(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "importlib.metadata.distributions() expected no arguments";
    return false;
  }
  out = Value::list({});
  return true;
}

} // namespace

void register_importlib_module(Runtime& runtime) {
  Value loader = make_simple_class("Loader");
  Value resource_loader = make_simple_class("ResourceLoader", {{"get_data", runtime.make_native_function("ResourceLoader.get_data", importlib_loader_get_data)}});
  Value inspection_loader = make_simple_class("InspectLoader");
  Value execution_loader = make_simple_class("ExecutionLoader");
  Value file_loader = make_loader_class(runtime, "FileLoader");
  Value source_loader = make_loader_class(runtime, "SourceLoader");
  Value source_file_loader = make_loader_class(runtime, "SourceFileLoader");
  Value sourceless_file_loader = make_loader_class(runtime, "SourcelessFileLoader");
  Value extension_file_loader = make_loader_class(runtime, "ExtensionFileLoader");
  Value meta_path_finder = make_finder_class(runtime, "MetaPathFinder");
  Value path_entry_finder = make_finder_class(runtime, "PathEntryFinder");
  Value path_finder = make_finder_class(runtime, "PathFinder");
  Value file_finder = make_finder_class(runtime, "FileFinder");
  Value builtin_importer = make_finder_class(runtime, "BuiltinImporter");
  Value frozen_importer = make_finder_class(runtime, "FrozenImporter");
  Value namespace_loader = make_loader_class(runtime, "NamespaceLoader");

  NativeModuleBuilder machinery_builder(runtime, "importlib.machinery");
  machinery_builder.value("ModuleSpec", make_simple_class("ModuleSpec"))
      .value("BuiltinImporter", builtin_importer)
      .value("FrozenImporter", frozen_importer)
      .value("PathFinder", path_finder)
      .value("FileFinder", file_finder)
      .value("SourceFileLoader", source_file_loader)
      .value("SourcelessFileLoader", sourceless_file_loader)
      .value("ExtensionFileLoader", extension_file_loader)
      .value("NamespaceLoader", namespace_loader)
      .value("SOURCE_SUFFIXES", Value::list({Value::string(".py")}))
      .value("BYTECODE_SUFFIXES", Value::list({Value::string(".pyc")}))
      .value("EXTENSION_SUFFIXES", Value::list({}));
  Value machinery = machinery_builder.finish();
  runtime.register_module("importlib.machinery", machinery);

  NativeModuleBuilder abc_builder(runtime, "importlib.abc");
  abc_builder.value("Loader", loader)
      .value("ResourceLoader", resource_loader)
      .value("InspectLoader", inspection_loader)
      .value("ExecutionLoader", execution_loader)
      .value("FileLoader", file_loader)
      .value("SourceLoader", source_loader)
      .value("MetaPathFinder", meta_path_finder)
      .value("PathEntryFinder", path_entry_finder);
  Value abc = abc_builder.finish();
  runtime.register_module("importlib.abc", abc);

  NativeModuleBuilder util_builder(runtime, "importlib.util");
  util_builder.function("find_spec", importlib_find_spec)
      .function("spec_from_file_location", importlib_spec_from_file_location)
      .function("module_from_spec", importlib_module_from_spec);
  Value util = util_builder.finish();
  runtime.register_module("importlib.util", util);

  NativeModuleBuilder metadata_builder(runtime, "importlib.metadata");
  metadata_builder.function("distributions", importlib_metadata_distributions);
  Value metadata = metadata_builder.finish();
  runtime.register_module("importlib.metadata", metadata);
  runtime.register_module("importlib_metadata", metadata);

  NativeModuleBuilder frozen_builder(runtime, "_frozen_importlib");
  frozen_builder.value("__name__", Value::string("_frozen_importlib"))
      .value("BuiltinImporter", builtin_importer)
      .value("FrozenImporter", frozen_importer)
      .value("ModuleSpec", make_simple_class("ModuleSpec"));
  Value frozen = frozen_builder.finish();
  runtime.register_module("_frozen_importlib", frozen);
  runtime.register_module("importlib._bootstrap", frozen);

  NativeModuleBuilder external_builder(runtime, "_frozen_importlib_external");
  external_builder.value("__name__", Value::string("_frozen_importlib_external"))
      .value("FileFinder", file_finder)
      .value("PathFinder", path_finder)
      .value("SourceFileLoader", source_file_loader)
      .value("SourcelessFileLoader", sourceless_file_loader)
      .value("ExtensionFileLoader", extension_file_loader)
      .value("NamespaceLoader", namespace_loader)
      .value("SOURCE_SUFFIXES", Value::list({Value::string(".py")}))
      .value("BYTECODE_SUFFIXES", Value::list({Value::string(".pyc")}))
      .value("EXTENSION_SUFFIXES", Value::list({}));
  Value external = external_builder.finish();
  runtime.register_module("_frozen_importlib_external", external);
  runtime.register_module("importlib._bootstrap_external", external);

  NativeModuleBuilder builder(runtime, "importlib");
  builder.function("import_module", importlib_import_module)
      .function("invalidate_caches", importlib_invalidate_caches)
      .value("abc", abc)
      .value("machinery", machinery)
      .value("util", util)
      .value("metadata", metadata)
      .value("_bootstrap", frozen)
      .value("_bootstrap_external", external);
  runtime.register_module("importlib", builder.finish());
}

} // namespace xlang3
