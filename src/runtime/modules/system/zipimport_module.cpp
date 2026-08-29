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
#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"
#include "xlang3/vfs.h"

#include "zip_archive.h"

#include <memory>

namespace xlang3 {

namespace {

bool zip_get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

std::string zip_module_base(const std::string& fullname) {
  std::string base = fullname;
  for (char& ch : base) {
    if (ch == '.') {
      ch = '/';
    }
  }
  return base;
}

std::string zip_origin_path(const std::string& archive, const std::string& member) {
#if defined(_WIN32)
  constexpr char separator = '\\';
#else
  constexpr char separator = '/';
#endif
  std::string origin = archive;
  if (!origin.empty() && origin.back() != '/' && origin.back() != '\\') {
    origin.push_back(separator);
  }
  for (char ch : member) {
    origin.push_back(ch == '/' ? separator : ch);
  }
  return origin;
}

bool zipimporter_archive(const Value& self, std::string& archive, std::string& error) {
  Value archive_value;
  std::string ignored;
  if (!object_get_attr(self, "archive", archive_value, ignored) ||
      !zip_get_string_arg(archive_value, "archive", archive, error)) {
    error = "zipimporter has no archive";
    return false;
  }
  return true;
}

Value make_zip_module_spec(
    const std::string& fullname,
    const Value& loader,
    const std::string& archive,
    const std::string& member,
    bool is_package) {
  Value klass = Value::class_object("ModuleSpec", {{"__module__", Value::string("importlib")}});
  Value spec = Value::instance(klass);
  std::string ignored;
  const std::string origin = zip_origin_path(archive, member);
  object_set_attr(spec, "name", Value::string(fullname), ignored);
  object_set_attr(spec, "loader", loader, ignored);
  object_set_attr(spec, "origin", Value::string(origin), ignored);
  object_set_attr(spec, "cached", Value::none(), ignored);
  const auto dot = fullname.rfind('.');
  object_set_attr(spec, "parent", Value::string(dot == std::string::npos ? "" : fullname.substr(0, dot)), ignored);
  object_set_attr(spec, "has_location", Value::boolean(true), ignored);
  if (is_package) {
    object_set_attr(spec, "submodule_search_locations", Value::list({Value::string(zip_origin_path(archive, zip_module_base(fullname)))}), ignored);
  } else {
    object_set_attr(spec, "submodule_search_locations", Value::none(), ignored);
  }
  return spec;
}

bool zipimporter_find_member(
    Runtime& runtime,
    const Value& self,
    const std::string& fullname,
    std::string& archive,
    std::string& member,
    bool& is_package,
    bool& archive_valid,
    std::string& error) {
  archive_valid = false;
  if (!zipimporter_archive(self, archive, error)) {
    return false;
  }
  std::vector<uint8_t> archive_bytes;
  if (!runtime.vfs().read_file(archive, archive_bytes, error)) {
    return false;
  }
  std::vector<ZipArchiveEntry> entries;
  if (!zip_archive_list_entries(archive_bytes, entries, error)) {
    error.clear();
    return false;
  }
  archive_valid = true;
  const std::string base = zip_module_base(fullname);
  member = base + ".py";
  for (const auto& entry : entries) {
    if (entry.name == member) {
      is_package = false;
      return true;
    }
  }
  const std::string package_member = base + "/__init__.py";
  for (const auto& entry : entries) {
    if (entry.name == package_member) {
      member = package_member;
      is_package = true;
      return true;
    }
  }
  return false;
}

bool zipimporter_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.__init__ expected archive path";
    return false;
  }
  std::string archive;
  if (!zip_get_string_arg(args[1], "archive path", archive, error)) {
    return false;
  }
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "archive", Value::string(archive), ignored);
  object_set_attr(self, "prefix", Value::string(""), ignored);
  value_set_none(out);
  return true;
}

bool zipimporter_find_spec(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "zipimporter.find_spec expected fullname, optional target";
    return false;
  }
  std::string fullname;
  if (!zip_get_string_arg(args[1], "fullname", fullname, error)) {
    return false;
  }
  std::string archive;
  std::string member;
  bool is_package = false;
  bool archive_valid = false;
  if (!zipimporter_find_member(runtime, args[0], fullname, archive, member, is_package, archive_valid, error)) {
    value_set_none(out);
    return true;
  }
  out = make_zip_module_spec(fullname, args[0], archive, member, is_package);
  return true;
}

bool zipimporter_find_module(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "zipimporter.find_module expected fullname and optional path";
    return false;
  }
  value_set_none(out);
  return true;
}

bool zipimporter_get_filename(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.get_filename expected fullname";
    return false;
  }
  std::string fullname;
  if (!zip_get_string_arg(args[1], "fullname", fullname, error)) {
    return false;
  }
  std::string archive;
  std::string member;
  bool is_package = false;
  bool archive_valid = false;
  if (!zipimporter_find_member(runtime, args[0], fullname, archive, member, is_package, archive_valid, error)) {
    if (!archive_valid) {
      out = Value::string(archive + "/" + fullname + ".py");
      return true;
    }
    error = "can't find module '" + fullname + "'";
    return false;
  }
  out = Value::string(zip_origin_path(archive, member));
  return true;
}

bool zipimporter_get_data(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.get_data expected path";
    return false;
  }
  std::string path;
  if (!zip_get_string_arg(args[1], "path", path, error)) {
    return false;
  }
  Value archive_value;
  std::string ignored;
  if (object_get_attr(args[0], "archive", archive_value, ignored)) {
    std::string archive_path = value_to_string(archive_value);
    std::string member;
    if (zip_archive_split_member_path(archive_path, path, member)) {
      std::vector<uint8_t> archive_bytes;
      if (!runtime.vfs().read_file(archive_path, archive_bytes, error)) {
        return false;
      }
      ZipArchiveEntry entry;
      std::string extracted;
      if (!zip_archive_find_entry(archive_bytes, member, entry, error) ||
          !zip_archive_extract_member(archive_bytes, entry, extracted, error)) {
        return false;
      }
      out = Value::bytes(std::move(extracted));
      return true;
    }
  }
  std::vector<uint8_t> data;
  if (!runtime.vfs().read_file(path, data, error)) {
    return false;
  }
  out = Value::bytes(std::string(reinterpret_cast<const char*>(data.data()), data.size()));
  return true;
}

bool zipimporter_return_none(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "zipimporter method expected fullname";
    return false;
  }
  value_set_none(out);
  return true;
}

bool zipimporter_get_code(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.get_code expected fullname";
    return false;
  }
  std::string fullname;
  if (!zip_get_string_arg(args[1], "fullname", fullname, error)) {
    return false;
  }
  std::string archive;
  std::string member;
  bool is_package = false;
  bool archive_valid = false;
  if (!zipimporter_find_member(runtime, args[0], fullname, archive, member, is_package, archive_valid, error)) {
    if (!archive_valid) {
      value_set_none(out);
      return true;
    }
    error = "can't find module '" + fullname + "'";
    return false;
  }
  std::vector<uint8_t> archive_bytes;
  if (!runtime.vfs().read_file(archive, archive_bytes, error)) {
    return false;
  }
  ZipArchiveEntry entry;
  if (!zip_archive_find_entry(archive_bytes, member, entry, error)) {
    return false;
  }
  std::string source;
  if (!zip_archive_extract_member(archive_bytes, entry, source, error)) {
    return false;
  }
  const Value* compile_builtin = runtime.find_builtin("compile");
  if (compile_builtin == nullptr) {
    error = "compile builtin is not registered";
    return false;
  }
  Value compile_args[3] = {
      Value::string(std::move(source)),
      Value::string(zip_origin_path(archive, member)),
      Value::string("exec"),
  };
  return runtime_call_callable(runtime, *compile_builtin, compile_args, 3, out, error);
}

bool zipimporter_execute_module(
    Runtime& runtime,
    const Value& loader,
    const std::string& fullname,
    Value module_value,
    bool register_module,
    bool& found,
    std::string& error) {
  found = false;
  std::string archive;
  std::string member;
  bool is_package = false;
  bool archive_valid = false;
  if (!zipimporter_find_member(runtime, loader, fullname, archive, member, is_package, archive_valid, error)) {
    if (!archive_valid) {
      error.clear();
      return true;
    }
    error = "can't find module '" + fullname + "'";
    return false;
  }
  found = true;
  std::vector<uint8_t> archive_bytes;
  if (!runtime.vfs().read_file(archive, archive_bytes, error)) {
    return false;
  }
  ZipArchiveEntry entry;
  if (!zip_archive_find_entry(archive_bytes, member, entry, error)) {
    return false;
  }
  std::string source;
  if (!zip_archive_extract_member(archive_bytes, entry, source, error)) {
    return false;
  }

  auto parsed = parse_source(source);
  if (!parsed.errors.empty()) {
    error = "parse error importing module '" + fullname + "': " + parsed.errors.front();
    return false;
  }
  auto lowered = lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    error = "lower error importing module '" + fullname + "': " + lowered.errors.front();
    return false;
  }

  const std::string origin = zip_origin_path(archive, member);
  std::string ignored;
  module_set_attr(module_value, "__name__", Value::string(fullname), ignored);
  module_set_attr(module_value, "__file__", Value::string(origin), ignored);
  const auto dot = fullname.rfind('.');
  module_set_attr(module_value, "__package__", Value::string(is_package ? fullname : (dot == std::string::npos ? "" : fullname.substr(0, dot))), ignored);
  module_set_attr(module_value, "__loader__", loader, ignored);
  module_set_attr(module_value, "__spec__", make_zip_module_spec(fullname, loader, archive, member, is_package), ignored);
  if (is_package) {
    module_set_attr(module_value, "__path__", Value::list({Value::string(zip_origin_path(archive, zip_module_base(fullname)))}), ignored);
  }

  auto module_ir = std::make_shared<ir::Module>(std::move(lowered.module));
  module_ir->source_file = origin;
  if (register_module) {
    runtime.register_module(fullname, module_value);
  }
  Interpreter interpreter(runtime);
  RuntimeResult result = interpreter.run_module(*module_ir, module_value, module_ir);
  if (!result.errors.empty()) {
    if (register_module) {
      runtime.unregister_module(fullname);
    }
    error = "runtime error importing module '" + fullname + "': " + result.errors.front();
    return false;
  }
  return true;
}

bool zipimporter_load_module(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.load_module expected fullname";
    return false;
  }
  std::string fullname;
  if (!zip_get_string_arg(args[1], "fullname", fullname, error)) {
    return false;
  }
  Value module_value = Value::module(fullname);
  bool found = false;
  if (!zipimporter_execute_module(runtime, args[0], fullname, module_value, true, found, error)) {
    return false;
  }
  if (!found) {
    value_set_none(out);
    return true;
  }
  out = std::move(module_value);
  return true;
}

bool zipimporter_exec_module(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.exec_module expected module";
    return false;
  }
  ModuleObject* module = value_as_module(args[1]);
  if (module == nullptr) {
    error = "zipimporter.exec_module expected module";
    return false;
  }
  const std::string fullname = module->name;
  bool found = false;
  if (!zipimporter_execute_module(runtime, args[0], fullname, args[1], false, found, error)) {
    return false;
  }
  if (!found) {
    error = "can't find module '" + fullname + "'";
    return false;
  }
  value_set_none(out);
  return true;
}

bool zipimporter_get_source(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.get_source expected fullname";
    return false;
  }
  std::string fullname;
  if (!zip_get_string_arg(args[1], "fullname", fullname, error)) {
    return false;
  }
  std::string archive;
  std::string member;
  bool is_package = false;
  bool archive_valid = false;
  if (!zipimporter_find_member(runtime, args[0], fullname, archive, member, is_package, archive_valid, error)) {
    if (!archive_valid) {
      value_set_none(out);
      return true;
    }
    error = "can't find module '" + fullname + "'";
    return false;
  }
  std::vector<uint8_t> archive_bytes;
  if (!runtime.vfs().read_file(archive, archive_bytes, error)) {
    return false;
  }
  ZipArchiveEntry entry;
  if (!zip_archive_find_entry(archive_bytes, member, entry, error)) {
    return false;
  }
  std::string source;
  if (!zip_archive_extract_member(archive_bytes, entry, source, error)) {
    return false;
  }
  out = Value::string(std::move(source));
  return true;
}

bool zipimporter_is_package(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.is_package expected fullname";
    return false;
  }
  std::string fullname;
  if (!zip_get_string_arg(args[1], "fullname", fullname, error)) {
    return false;
  }
  std::string archive;
  std::string member;
  bool is_package = false;
  bool archive_valid = false;
  if (!zipimporter_find_member(runtime, args[0], fullname, archive, member, is_package, archive_valid, error)) {
    if (!archive_valid) {
      out = Value::boolean(false);
      return true;
    }
    error = "can't find module '" + fullname + "'";
    return false;
  }
  out = Value::boolean(is_package);
  return true;
}

Value make_zipimporter_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipimport")});
  attrs.push_back({"__init__", runtime.make_native_function("zipimport.zipimporter.__init__", zipimporter_init)});
  attrs.push_back({"find_spec", runtime.make_native_function("zipimport.zipimporter.find_spec", zipimporter_find_spec)});
  attrs.push_back({"find_module", runtime.make_native_function("zipimport.zipimporter.find_module", zipimporter_find_module)});
  attrs.push_back({"get_filename", runtime.make_native_function("zipimport.zipimporter.get_filename", zipimporter_get_filename)});
  attrs.push_back({"get_data", runtime.make_native_function("zipimport.zipimporter.get_data", zipimporter_get_data)});
  attrs.push_back({"get_code", runtime.make_native_function("zipimport.zipimporter.get_code", zipimporter_get_code)});
  attrs.push_back({"get_source", runtime.make_native_function("zipimport.zipimporter.get_source", zipimporter_get_source)});
  attrs.push_back({"load_module", runtime.make_native_function("zipimport.zipimporter.load_module", zipimporter_load_module)});
  attrs.push_back({"exec_module", runtime.make_native_function("zipimport.zipimporter.exec_module", zipimporter_exec_module)});
  attrs.push_back({"is_package", runtime.make_native_function("zipimport.zipimporter.is_package", zipimporter_is_package)});
  return Value::class_object("zipimporter", std::move(attrs));
}

} // namespace

void register_zipimport_module(Runtime& runtime) {
  Value error_class = Value::class_object("ZipImportError", {});
  Value zipimporter = make_zipimporter_class(runtime);
  NativeModuleBuilder builder(runtime, "zipimport");
  builder.value("zipimporter", zipimporter)
      .value("ZipImportError", error_class)
      .value("_zip_directory_cache", Value::dict({}));
  runtime.register_module("zipimport", builder.finish());

  Value sys;
  std::string error;
  if (runtime.import_module("sys", sys, error)) {
    std::string ignored;
    module_set_attr(sys, "path_hooks", Value::list({zipimporter}), ignored);
  }
}

} // namespace xlang3
