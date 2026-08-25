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

#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include <filesystem>

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

bool module_spec_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 5) {
    error = "ModuleSpec.__init__ expected name, loader, optional origin/is_package";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[1], "ModuleSpec name", name, error)) {
    return false;
  }
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "name", Value::string(name), ignored);
  object_set_attr(self, "loader", args[2], ignored);
  object_set_attr(self, "origin", argc >= 4 ? args[3] : Value::none(), ignored);
  object_set_attr(self, "cached", Value::none(), ignored);
  const auto dot = name.rfind('.');
  object_set_attr(self, "parent", Value::string(dot == std::string::npos ? "" : name.substr(0, dot)), ignored);
  const bool is_package = argc >= 5 && value_truthy(args[4]);
  object_set_attr(self, "submodule_search_locations", is_package ? Value::list({}) : Value::none(), ignored);
  object_set_attr(self, "has_location", Value::boolean(argc >= 4 && args[3].tag != ValueTag::None), ignored);
  value_set_none(out);
  return true;
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

bool importlib_loader_exec_module(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "loader.exec_module expected self and module";
    return false;
  }
  Value path_value;
  std::string ignored;
  if (!object_get_attr(args[0], "path", path_value, ignored)) {
    value_set_none(out);
    return true;
  }
  std::string path;
  if (!get_string_arg(path_value, "loader path", path, error)) {
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!runtime.vfs().read_file(path, bytes, error)) {
    return false;
  }
  std::string source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  auto parsed = parse_source(source);
  if (!parsed.errors.empty()) {
    error = parsed.errors.front();
    runtime.raise_class_error("SyntaxError", error);
    return false;
  }
  auto lowered = lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    error = lowered.errors.front();
    runtime.raise_class_error("SyntaxError", error);
    return false;
  }
  auto module_ir = std::make_shared<ir::Module>(std::move(lowered.module));
  module_ir->source_file = path;
  Value module_value = args[1];
  module_set_attr(module_value, "__file__", Value::string(path), ignored);
  Interpreter interpreter(runtime);
  auto result = interpreter.run_module(*module_ir, module_value, module_ir);
  if (!result.errors.empty()) {
    error = result.errors.front();
    return false;
  }
  value_set_none(out);
  return true;
}

bool importlib_finder_find_spec(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "finder.find_spec expected fullname";
    return false;
  }
  std::string name;
  const Value& name_arg = argc == 1 ? args[0] : args[1];
  if (!get_string_arg(name_arg, "finder fullname", name, error)) {
    return false;
  }
  Value module;
  std::string ignored;
  if (runtime.has_registered_module(name) && runtime.import_module(name, module, ignored)) {
    std::string ignored;
    if (module_get_attr(module, "__spec__", out, ignored) && out.tag != ValueTag::None && out.tag != ValueTag::Invalid) {
      return true;
    }
    out = make_module_spec(name, module);
    return true;
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
  if (!name.empty() && name.front() == '.') {
    if (argc != 2 || args[1].tag == ValueTag::None) {
      error = "the 'package' argument is required to perform a relative import";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    std::string package;
    if (!get_string_arg(args[1], "importlib.import_module package", package, error)) {
      return false;
    }
    size_t dots = 0;
    while (dots < name.size() && name[dots] == '.') {
      ++dots;
    }
    for (size_t i = 1; i < dots && !package.empty(); ++i) {
      const auto cut = package.rfind('.');
      package = cut == std::string::npos ? std::string() : package.substr(0, cut);
    }
    const auto tail = name.substr(dots);
    name = package;
    if (!tail.empty()) {
      if (!name.empty()) {
        name += ".";
      }
      name += tail;
    }
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
    std::string ignored;
    if (module_get_attr(module, "__spec__", out, ignored) && out.tag != ValueTag::None && out.tag != ValueTag::Invalid) {
      return true;
    }
    out = make_module_spec(name, module);
    return true;
  }
  value_set_none(out);
  return true;
}

bool importlib_resolve_name(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "importlib.util.resolve_name() expected name and package";
    return false;
  }
  std::string name;
  std::string package;
  if (!get_string_arg(args[0], "resolve_name name", name, error) ||
      !get_string_arg(args[1], "resolve_name package", package, error)) {
    return false;
  }
  if (name.empty() || name.front() != '.') {
    out = Value::string(name);
    return true;
  }
  size_t dots = 0;
  while (dots < name.size() && name[dots] == '.') {
    ++dots;
  }
  for (size_t i = 1; i < dots && !package.empty(); ++i) {
    const auto cut = package.rfind('.');
    package = cut == std::string::npos ? std::string() : package.substr(0, cut);
  }
  const std::string tail = name.substr(dots);
  if (!tail.empty()) {
    if (!package.empty()) {
      package += ".";
    }
    package += tail;
  }
  out = Value::string(package);
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

bool package_base_path(Runtime& runtime, const Value& package_arg, std::string& out, std::string& error) {
  Value module;
  if (auto* package_text = value_as_string(package_arg)) {
    if (!runtime.import_module(string_object_to_string(*package_text), module, error)) {
      return false;
    }
  } else if (value_as_module(package_arg) != nullptr) {
    module = package_arg;
  } else {
    error = "package must be module or str";
    return false;
  }

  Value path_attr;
  std::string ignored;
  if (module_get_attr(module, "__path__", path_attr, ignored)) {
    if (auto* path_text = value_as_string(path_attr)) {
      out = string_object_to_string(*path_text);
      return true;
    }
    if (auto* list = value_as_list(path_attr); list != nullptr && !list->items.empty()) {
      if (auto* first = value_as_string(list->items[0])) {
        out = string_object_to_string(*first);
        return true;
      }
    }
  }
  if (module_get_attr(module, "__file__", path_attr, ignored)) {
    if (auto* file_text = value_as_string(path_attr)) {
      out = std::filesystem::path(string_object_to_string(*file_text)).parent_path().string();
      return true;
    }
  }
  error = "package has no filesystem location";
  return false;
}

std::string join_resource_path(std::string base, const std::string& resource) {
  if (base.empty()) {
    return resource;
  }
  const char last = base.back();
  if (last == '/' || last == '\\') {
    return base + resource;
  }
  return base + "/" + resource;
}

bool importlib_resources_files(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "importlib.resources.files() expected package";
    return false;
  }
  std::string base;
  if (!package_base_path(runtime, args[0], base, error)) {
    return false;
  }
  out = Value::string(base);
  return true;
}

bool importlib_resources_read_binary(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "importlib.resources.read_binary() expected package and resource";
    return false;
  }
  std::string base;
  std::string resource;
  if (!package_base_path(runtime, args[0], base, error) ||
      !get_string_arg(args[1], "resource", resource, error)) {
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!runtime.vfs().read_file(join_resource_path(base, resource), bytes, error)) {
    return false;
  }
  out = Value::bytes(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
  return true;
}

bool importlib_resources_read_text(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "importlib.resources.read_text() expected package, resource, and optional encoding";
    return false;
  }
  Value bytes;
  if (!importlib_resources_read_binary(runtime, args, 2, bytes, error, nullptr)) {
    return false;
  }
  auto* data = value_as_bytes(bytes);
  if (data == nullptr) {
    error = "resource read did not return bytes";
    return false;
  }
  const auto view = bytes_object_view(*data);
  out = Value::string(std::string(view.data(), view.size()));
  return true;
}

bool importlib_resources_is_resource(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "importlib.resources.is_resource() expected package and name";
    return false;
  }
  std::string base;
  std::string resource;
  if (!package_base_path(runtime, args[0], base, error) ||
      !get_string_arg(args[1], "resource", resource, error)) {
    return false;
  }
  VfsStat stat;
  std::string stat_error;
  out = Value::boolean(runtime.vfs().stat(join_resource_path(base, resource), stat, stat_error) && stat.kind == VfsNodeKind::File);
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

  Value module_spec_class = make_simple_class(
      "ModuleSpec",
      {{"__init__", runtime.make_native_function("ModuleSpec.__init__", module_spec_init)}});

  NativeModuleBuilder machinery_builder(runtime, "importlib.machinery");
  machinery_builder.value("ModuleSpec", module_spec_class)
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
      .function("resolve_name", importlib_resolve_name)
      .function("spec_from_file_location", importlib_spec_from_file_location)
      .function("module_from_spec", importlib_module_from_spec);
  Value util = util_builder.finish();
  runtime.register_module("importlib.util", util);

  NativeModuleBuilder metadata_builder(runtime, "importlib.metadata");
  metadata_builder.function("distributions", importlib_metadata_distributions);
  Value metadata = metadata_builder.finish();
  runtime.register_module("importlib.metadata", metadata);
  runtime.register_module("importlib_metadata", metadata);

  NativeModuleBuilder resources_builder(runtime, "importlib.resources");
  resources_builder.function("files", importlib_resources_files)
      .function("read_binary", importlib_resources_read_binary)
      .function("read_text", importlib_resources_read_text)
      .function("is_resource", importlib_resources_is_resource);
  Value resources = resources_builder.finish();
  runtime.register_module("importlib.resources", resources);
  runtime.register_module("importlib_resources", resources);

  NativeModuleBuilder frozen_builder(runtime, "_frozen_importlib");
  frozen_builder.value("__name__", Value::string("_frozen_importlib"))
      .value("BuiltinImporter", builtin_importer)
      .value("FrozenImporter", frozen_importer)
      .value("ModuleSpec", module_spec_class);
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
      .value("resources", resources)
      .value("_bootstrap", frozen)
      .value("_bootstrap_external", external);
  runtime.register_module("importlib", builder.finish());
}

} // namespace xlang3
