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

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/import_loader.h"
#include "xlang3/interpreter.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace xlang3 {

namespace {

void preserve_or_raise_import_error(
    Runtime& runtime,
    bool module_not_found,
    const std::string& error) {
  Value pending;
  if (runtime.take_pending_exception(pending)) {
    runtime.set_pending_exception(std::move(pending));
  } else {
    runtime.raise_class_error(module_not_found ? "ModuleNotFoundError" : "ImportError", error);
  }
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

bool get_path_arg(Runtime& runtime, const Value& value, const char* name, std::string& out, std::string& error) {
  if (get_string_arg(value, name, out, error)) return true;
  error.clear();
  Value fspath;
  std::string ignored;
  if (!object_get_attr(value, "__fspath__", fspath, ignored)) {
    error = std::string(name) + " must be str or os.PathLike";
    return false;
  }
  Value path_value;
  if (!runtime_call_callable(runtime, fspath, nullptr, 0, path_value, error)) return false;
  return get_string_arg(path_value, name, out, error);
}

Value make_source_file_loader(Runtime& runtime, const std::string& name, const Value& path);
void normalize_file_module_spec_loader(Runtime& runtime, const std::string& name, Value& module, Value& spec);
bool importlib_finder_get_optional_code(
    Runtime&, const Value*, uint32_t, Value&, std::string&, void*);
bool importlib_finder_is_package(
    Runtime&, const Value*, uint32_t, Value&, std::string&, void*);

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

Value make_module_spec_for_file(
    const std::string& name,
    const std::string& path,
    const Value& loader,
    bool is_package = false,
    const std::string& package_dir = {}) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("importlib")});
  Value klass = Value::class_object("ModuleSpec", std::move(attrs));
  Value spec = Value::instance(klass);
  std::string ignored;
  object_set_attr(spec, "name", Value::string(name), ignored);
  object_set_attr(spec, "loader", loader, ignored);
  object_set_attr(spec, "origin", Value::string(path), ignored);
  std::string cached = path;
  const std::filesystem::path file_path(path);
  if (file_path.extension() == ".py") {
    cached = (file_path.parent_path() / "__pycache__" /
              (file_path.stem().string() + ".xlang3-314.pyc")).string();
  }
  object_set_attr(spec, "cached", Value::string(cached), ignored);
  const auto dot = name.rfind('.');
  object_set_attr(
      spec,
      "parent",
      Value::string(is_package ? name : (dot == std::string::npos ? "" : name.substr(0, dot))),
      ignored);
  object_set_attr(spec, "has_location", Value::boolean(true), ignored);
  object_set_attr(
      spec,
      "submodule_search_locations",
      is_package ? Value::list({Value::string(package_dir)}) : Value::none(),
      ignored);
  return spec;
}

bool find_module_spec_without_import(
    Runtime& runtime, const std::string& name, Value& out, bool check_registry = true) {
  Value registered;
  std::string ignored;
  if (check_registry && runtime.module_registry_dict().tag != ValueTag::Invalid &&
      mapping_get_item(runtime.module_registry_dict(), Value::string(name), registered, ignored)) {
    if (registered.tag == ValueTag::None) {
      value_set_none(out);
      return true;
    }
    if (value_as_module(registered) != nullptr) {
      if (module_get_attr(registered, "__spec__", out, ignored) &&
          out.tag != ValueTag::None && out.tag != ValueTag::Invalid) {
        normalize_file_module_spec_loader(runtime, name, registered, out);
      } else {
        out = make_module_spec(name, registered);
        normalize_file_module_spec_loader(runtime, name, registered, out);
      }
      return true;
    }
  }

  PythonModuleLocation location;
  if (!find_python_module_location(runtime, name, location)) {
    value_set_none(out);
    return true;
  }
  if (location.is_namespace_package) {
    out = make_module_spec_for_file(name, location.package_dir, Value::none(), true, location.package_dir);
    object_set_attr(out, "origin", Value::none(), ignored);
    object_set_attr(out, "cached", Value::none(), ignored);
    object_set_attr(out, "has_location", Value::boolean(false), ignored);
    std::vector<Value> paths;
    for (const auto& path : location.namespace_dirs) {
      paths.push_back(Value::string(path));
    }
    object_set_attr(out, "submodule_search_locations", Value::list(std::move(paths)), ignored);
    return true;
  }
  if (location.is_zip_source) {
    const size_t slash = location.path.find_first_of("/\\", location.path.find(".zip") + 4);
    if (slash != std::string::npos) {
      const std::string archive = location.path.substr(0, slash);
      Value zipimport_module;
      Value zipimporter_class;
      Value zip_loader;
      Value archive_arg = Value::string(archive);
      Value find_spec;
      Value name_arg = Value::string(name);
      std::string zip_error;
      if (runtime.import_module("zipimport", zipimport_module, zip_error) &&
          module_get_attr(zipimport_module, "zipimporter", zipimporter_class, zip_error) &&
          runtime_call_callable(runtime, zipimporter_class, &archive_arg, 1, zip_loader, zip_error) &&
          object_get_attr(zip_loader, "find_spec", find_spec, zip_error) &&
          runtime_call_callable(runtime, find_spec, &name_arg, 1, out, zip_error) &&
          out.tag != ValueTag::None && out.tag != ValueTag::Invalid) {
        return true;
      }
      Value ignored_pending;
      runtime.take_pending_exception(ignored_pending);
    }
  }
  Value loader = make_source_file_loader(runtime, name, Value::string(location.path));
  out = make_module_spec_for_file(name, location.path, loader, location.is_package, location.package_dir);
  return true;
}

bool object_string_attr_equals(const Value& object, const char* name, const char* expected) {
  Value value;
  std::string ignored;
  if (!object_get_attr(object, name, value, ignored)) {
    return false;
  }
  auto* string = value_as_string(value);
  return string != nullptr && string_object_to_string(*string) == expected;
}

void canonicalize_existing_builtin_import_loaders(Runtime& runtime, const Value& builtin_importer, const Value& frozen_importer) {
  auto* modules = value_as_dict(runtime.module_registry_dict());
  if (modules == nullptr) {
    return;
  }
  for (auto& entry : modules->entries) {
    Value& module = entry.second;
    if (value_as_module(module) == nullptr) {
      continue;
    }
    Value spec;
    std::string ignored;
    if (!module_get_attr(module, "__spec__", spec, ignored)) {
      continue;
    }
    Value loader;
    if (object_string_attr_equals(spec, "origin", "built-in")) {
      value_assign_fast(loader, builtin_importer);
    } else if (object_string_attr_equals(spec, "origin", "frozen")) {
      value_assign_fast(loader, frozen_importer);
    } else {
      continue;
    }
    module_set_attr(module, "__loader__", loader, ignored);
    object_set_attr(spec, "loader", loader, ignored);
  }
}

bool importlib_loader_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3) {
    error = "loader.__init__ expected self, name, path";
    return false;
  }
  std::string name;
  std::string path;
  if (!get_string_arg(args[1], "loader name", name, error) ||
      !get_path_arg(runtime, args[2], "loader path", path, error)) {
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
  if (!get_path_arg(runtime, args[1], "loader path", path, error)) {
    return false;
  }
  std::vector<uint8_t> data;
  if (!runtime.vfs().read_file(path, data, error)) {
    return false;
  }
  out = Value::bytes(std::string(reinterpret_cast<const char*>(data.data()), data.size()));
  return true;
}

bool importlib_loader_get_code(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "loader.get_code expected self and fullname";
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
  if (std::filesystem::path(path).extension() == ".pyc") {
    static constexpr unsigned char kMagic[] = {0x33, 0x58, 0x0d, 0x0a};
    if (bytes.size() < 16 || !std::equal(std::begin(kMagic), std::end(kMagic), bytes.begin())) {
      error = "bad magic number in bytecode file '" + path + "'";
      return false;
    }
    Value marshal_module;
    Value loads;
    Value payload = Value::bytes(std::string(
        reinterpret_cast<const char*>(bytes.data() + 16), bytes.size() - 16));
    if (!runtime.import_module("marshal", marshal_module, error) ||
        !module_get_attr(marshal_module, "loads", loads, error) ||
        !runtime_call_callable(runtime, loads, &payload, 1, out, error)) {
      return false;
    }
    if (value_as_code(out) == nullptr) {
      error = "bytecode file does not contain a code object";
      return false;
    }
    return true;
  }
  std::string source;
  if (!runtime.decode_python_source(
          std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), source, error)) {
    return false;
  }
  const Value* compile_builtin = runtime.find_builtin("compile");
  if (compile_builtin == nullptr) {
    error = "compile builtin is not registered";
    return false;
  }
  Value compile_args[3] = {
      Value::string(std::move(source)),
      Value::string(path),
      Value::string("exec"),
  };
  return runtime_call_callable(runtime, *compile_builtin, compile_args, 3, out, error);
}

bool importlib_loader_path_stats(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 2) {
    error = "loader.path_stats expected self and path";
    return false;
  }
  std::string path;
  if (!get_path_arg(runtime, args[1], "loader.path_stats path", path, error)) return false;
  Value path_value = Value::string(std::move(path));
  Value os_module;
  Value stat_function;
  Value stat_result;
  if (!runtime.import_module("os", os_module, error) ||
      !module_get_attr(os_module, "stat", stat_function, error) ||
      !runtime_call_callable(runtime, stat_function, &path_value, 1, stat_result, error)) {
    return false;
  }
  Value mtime;
  Value size;
  if (!attribute_get(stat_result, "st_mtime", mtime, error) ||
      !attribute_get(stat_result, "st_size", size, error)) {
    return false;
  }
  out = Value::dict({
      {Value::string("mtime"), std::move(mtime)},
      {Value::string("size"), std::move(size)},
  });
  return true;
}

bool importlib_loader_source_to_code(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 3 || argc > 4) {
    error = "loader.source_to_code expected data, path, and optional optimize";
    return false;
  }
  std::string source;
  if (auto* text = value_as_string(args[1])) {
    source = string_object_to_string(*text);
  } else if (auto* bytes = value_as_bytes(args[1])) {
    const std::string encoded = bytes_object_to_string(*bytes);
    if (!runtime.decode_python_source(encoded, source, error)) return false;
  } else {
    error = "source_to_code() argument 1 must be str or bytes";
    return false;
  }
  std::string path;
  if (!get_path_arg(runtime, args[2], "source_to_code() argument 2", path, error)) return false;
  const Value* compile_builtin = runtime.find_builtin("compile");
  if (compile_builtin == nullptr) {
    error = "compile builtin is not registered";
    return false;
  }
  Value compile_args[] = {Value::string(std::move(source)), Value::string(std::move(path)), Value::string("exec")};
  return runtime_call_callable(runtime, *compile_builtin, compile_args, 3, out, error);
}

bool importlib_loader_source_to_code_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (kwargc > 1 ||
      (kwargc == 1 &&
       (kwargs[0].name == nullptr || std::string_view(kwargs[0].name) != "_optimize"))) {
    error = "source_to_code() got an unexpected keyword argument";
    return false;
  }
  return importlib_loader_source_to_code(runtime, args, argc, out, error, user_data);
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
  Value fullname;
  if (!object_get_attr(args[0], "name", fullname, ignored)) {
    fullname = Value::string("");
  }
  Value get_code_args[] = {args[0], fullname};
  Value code_value;
  if (!importlib_loader_get_code(runtime, get_code_args, 2, code_value, error, nullptr)) {
    return false;
  }
  auto* code = value_as_code(code_value);
  if (code == nullptr || code->module == nullptr) {
    error = "loader.get_code() did not return a code object";
    return false;
  }
  std::string path;
  if (!get_string_arg(path_value, "loader path", path, error)) return false;
  Value module_value = args[1];
  module_set_attr(module_value, "__file__", Value::string(path), ignored);
  Interpreter interpreter(runtime);
  auto result = interpreter.run_module(*code->module, module_value, code->module);
  if (!result.errors.empty()) {
    error = result.errors.front();
    return false;
  }
  value_set_none(out);
  return true;
}

bool importlib_loader_load_module(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "loader.load_module expected self and fullname";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[1], "loader fullname", name, error)) {
    return false;
  }
  bool module_not_found = false;
  if (!runtime.import_module(name, out, error, &module_not_found)) {
    preserve_or_raise_import_error(runtime, module_not_found, error);
    return false;
  }
  return true;
}

Value make_source_file_loader(Runtime& runtime, const std::string& name, const Value& path) {
  Value loader_class;
  Value external;
  std::string ignored;
  if (!runtime.import_module("_frozen_importlib_external", external, ignored) ||
      !module_get_attr(external, "SourceFileLoader", loader_class, ignored) ||
      value_as_class(loader_class) == nullptr) {
    std::vector<std::pair<std::string, Value>> attrs;
    attrs.push_back({"__module__", Value::string("_frozen_importlib_external")});
    attrs.push_back({"__name__", Value::string("SourceFileLoader")});
    attrs.push_back({"__init__", runtime.make_native_function("SourceFileLoader.__init__", importlib_loader_init)});
    attrs.push_back({"create_module", runtime.make_native_function("SourceFileLoader.create_module", importlib_loader_create_module)});
    attrs.push_back({"exec_module", runtime.make_native_function("SourceFileLoader.exec_module", importlib_loader_exec_module)});
    attrs.push_back({"load_module", runtime.make_native_function("SourceFileLoader.load_module", importlib_loader_load_module)});
    attrs.push_back({"get_filename", runtime.make_native_function("SourceFileLoader.get_filename", importlib_loader_get_filename)});
    attrs.push_back({"get_data", runtime.make_native_function("SourceFileLoader.get_data", importlib_loader_get_data)});
    attrs.push_back({"get_code", runtime.make_native_function("SourceFileLoader.get_code", importlib_loader_get_code)});
    attrs.push_back({"path_stats", runtime.make_native_function("SourceFileLoader.path_stats", importlib_loader_path_stats)});
    attrs.push_back({"source_to_code", runtime.make_native_function(
        "SourceFileLoader.source_to_code", importlib_loader_source_to_code,
        nullptr, nullptr, nullptr, false, importlib_loader_source_to_code_kw)});
    loader_class = Value::class_object("SourceFileLoader", std::move(attrs));
  }
  Value loader = Value::instance(loader_class);
  object_set_attr(loader, "name", Value::string(name), ignored);
  object_set_attr(loader, "path", path, ignored);
  return loader;
}

void normalize_file_module_spec_loader(Runtime& runtime, const std::string& name, Value& module, Value& spec) {
  Value file;
  std::string ignored;
  if (!module_get_attr(module, "__file__", file, ignored) || file.tag == ValueTag::Invalid || file.tag == ValueTag::None) {
    return;
  }
  Value cached;
  if (module_get_attr(module, "__cached__", cached, ignored) &&
      cached.tag != ValueTag::Invalid && cached.tag != ValueTag::None) {
    object_set_attr(spec, "cached", cached, ignored);
  }
  Value loader;
  if (object_get_attr(spec, "loader", loader, ignored)) {
    Value get_code;
    if (object_get_attr(loader, "get_code", get_code, ignored)) {
      return;
    }
  }
  Value fixed_loader = make_source_file_loader(runtime, name, file);
  object_set_attr(spec, "loader", fixed_loader, ignored);
  module_set_attr(module, "__loader__", fixed_loader, ignored);
}

bool importlib_finder_find_spec(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "finder.find_spec expected fullname";
    return false;
  }
  std::string name;
  // Static finder methods receive fullname first, while a FileFinder instance
  // receives self first. PathFinder.find_spec(fullname, path) also has two
  // arguments, so argument count alone cannot distinguish these call shapes.
  const Value& name_arg = value_as_string(args[0]) != nullptr ? args[0] : args[1];
  if (!get_string_arg(name_arg, "finder fullname", name, error)) {
    return false;
  }
  if (argc >= 2) {
    Value finder_path;
    std::string ignored;
    if (object_get_attr(args[0], "path", finder_path, ignored)) {
      std::string root;
      if (!get_string_arg(finder_path, "finder path", root, error)) return false;
      const auto dot = name.rfind('.');
      const std::string leaf = dot == std::string::npos ? name : name.substr(dot + 1);
      const auto base = std::filesystem::path(root) / leaf;
      VfsStat stat;
      std::string stat_error;
      auto source = base;
      source += ".py";
      if (runtime.vfs().stat(source.string(), stat, stat_error) && stat.kind == VfsNodeKind::File) {
        Value loader = make_source_file_loader(runtime, name, Value::string(source.string()));
        out = make_module_spec_for_file(name, source.string(), loader);
        return true;
      }
      const auto init = base / "__init__.py";
      if (runtime.vfs().stat(init.string(), stat, stat_error) && stat.kind == VfsNodeKind::File) {
        Value loader = make_source_file_loader(runtime, name, Value::string(init.string()));
        out = make_module_spec_for_file(name, init.string(), loader, true, base.string());
        return true;
      }
      if (runtime.vfs().stat(base.string(), stat, stat_error) && stat.kind == VfsNodeKind::Directory) {
        out = make_module_spec_for_file(name, base.string(), Value::none(), true, base.string());
        object_set_attr(out, "origin", Value::none(), ignored);
        object_set_attr(out, "cached", Value::none(), ignored);
        object_set_attr(out, "has_location", Value::boolean(false), ignored);
        return true;
      }
      value_set_none(out);
      return true;
    }
  }
  const bool has_reload_target = value_as_string(args[0]) != nullptr &&
      argc >= 3 && args[2].tag != ValueTag::None && args[2].tag != ValueTag::Invalid;
  return find_module_spec_without_import(runtime, name, out, !has_reload_target);
}

bool importlib_builtin_create_module(
    Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "builtin loader create_module expected spec";
    return false;
  }
  value_set_none(out);
  return true;
}

bool importlib_builtin_exec_module(
    Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "builtin loader exec_module expected module";
    return false;
  }
  value_set_none(out);
  return true;
}

bool importlib_path_finder_find_distributions(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "PathFinder.find_distributions() expected optional context";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value metadata_module;
  if (!runtime.import_module("importlib.metadata", metadata_module, error)) {
    return false;
  }
  Value metadata_path_finder;
  if (!module_get_attr(metadata_module, "MetadataPathFinder", metadata_path_finder, error)) {
    return false;
  }
  Value find_distributions;
  if (!object_get_attr(metadata_path_finder, "find_distributions", find_distributions, error)) {
    return false;
  }
  if (argc > 0) {
    return runtime_call_callable(runtime, find_distributions, &args[argc - 1], 1, out, error);
  }
  return runtime_call_callable(runtime, find_distributions, nullptr, 0, out, error);
}

bool importlib_path_finder_invalidate_caches(
    Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "PathFinder.invalidate_caches() expected no arguments";
    return false;
  }
  runtime.clear_python_import_misses();
  Value sys;
  Value cache_value;
  if (!runtime.import_module("sys", sys, error) ||
      !module_get_attr(sys, "path_importer_cache", cache_value, error)) {
    return false;
  }
  if (auto* cache = value_as_dict(cache_value)) {
    for (const auto& entry : cache->entries) {
      const Value& finder = entry.second;
      if (finder.tag == ValueTag::None || finder.tag == ValueTag::Invalid) continue;
      Value invalidate;
      std::string ignored;
      if (!object_get_attr(finder, "invalidate_caches", invalidate, ignored)) continue;
      Value ignored_result;
      if (!runtime_call_callable(runtime, invalidate, nullptr, 0, ignored_result, error)) return false;
    }
  }
  value_set_none(out);
  return true;
}

bool importlib_namespace_resource_reader_files(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "NamespaceResourceReader.files expected self";
    return false;
  }
  return object_get_attr(args[0], "__files", out, error);
}

bool importlib_namespace_loader_get_resource_reader(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "loader.get_resource_reader expected self and fullname";
    return false;
  }
  auto* fullname_string = value_as_string(args[1]);
  if (fullname_string == nullptr) {
    error = "loader.get_resource_reader fullname must be str";
    return false;
  }
  Value package;
  if (!runtime.import_module(string_object_to_string(*fullname_string), package, error)) {
    return false;
  }
  Value package_path;
  if (!module_get_attr(package, "__path__", package_path, error)) {
    return false;
  }
  Value readers_module;
  if (!runtime.import_module("importlib.resources.readers", readers_module, error)) {
    return false;
  }
  Value multiplexed_path_class;
  if (!module_get_attr(readers_module, "MultiplexedPath", multiplexed_path_class, error)) {
    return false;
  }
  auto* paths = value_as_list(package_path);
  if (paths == nullptr) {
    error = "namespace package __path__ must be iterable";
    return false;
  }
  std::vector<Value> valid_paths;
  valid_paths.reserve(paths->items.size());
  for (const auto& path : paths->items) {
    auto* text = value_as_string(path);
    if (text == nullptr) continue;
    const std::string candidate = string_object_to_string(*text);
    VfsStat stat;
    std::string stat_error;
    std::string lowered = candidate;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    if ((runtime.vfs().stat(candidate, stat, stat_error) && stat.kind == VfsNodeKind::Directory) ||
        lowered.find(".zip") != std::string::npos) {
      valid_paths.push_back(path);
    }
  }
  Value files;
  if (!runtime_call_callable(runtime, multiplexed_path_class, valid_paths.data(), static_cast<uint32_t>(valid_paths.size()), files, error)) {
    return false;
  }
  out = Value::instance(Value::class_object(
      "NamespaceResourceReader",
      {{"files", runtime.make_native_function("NamespaceResourceReader.files", importlib_namespace_resource_reader_files)}}));
  std::string ignored;
  object_set_attr(out, "__files", files, ignored);
  return true;
}

bool importlib_file_loader_get_resource_reader(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "loader.get_resource_reader expected self and fullname";
    return false;
  }
  Value readers;
  Value file_reader;
  if (!runtime.import_module("importlib.resources.readers", readers, error) ||
      !module_get_attr(readers, "FileReader", file_reader, error)) {
    return false;
  }
  return runtime_call_callable(runtime, file_reader, args, 1, out, error);
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
  attrs.push_back({"load_module", runtime.make_native_function(name + ".load_module", importlib_loader_load_module)});
  attrs.push_back({"get_filename", runtime.make_native_function(name + ".get_filename", importlib_loader_get_filename)});
  attrs.push_back({"get_data", runtime.make_native_function(name + ".get_data", importlib_loader_get_data)});
  attrs.push_back({"get_code", runtime.make_native_function(name + ".get_code", importlib_loader_get_code)});
  if (name == "SourceFileLoader") {
    attrs.push_back({"path_stats", runtime.make_native_function(name + ".path_stats", importlib_loader_path_stats)});
    attrs.push_back({"get_resource_reader", runtime.make_native_function(name + ".get_resource_reader", importlib_file_loader_get_resource_reader)});
  }
  attrs.push_back({"source_to_code", runtime.make_native_function(
      name + ".source_to_code", importlib_loader_source_to_code,
      nullptr, nullptr, nullptr, false, importlib_loader_source_to_code_kw)});
  if (name == "NamespaceLoader") {
    attrs.push_back({"get_resource_reader", runtime.make_native_function(name + ".get_resource_reader", importlib_namespace_loader_get_resource_reader)});
  }
  return make_simple_class(name, std::move(attrs));
}

Value make_finder_class(Runtime& runtime, const std::string& name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"find_spec", runtime.make_native_function(name + ".find_spec", importlib_finder_find_spec)});
  if (name == "BuiltinImporter" || name == "FrozenImporter") {
    attrs.push_back({"create_module", runtime.make_native_function(name + ".create_module", importlib_builtin_create_module)});
    attrs.push_back({"exec_module", runtime.make_native_function(name + ".exec_module", importlib_builtin_exec_module)});
    attrs.push_back({"get_code", runtime.make_native_function(name + ".get_code", importlib_finder_get_optional_code)});
    attrs.push_back({"get_source", runtime.make_native_function(name + ".get_source", importlib_finder_get_optional_code)});
    attrs.push_back({"is_package", runtime.make_native_function(name + ".is_package", importlib_finder_is_package)});
  }
  if (name == "PathFinder") {
    attrs.push_back({"find_distributions", runtime.make_native_function(name + ".find_distributions", importlib_path_finder_find_distributions)});
    attrs.push_back({"invalidate_caches", runtime.make_native_function(name + ".invalidate_caches", importlib_path_finder_invalidate_caches)});
  }
  return make_simple_class(name, std::move(attrs));
}

void importlib_value_cleanup(void* data) {
  delete static_cast<Value*>(data);
}

bool importlib_file_finder_path_hook(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "FileFinder path hook expected one string path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto* file_finder_class = static_cast<const Value*>(user_data);
  if (file_finder_class == nullptr || value_as_class(*file_finder_class) == nullptr) {
    error = "FileFinder path hook has no finder class";
    return false;
  }
  const std::string path = string_object_to_string(*value_as_string(args[0]));
  VfsStat stat;
  std::string stat_error;
  if (!runtime.vfs().stat(path, stat, stat_error) || stat.kind != VfsNodeKind::Directory) {
    error = "only directories are supported by FileFinder";
    runtime.raise_class_error("ImportError", error);
    return false;
  }
  out = Value::instance(*file_finder_class);
  return object_set_attr(out, "path", args[0], error);
}

bool bootstrap_resolve_name(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*);

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
  bool module_not_found = false;
  if (!runtime.import_module(name, out, error, &module_not_found)) {
    preserve_or_raise_import_error(runtime, module_not_found, error);
    return false;
  }
  return true;
}

bool bootstrap_gcd_import(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "importlib._bootstrap._gcd_import() expected name, package, level";
    return false;
  }

  std::string name;
  if (!get_string_arg(args[0], "_gcd_import name", name, error)) {
    return false;
  }

  int64_t level = 0;
  if (argc >= 3 && args[2].tag != ValueTag::None) {
    if (args[2].tag != ValueTag::Int64) {
      error = "_gcd_import level must be int";
      return false;
    }
    level = args[2].as.i64;
  }

  if (level > 0) {
    if (argc < 2 || args[1].tag == ValueTag::None) {
      error = "relative import requires package";
      runtime.raise_class_error("ImportError", error);
      return false;
    }
    std::string package;
    if (!get_string_arg(args[1], "_gcd_import package", package, error)) {
      return false;
    }
    Value resolved;
    Value resolve_args[3] = {Value::string(name), Value::string(package), Value::int64(level)};
    if (!bootstrap_resolve_name(runtime, resolve_args, 3, resolved, error, nullptr)) {
      return false;
    }
    auto* resolved_text = value_as_string(resolved);
    if (resolved_text == nullptr) {
      error = "_gcd_import resolved name is not str";
      return false;
    }
    name = string_object_to_string(*resolved_text);
  }

  bool module_not_found = false;
  if (!runtime.import_module(name, out, error, &module_not_found)) {
    preserve_or_raise_import_error(runtime, module_not_found, error);
    return false;
  }
  return true;
}

bool importlib_invalidate_caches(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "importlib.invalidate_caches() expected no arguments";
    return false;
  }
  runtime.clear_python_import_misses();
  Value sys;
  if (!runtime.import_module("sys", sys, error)) return false;
  auto invalidate_one = [&](const Value& finder) -> bool {
    if (finder.tag == ValueTag::None || finder.tag == ValueTag::Invalid) return true;
    Value invalidate;
    std::string ignored;
    if (!object_get_attr(finder, "invalidate_caches", invalidate, ignored)) return true;
    Value ignored_result;
    return runtime_call_callable(runtime, invalidate, nullptr, 0, ignored_result, error);
  };
  Value cache_value;
  if (module_get_attr(sys, "path_importer_cache", cache_value, error)) {
    if (auto* cache = value_as_dict(cache_value)) {
      for (const auto& entry : cache->entries) {
        if (!invalidate_one(entry.second)) return false;
      }
    }
  }
  Value meta_path;
  if (module_get_attr(sys, "meta_path", meta_path, error)) {
    if (auto* finders = value_as_list(meta_path)) {
      for (const auto& finder : finders->items) {
        if (!invalidate_one(finder)) return false;
      }
    }
  }
  value_set_none(out);
  return true;
}

bool importlib_exec(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "importlib._bootstrap._exec() expected spec and module";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value loader;
  if (!object_get_attr(args[0], "loader", loader, error)) {
    return false;
  }
  Value spec_value = args[0];
  Value module_value = args[1];
  std::string ignored;
  module_set_attr(module_value, "__spec__", spec_value, ignored);
  module_set_attr(module_value, "__loader__", loader, ignored);
  Value metadata;
  if (object_get_attr(spec_value, "name", metadata, ignored))
    module_set_attr(module_value, "__name__", metadata, ignored);
  if (object_get_attr(spec_value, "parent", metadata, ignored))
    module_set_attr(module_value, "__package__", metadata, ignored);
  Value has_location;
  if (object_get_attr(spec_value, "has_location", has_location, ignored) && value_truthy(has_location) &&
      object_get_attr(spec_value, "origin", metadata, ignored))
    module_set_attr(module_value, "__file__", metadata, ignored);
  if (object_get_attr(spec_value, "cached", metadata, ignored) && metadata.tag != ValueTag::None)
    module_set_attr(module_value, "__cached__", metadata, ignored);
  if (object_get_attr(spec_value, "submodule_search_locations", metadata, ignored) && metadata.tag != ValueTag::None)
    module_set_attr(module_value, "__path__", metadata, ignored);
  // Namespace packages have no source body.  Reload still refreshes their
  // spec and search locations, then completes without invoking a loader.
  if (loader.tag == ValueTag::None || loader.tag == ValueTag::Invalid) {
    value_set_none(out);
    return true;
  }
  Value exec_module;
  if (!object_get_attr(loader, "exec_module", exec_module, error)) {
    return false;
  }
  Value ignored_result;
  if (!runtime_call_callable(runtime, exec_module, &args[1], 1, ignored_result, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool module_spec_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1 || argc > 3) {
    error = "ModuleSpec.__init__() takes 1 to 3 positional arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value name_value = argc >= 2 ? args[1] : Value::invalid();
  Value loader = argc == 3 ? args[2] : Value::invalid();
  Value origin = Value::none();
  Value loader_state = Value::none();
  Value is_package = Value::none();
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (name == "name") {
      if (name_value.tag != ValueTag::Invalid) {
        error = "ModuleSpec.__init__() got multiple values for argument 'name'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(name_value, *kwargs[i].value);
    } else if (name == "loader") {
      if (loader.tag != ValueTag::Invalid) {
        error = "ModuleSpec.__init__() got multiple values for argument 'loader'";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(loader, *kwargs[i].value);
    } else if (name == "origin") {
      value_assign_fast(origin, *kwargs[i].value);
    } else if (name == "loader_state") {
      value_assign_fast(loader_state, *kwargs[i].value);
    } else if (name == "is_package") {
      value_assign_fast(is_package, *kwargs[i].value);
    } else {
      error = "ModuleSpec.__init__() got an unexpected keyword argument '" +
          std::string(name) + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (name_value.tag == ValueTag::Invalid) {
    error = "ModuleSpec.__init__() missing required argument 'name'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (loader.tag == ValueTag::Invalid) {
    error = "ModuleSpec.__init__() missing required argument 'loader'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value positional[5] = {args[0], name_value, loader, origin, is_package};
  if (!module_spec_init(runtime, positional, 5, out, error, user_data)) {
    return false;
  }
  std::string ignored;
  object_set_attr(const_cast<Value&>(args[0]), "loader_state", loader_state, ignored);
  return true;
}

bool importlib_find_spec(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "importlib._find_spec() expected name, optional path, and optional target";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "importlib.util.find_spec name", name, error)) {
    return false;
  }
  Value sys;
  Value meta_path;
  std::string ignored;
  if (runtime.import_module("sys", sys, ignored) &&
      module_get_attr(sys, "meta_path", meta_path, ignored)) {
    if (auto* finders = value_as_list(meta_path)) {
      for (const auto& finder : finders->items) {
        Value finder_find_spec;
        if (!object_get_attr(finder, "find_spec", finder_find_spec, ignored)) continue;
        Value finder_args[] = {
            args[0],
            argc >= 2 ? args[1] : Value::none(),
            argc >= 3 ? args[2] : Value::none(),
        };
        if (!runtime_call_callable(runtime, finder_find_spec, finder_args, 3, out, error)) return false;
        if (out.tag != ValueTag::None && out.tag != ValueTag::Invalid) return true;
      }
    }
  }
  return find_module_spec_without_import(runtime, name, out);
}

bool importlib_finder_get_optional_code(
    Runtime&,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    error = "loader method expected fullname";
    return false;
  }
  value_set_none(out);
  return true;
}

bool importlib_finder_is_package(
    Runtime&,
    const Value*,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 2) {
    error = "loader.is_package expected fullname";
    return false;
  }
  out = Value::boolean(false);
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

bool importlib_spec_from_file_location_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  Value loader = Value::none();
  uint32_t positional_count = argc;
  if (argc > 3) {
    error = "importlib.util.spec_from_file_location() takes from 1 to 2 positional arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (argc == 3) {
    value_assign_fast(loader, args[2]);
    positional_count = 2;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (name == "loader") {
      value_assign_fast(loader, *kwargs[i].value);
      continue;
    }
    if (name == "submodule_search_locations" || name == "_set_fileattr") {
      continue;
    }
    error = "importlib.util.spec_from_file_location() got an unexpected keyword argument '" + std::string(name) + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (positional_count < 2) {
    error = "importlib.util.spec_from_file_location() missing required argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value local_args[3] = {args[0], args[1], loader};
  return importlib_spec_from_file_location(runtime, local_args, 3, out, error, user_data);
}

bool importlib_module_from_spec(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
  Value loader;
  Value create_module;
  if (object_get_attr(args[0], "loader", loader, ignored) &&
      object_get_attr(loader, "create_module", create_module, ignored)) {
    if (!runtime_call_callable(runtime, create_module, &args[0], 1, out, error)) {
      return false;
    }
  }
  if (out.tag == ValueTag::Invalid || out.tag == ValueTag::None) {
    out = Value::module(name);
  }
  module_set_attr(out, "__name__", Value::string(name), ignored);
  module_set_attr(out, "__spec__", args[0], ignored);
  module_set_attr(out, "__loader__", loader, ignored);
  Value origin;
  if (object_get_attr(args[0], "origin", origin, ignored)) {
    module_set_attr(out, "__file__", origin, ignored);
  }
  Value parent;
  if (object_get_attr(args[0], "parent", parent, ignored)) {
    module_set_attr(out, "__package__", parent, ignored);
  }
  Value locations;
  if (object_get_attr(args[0], "submodule_search_locations", locations, ignored) &&
      locations.tag != ValueTag::None && locations.tag != ValueTag::Invalid) {
    module_set_attr(out, "__path__", locations, ignored);
  }
  return true;
}

bool bootstrap_load(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "importlib._bootstrap._load() expected spec";
    return false;
  }
  Value name_value;
  std::string name;
  if (!object_get_attr(args[0], "name", name_value, error) ||
      !get_string_arg(name_value, "spec name", name, error)) {
    return false;
  }
  if (!importlib_module_from_spec(runtime, args, 1, out, error, nullptr)) {
    return false;
  }
  runtime.register_module(name, out);
  Value loader;
  Value exec_module;
  if (!object_get_attr(args[0], "loader", loader, error) ||
      !object_get_attr(loader, "exec_module", exec_module, error)) {
    runtime.unregister_module(name);
    return false;
  }
  Value ignored_result;
  if (!runtime_call_callable(runtime, exec_module, &out, 1, ignored_result, error)) {
    runtime.unregister_module(name);
    return false;
  }
  return true;
}

bool bootstrap_resolve_name(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "importlib._bootstrap._resolve_name() expected name, package, level";
    return false;
  }
  std::string name;
  std::string package;
  int64_t level = 0;
  if (!get_string_arg(args[0], "_resolve_name name", name, error) ||
      !get_string_arg(args[1], "_resolve_name package", package, error)) {
    return false;
  }
  if (args[2].tag != ValueTag::Int64) {
    error = "_resolve_name level must be int";
    return false;
  }
  level = args[2].as.i64;
  for (int64_t i = 1; i < level && !package.empty(); ++i) {
    const auto dot = package.rfind('.');
    package = dot == std::string::npos ? std::string() : package.substr(0, dot);
  }
  if (!name.empty()) {
    if (!package.empty()) {
      package += ".";
    }
    package += name;
  }
  out = Value::string(package);
  return true;
}

bool bootstrap_spec_from_loader(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "spec_from_loader() expected name and loader";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "spec_from_loader name", name, error)) {
    return false;
  }
  out = make_module_spec_for_file(name, "", args[1]);
  return true;
}

bool bootstrap_external_cache_from_source(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "cache_from_source() expected path";
    return false;
  }
  auto* path_string = value_as_string(args[0]);
  if (path_string == nullptr) {
    error = "cache_from_source() path must be str";
    return false;
  }
  const std::filesystem::path path(string_object_to_string(*path_string));
  const std::string stem = path.stem().string();
  if (stem.empty()) {
    error = "cache_from_source() path has no filename";
    return false;
  }
  const auto cached = path.parent_path() / "__pycache__" / (stem + ".xlang3-314.pyc");
  out = Value::string(cached.string());
  return true;
}

bool bootstrap_external_source_from_cache(
    Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "source_from_cache() expected path";
    return false;
  }
  auto* path_string = value_as_string(args[0]);
  if (path_string == nullptr) {
    error = "source_from_cache() path must be str";
    return false;
  }
  const std::filesystem::path path(string_object_to_string(*path_string));
  if (path.parent_path().filename() != "__pycache__") {
    error = "__pycache__ not bottom-level directory in bytecode path";
    return false;
  }
  const std::string filename = path.filename().string();
  const size_t dot = filename.find('.');
  if (dot == std::string::npos || path.extension() != ".pyc") {
    error = "invalid bytecode cache path";
    return false;
  }
  out = Value::string((path.parent_path().parent_path() / (filename.substr(0, dot) + ".py")).string());
  return true;
}

bool bootstrap_external_cache_from_source_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (std::string_view(kwargs[i].name) != "debug_override" &&
        std::string_view(kwargs[i].name) != "optimization") {
      error = std::string("cache_from_source() got an unexpected keyword argument '") + kwargs[i].name + "'";
      return false;
    }
  }
  return bootstrap_external_cache_from_source(runtime, args, argc, out, error, user_data);
}

bool bootstrap_external_decode_source(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "decode_source() expected source bytes";
    return false;
  }
  if (auto* bytes = value_as_bytes(args[0])) {
    const auto view = bytes_object_view(*bytes);
    out = Value::string(std::string(view.data(), view.size()));
    return true;
  }
  value_assign_fast(out, args[0]);
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

bool bootstrap_external_pack_uint32(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "_pack_uint32() expected an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const uint32_t value = static_cast<uint32_t>(args[0].as.i64);
  std::string bytes;
  bytes.push_back(static_cast<char>(value & 0xff));
  bytes.push_back(static_cast<char>((value >> 8) & 0xff));
  bytes.push_back(static_cast<char>((value >> 16) & 0xff));
  bytes.push_back(static_cast<char>((value >> 24) & 0xff));
  out = Value::bytes(std::move(bytes));
  return true;
}

bool bootstrap_external_unpack_uint32(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_unpack_uint32() expected bytes";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* bytes = value_as_bytes(args[0]);
  if (bytes == nullptr) {
    error = "_unpack_uint32() expected bytes";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto view = bytes_object_view(*bytes);
  if (view.size() != 4) {
    error = "_unpack_uint32() expected 4 bytes";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  const uint32_t value = static_cast<unsigned char>(view[0]) |
                         (static_cast<uint32_t>(static_cast<unsigned char>(view[1])) << 8) |
                         (static_cast<uint32_t>(static_cast<unsigned char>(view[2])) << 16) |
                         (static_cast<uint32_t>(static_cast<unsigned char>(view[3])) << 24);
  out = Value::int64(value);
  return true;
}

void append_uint32_le(std::string& data, uint32_t value) {
  data.push_back(static_cast<char>(value & 0xff));
  data.push_back(static_cast<char>((value >> 8) & 0xff));
  data.push_back(static_cast<char>((value >> 16) & 0xff));
  data.push_back(static_cast<char>((value >> 24) & 0xff));
}

bool append_marshaled_code(
    Runtime& runtime, const Value& code, std::string& data, Value& out, std::string& error) {
  Value marshal_module;
  Value dumps;
  Value marshaled;
  if (!runtime.import_module("marshal", marshal_module, error) ||
      !module_get_attr(marshal_module, "dumps", dumps, error) ||
      !runtime_call_callable(runtime, dumps, &code, 1, marshaled, error)) {
    return false;
  }
  auto* bytes = value_as_bytes(marshaled);
  if (bytes == nullptr) {
    error = "marshal.dumps() did not return bytes";
    return false;
  }
  data.append(bytes_object_view(*bytes));
  out = Value::bytes(std::move(data));
  return true;
}

bool bootstrap_external_code_to_timestamp_pyc(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 3) {
    error = "_code_to_timestamp_pyc() expected code, mtime, and source_size";
    return false;
  }
  const uint32_t mtime = argc >= 2 && args[1].tag == ValueTag::Int64
      ? static_cast<uint32_t>(args[1].as.i64) : 0;
  const uint32_t size = argc >= 3 && args[2].tag == ValueTag::Int64
      ? static_cast<uint32_t>(args[2].as.i64) : 0;
  std::string data("\x33\x58\x0d\x0a", 4);
  append_uint32_le(data, 0);
  append_uint32_le(data, mtime);
  append_uint32_le(data, size);
  return append_marshaled_code(runtime, args[0], data, out, error);
}

bool bootstrap_external_code_to_hash_pyc(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3) {
    error = "_code_to_hash_pyc() expected code, source_hash, and checked";
    return false;
  }
  auto* hash = value_as_bytes(args[1]);
  if (hash == nullptr || bytes_object_view(*hash).size() != 8) {
    error = "source_hash must be an 8-byte bytes object";
    return false;
  }
  const bool checked = argc < 3 || value_truthy(args[2]);
  std::string data("\x33\x58\x0d\x0a", 4);
  append_uint32_le(data, checked ? 3u : 1u);
  data.append(bytes_object_view(*hash));
  return append_marshaled_code(runtime, args[0], data, out, error);
}

bool bootstrap_external_write_atomic(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 3) {
    error = "_write_atomic() expected path, data, and optional mode";
    return false;
  }
  auto* data = value_as_bytes(args[1]);
  std::string path;
  if (data == nullptr || !get_path_arg(runtime, args[0], "_write_atomic() path", path, error)) {
    if (error.empty()) error = "_write_atomic() expected a path and bytes data";
    return false;
  }
  const auto view = bytes_object_view(*data);
  if (!runtime.vfs().write_file(
          path,
          reinterpret_cast<const uint8_t*>(view.data()),
          view.size(),
          error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool bootstrap_external_calc_mode(
    Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_calc_mode() expected path";
    return false;
  }
  out = Value::int64(0666);
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
  Value windows_registry_finder = make_finder_class(runtime, "WindowsRegistryFinder");
  Value builtin_importer = make_finder_class(runtime, "BuiltinImporter");
  Value frozen_importer = make_finder_class(runtime, "FrozenImporter");
  Value apple_framework_loader = make_loader_class(runtime, "AppleFrameworkLoader");
  Value namespace_loader = make_loader_class(runtime, "NamespaceLoader");
  Value loader_basics = make_simple_class(
      "_LoaderBasics",
      {{"exec_module", runtime.make_native_function("_LoaderBasics.exec_module", importlib_loader_exec_module)},
       {"load_module", runtime.make_native_function("_LoaderBasics.load_module", importlib_loader_load_module)}});

  std::string ignored;
  object_set_attr(builtin_importer, "__module__", Value::string("_frozen_importlib"), ignored);
  object_set_attr(frozen_importer, "__module__", Value::string("_frozen_importlib"), ignored);
  object_set_attr(path_finder, "__module__", Value::string("_frozen_importlib_external"), ignored);
  object_set_attr(file_finder, "__module__", Value::string("_frozen_importlib_external"), ignored);
  object_set_attr(windows_registry_finder, "__module__", Value::string("_frozen_importlib_external"), ignored);
  object_set_attr(apple_framework_loader, "__module__", Value::string("_frozen_importlib_external"), ignored);
  object_set_attr(loader_basics, "__module__", Value::string("_frozen_importlib_external"), ignored);

  Value module_spec_class = make_simple_class(
      "ModuleSpec",
      {{"__init__", runtime.make_native_function(
          "ModuleSpec.__init__",
          module_spec_init,
          nullptr,
          nullptr,
          nullptr,
          false,
          module_spec_init_kw)}});

  NativeModuleBuilder frozen_builder(runtime, "_frozen_importlib");
  Value bootstrap_import;
  if (const auto* import_builtin = runtime.find_builtin("__import__")) {
    value_assign_fast(bootstrap_import, *import_builtin);
  } else {
    value_set_none(bootstrap_import);
  }
  frozen_builder.value("__name__", Value::string("_frozen_importlib"))
      .value("BuiltinImporter", builtin_importer)
      .value("FrozenImporter", frozen_importer)
      .value("ModuleSpec", module_spec_class)
      .value("__import__", bootstrap_import)
      .function("module_from_spec", importlib_module_from_spec)
      .function("_gcd_import", bootstrap_gcd_import)
      .function("_resolve_name", bootstrap_resolve_name)
      .function("spec_from_loader", bootstrap_spec_from_loader)
      .function("_exec", importlib_exec)
      .function("_load", bootstrap_load)
      .function("_find_spec", importlib_find_spec);
  Value frozen = frozen_builder.finish();
  runtime.register_module("_frozen_importlib", frozen);
  runtime.register_module("importlib._bootstrap", frozen);

  NativeModuleBuilder external_builder(runtime, "_frozen_importlib_external");
  external_builder.value("__name__", Value::string("_frozen_importlib_external"))
      .value("FileFinder", file_finder)
      .value("WindowsRegistryFinder", windows_registry_finder)
      .value("PathFinder", path_finder)
      .value("FileLoader", file_loader)
      .value("SourceLoader", source_loader)
      .value("SourceFileLoader", source_file_loader)
      .value("SourcelessFileLoader", sourceless_file_loader)
      .value("ExtensionFileLoader", extension_file_loader)
      .value("AppleFrameworkLoader", apple_framework_loader)
      .value("NamespaceLoader", namespace_loader)
      .value("_LoaderBasics", loader_basics)
      .value("SOURCE_SUFFIXES", Value::list({Value::string(".py")}))
      .value("BYTECODE_SUFFIXES", Value::list({Value::string(".pyc")}))
      .value("DEBUG_BYTECODE_SUFFIXES", Value::list({Value::string(".pyc")}))
      .value("OPTIMIZED_BYTECODE_SUFFIXES", Value::list({Value::string(".pyc")}))
      .value("EXTENSION_SUFFIXES", Value::list({}))
      .value("MAGIC_NUMBER", Value::bytes(std::string("\x33\x58\x0d\x0a", 4)))
      .function("cache_from_source", bootstrap_external_cache_from_source, nullptr, false, bootstrap_external_cache_from_source_kw)
      .function("source_from_cache", bootstrap_external_source_from_cache)
      .function("decode_source", bootstrap_external_decode_source)
      .function("spec_from_file_location", importlib_spec_from_file_location, nullptr, false, importlib_spec_from_file_location_kw)
      .function("_pack_uint32", bootstrap_external_pack_uint32)
      .function("_unpack_uint32", bootstrap_external_unpack_uint32)
      .function("_code_to_timestamp_pyc", bootstrap_external_code_to_timestamp_pyc)
      .function("_code_to_hash_pyc", bootstrap_external_code_to_hash_pyc)
      .function("_write_atomic", bootstrap_external_write_atomic)
      .function("_calc_mode", bootstrap_external_calc_mode);
  Value external = external_builder.finish();
  runtime.register_module("_frozen_importlib_external", external);
  runtime.register_module("importlib._bootstrap_external", external);
  canonicalize_existing_builtin_import_loaders(runtime, builtin_importer, frozen_importer);

  Value sys;
  std::string error;
  if (runtime.import_module("sys", sys, error)) {
    Value file_finder_path_hook = runtime.make_native_function(
        "FileFinder.path_hook",
        importlib_file_finder_path_hook,
        new Value(file_finder),
        importlib_value_cleanup);
    module_set_attr(sys,
                    "meta_path",
                    Value::list({builtin_importer, frozen_importer, path_finder}),
                    ignored);
    module_set_attr(sys, "path_hooks", Value::list({std::move(file_finder_path_hook)}), ignored);
  }
}

} // namespace xlang3
