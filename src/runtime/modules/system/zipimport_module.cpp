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
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include "zip_archive.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>

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

bool raise_zipimport_error(Runtime& runtime, const std::string& message, std::string& error) {
  error = message;
  Value module;
  std::string ignored;
  if (runtime.import_module("zipimport", module, ignored)) {
    Value klass;
    if (module_get_attr(module, "ZipImportError", klass, ignored) && value_as_class(klass) != nullptr) {
      runtime.set_pending_exception(runtime.make_exception_from_class(std::move(klass), error));
      return false;
    }
  }
  runtime.raise_class_error("ImportError", error);
  return false;
}

std::string zip_module_base(const std::string& fullname) {
  std::string base = fullname;
  for (char& ch : base) {
    if (ch == '.') {
      ch = '/';
    } else if (ch == '\\') {
      ch = '/';
    }
  }
  return base;
}

std::string zip_origin_path(const std::string& archive, const std::string& member) {
  // XLang3's supported desktop target is Windows. zipimport uses native
  // Windows paths for __file__, __path__, and ModuleSpec.origin.
  constexpr char separator = '\\';
  std::string origin = archive;
  for (char& ch : origin) {
    if (ch == '/') ch = separator;
  }
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
  std::string cached = origin;
  const std::filesystem::path origin_path(origin);
  if (origin_path.extension() == ".py") {
    cached = (origin_path.parent_path() / "__pycache__" /
              (origin_path.stem().string() + ".xlang3-314.pyc")).string();
  }
  object_set_attr(spec, "cached", Value::string(cached), ignored);
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
  std::string prefix;
  Value prefix_value;
  std::string ignored;
  if (object_get_attr(self, "prefix", prefix_value, ignored)) {
    (void)zip_get_string_arg(prefix_value, "prefix", prefix, ignored);
  }
  for (char& ch : prefix) if (ch == '\\') ch = '/';
  if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
  const std::string base = prefix + zip_module_base(fullname);
  member = base + ".py";
  for (const auto& entry : entries) {
    if (entry.name == member) {
      is_package = false;
      return true;
    }
  }
  member = base + ".pyc";
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
  const std::string package_bytecode = base + "/__init__.pyc";
  for (const auto& entry : entries) {
    if (entry.name == package_bytecode) {
      member = package_bytecode;
      is_package = true;
      return true;
    }
  }
  return false;
}

bool zipimporter_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.__init__ expected archive path";
    return false;
  }
  std::string archive;
  if (!zip_get_string_arg(args[1], "archive path", archive, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string prefix;
  std::string archive_path = archive;
  std::vector<uint8_t> archive_bytes;
  std::vector<ZipArchiveEntry> entries;
  if (!runtime.vfs().read_file(archive_path, archive_bytes, error)) {
    error.clear();
    const auto zip_end = archive.find(".zip");
    if (zip_end != std::string::npos && zip_end + 4 < archive.size() &&
        (archive[zip_end + 4] == '/' || archive[zip_end + 4] == '\\')) {
      archive_path = archive.substr(0, zip_end + 4);
      prefix = archive.substr(zip_end + 5);
#if defined(_WIN32)
      for (char& ch : prefix) if (ch == '/') ch = '\\';
      if (!prefix.empty() && prefix.back() != '\\') prefix.push_back('\\');
#else
      for (char& ch : prefix) if (ch == '\\') ch = '/';
      if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');
#endif
    }
  }
  if (archive_bytes.empty() && !runtime.vfs().read_file(archive_path, archive_bytes, error)) {
    error = "not a Zip file: " + archive;
    return raise_zipimport_error(runtime, error, error);
  }
  if (
      !zip_archive_list_entries(archive_bytes, entries, error)) {
    error = "not a Zip file: " + archive;
    return raise_zipimport_error(runtime, error, error);
  }
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "archive", Value::string(archive_path), ignored);
  object_set_attr(self, "prefix", Value::string(prefix), ignored);
  Value zipimport;
  Value directory_cache;
  if (runtime.import_module("zipimport", zipimport, ignored) &&
      module_get_attr(zipimport, "_zip_directory_cache", directory_cache, ignored)) {
    std::vector<std::pair<Value, Value>> directory_entries;
    directory_entries.reserve(entries.size());
    for (const auto& entry : entries) {
      std::string member = entry.name;
#if defined(_WIN32)
      for (char& ch : member) if (ch == '/') ch = '\\';
#endif
      directory_entries.push_back({Value::string(std::move(member)), Value::none()});
    }
    mapping_set_item(
        directory_cache,
        Value::string(archive_path),
        Value::dict(std::move(directory_entries)),
        ignored);
  }
  value_set_none(out);
  return true;
}

bool zipimporter_init_kw(
    Runtime& runtime,
    const Value*,
    uint32_t,
    const NativeKeywordArg*,
    uint32_t,
    Value&,
    std::string& error,
    void*) {
  error = "zipimporter() takes no keyword arguments";
  runtime.raise_class_error("TypeError", error);
  return false;
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

bool zipimporter_create_module(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.create_module expected spec";
    return false;
  }
  value_set_none(out);
  return true;
}

bool zipimporter_cache_files(Runtime& runtime, const Value& self, Value& out, std::string& error) {
  std::string archive;
  if (!zipimporter_archive(self, archive, error)) return false;
  std::vector<uint8_t> archive_bytes;
  if (!runtime.vfs().read_file(archive, archive_bytes, error)) {
    Value module;
    Value cache;
    std::string ignored;
    if (runtime.import_module("zipimport", module, ignored) &&
        module_get_attr(module, "_zip_directory_cache", cache, ignored)) {
      (void)mapping_delete_item(cache, Value::string(archive), ignored);
    }
    out = Value::list({});
    return true;
  }
  std::vector<ZipArchiveEntry> entries;
  if (!zip_archive_list_entries(archive_bytes, entries, error)) return false;
  std::set<std::string> names;
  for (const auto& entry : entries) {
    std::string name = entry.name;
#if defined(_WIN32)
    for (char& ch : name) if (ch == '/') ch = '\\';
#endif
    names.insert(name);
    for (std::size_t slash = name.find_first_of("/\\"); slash != std::string::npos;
         slash = name.find_first_of("/\\", slash + 1)) {
      names.insert(name.substr(0, slash + 1));
    }
  }
  std::vector<Value> files;
  std::vector<std::pair<Value, Value>> cache_entries;
  files.reserve(names.size());
  cache_entries.reserve(names.size());
  for (const auto& name : names) {
    files.push_back(Value::string(name));
    cache_entries.push_back({Value::string(name), Value::none()});
  }
  Value module;
  Value cache;
  std::string ignored;
  if (runtime.import_module("zipimport", module, ignored) &&
      module_get_attr(module, "_zip_directory_cache", cache, ignored)) {
    (void)mapping_set_item(cache, Value::string(archive), Value::dict(std::move(cache_entries)), ignored);
  }
  out = Value::list(std::move(files));
  return true;
}

bool zipimporter_get_files(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "zipimporter._get_files expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return zipimporter_cache_files(runtime, args[0], out, error);
}

bool zipimporter_invalidate_caches(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "zipimporter.invalidate_caches expected no arguments";
    return false;
  }
  Value ignored_files;
  if (!zipimporter_cache_files(runtime, args[0], ignored_files, error)) return false;
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
    return raise_zipimport_error(runtime, "can't find module '" + fullname + "'", error);
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
    std::string archive_path;
    if (!zip_get_string_arg(archive_value, "archive", archive_path, error)) return false;
    std::string member;
    if (!zip_archive_split_member_path(archive_path, path, member)) {
      member = path;
      for (char& ch : member) if (ch == '\\') ch = '/';
      while (!member.empty() && member.front() == '/') member.erase(member.begin());
    }
    std::vector<uint8_t> archive_bytes;
    if (!runtime.vfs().read_file(archive_path, archive_bytes, error)) {
      return false;
    }
    if (!member.empty() && member.back() == '/') {
      out = Value::bytes("");
      return true;
    }
    ZipArchiveEntry entry;
    std::string extracted;
    if (!zip_archive_find_entry(archive_bytes, member, entry, error) ||
        !zip_archive_extract_member(archive_bytes, entry, extracted, error)) {
      error = "can't find data file '" + path + "'";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    out = Value::bytes(std::move(extracted));
    return true;
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
    return raise_zipimport_error(runtime, "can't find module '" + fullname + "'", error);
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
  if (std::filesystem::path(member).extension() == ".pyc") {
    if (source.size() < 16 || source.compare(0, 4, "\x33\x58\x0d\x0a", 4) != 0) {
      return raise_zipimport_error(runtime, "bad magic number in '" + member + "'", error);
    }
    Value marshal_module;
    Value loads;
    Value payload = Value::bytes(source.substr(16));
    if (!runtime.import_module("marshal", marshal_module, error) ||
        !module_get_attr(marshal_module, "loads", loads, error) ||
        !runtime_call_callable(runtime, loads, &payload, 1, out, error)) {
      return false;
    }
    if (value_as_code(out) == nullptr) {
      return raise_zipimport_error(runtime, "bytecode does not contain code", error);
    }
    return true;
  }
  std::string decoded_source;
  if (!runtime.decode_python_source(source, decoded_source, error)) {
    return false;
  }
  source = std::move(decoded_source);
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
    return raise_zipimport_error(runtime, "can't find module '" + fullname + "'", error);
  }
  found = true;
  Value code_args[] = {loader, Value::string(fullname)};
  Value code_value;
  if (!zipimporter_get_code(runtime, code_args, 2, code_value, error, nullptr)) {
    return false;
  }
  auto* code = value_as_code(code_value);
  if (code == nullptr || code->module == nullptr) {
    error = "zipimporter.get_code() did not return a code object";
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

  Value previous_module;
  bool had_previous_module = false;
  if (!register_module && runtime.has_registered_module(fullname)) {
    std::string previous_error;
    had_previous_module = runtime.import_module(fullname, previous_module, previous_error);
  }
  if (register_module) {
    runtime.register_module(fullname, module_value);
  }
  Interpreter interpreter(runtime);
  RuntimeResult result = interpreter.run_module(*code->module, module_value, code->module);
  if (!result.errors.empty()) {
    if (result.exception.tag != ValueTag::Invalid) {
      runtime.set_pending_exception(result.exception);
    }
    if (register_module) {
      runtime.unregister_module(fullname);
    } else if (had_previous_module) {
      runtime.register_module(fullname, std::move(previous_module));
    } else {
      runtime.unregister_module(fullname);
    }
    error = "runtime error importing module '" + fullname + "': " + result.errors.front();
    return false;
  }
  if (!register_module) {
    if (had_previous_module) {
      runtime.register_module(fullname, std::move(previous_module));
    } else {
      runtime.unregister_module(fullname);
    }
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
    return raise_zipimport_error(runtime, "can't find module '" + fullname + "'", error);
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
    return raise_zipimport_error(runtime, "can't find module '" + fullname + "'", error);
  }
  if (std::filesystem::path(member).extension() == ".pyc") {
    value_set_none(out);
    return true;
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
  std::string decoded_source;
  if (!runtime.decode_python_source(source, decoded_source, error)) {
    return false;
  }
  source = std::move(decoded_source);
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
    return raise_zipimport_error(runtime, "can't find module '" + fullname + "'", error);
  }
  out = Value::boolean(is_package);
  return true;
}

bool zipimporter_get_resource_reader(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.get_resource_reader expected fullname";
    return false;
  }
  if (value_as_string(args[1]) == nullptr) {
    error = "zipimporter.get_resource_reader fullname must be str";
    return false;
  }
  Value readers_module;
  if (!runtime.import_module("importlib.resources.readers", readers_module, error)) {
    return false;
  }
  Value zip_reader_class;
  if (!module_get_attr(readers_module, "ZipReader", zip_reader_class, error)) {
    return false;
  }
  std::string fullname = string_object_to_string(*value_as_string(args[1]));
  std::string archive;
  std::string member;
  bool is_package = false;
  bool archive_valid = false;
  std::string lookup_error;
  (void)zipimporter_find_member(
      runtime, args[0], fullname, archive, member, is_package, archive_valid, lookup_error);
  Value original_prefix;
  std::string ignored;
  const bool had_prefix = object_get_attr(args[0], "prefix", original_prefix, ignored);
  if (!is_package) {
    const auto dot = fullname.rfind('.');
    if (dot != std::string::npos) {
      std::string prefix;
      if (had_prefix && value_as_string(original_prefix) != nullptr)
        prefix = string_object_to_string(*value_as_string(original_prefix));
      std::string parent = fullname.substr(0, dot);
      std::replace(parent.begin(), parent.end(), '.', '/');
      prefix += parent + "/";
      object_set_attr(const_cast<Value&>(args[0]), "prefix", Value::string(prefix), ignored);
    }
  }
  Value reader_args[] = {args[0], args[1]};
  const bool ok = runtime_call_callable(runtime, zip_reader_class, reader_args, 2, out, error);
  if (had_prefix) object_set_attr(const_cast<Value&>(args[0]), "prefix", original_prefix, ignored);
  return ok;
}

Value make_zipimporter_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipimport")});
  attrs.push_back({"__init__", runtime.make_native_function(
      "zipimport.zipimporter.__init__", zipimporter_init, nullptr, nullptr, nullptr, false, zipimporter_init_kw)});
  attrs.push_back({"find_spec", runtime.make_native_function("zipimport.zipimporter.find_spec", zipimporter_find_spec)});
  attrs.push_back({"find_module", runtime.make_native_function("zipimport.zipimporter.find_module", zipimporter_find_module)});
  attrs.push_back({"create_module", runtime.make_native_function("zipimport.zipimporter.create_module", zipimporter_create_module)});
  attrs.push_back({"invalidate_caches", runtime.make_native_function("zipimport.zipimporter.invalidate_caches", zipimporter_invalidate_caches)});
  attrs.push_back({"_get_files", runtime.make_native_function("zipimport.zipimporter._get_files", zipimporter_get_files)});
  attrs.push_back({"get_filename", runtime.make_native_function("zipimport.zipimporter.get_filename", zipimporter_get_filename)});
  attrs.push_back({"get_data", runtime.make_native_function("zipimport.zipimporter.get_data", zipimporter_get_data)});
  attrs.push_back({"get_code", runtime.make_native_function("zipimport.zipimporter.get_code", zipimporter_get_code)});
  attrs.push_back({"get_source", runtime.make_native_function("zipimport.zipimporter.get_source", zipimporter_get_source)});
  attrs.push_back({"load_module", runtime.make_native_function("zipimport.zipimporter.load_module", zipimporter_load_module)});
  attrs.push_back({"exec_module", runtime.make_native_function("zipimport.zipimporter.exec_module", zipimporter_exec_module)});
  attrs.push_back({"is_package", runtime.make_native_function("zipimport.zipimporter.is_package", zipimporter_is_package)});
  attrs.push_back({"get_resource_reader", runtime.make_native_function("zipimport.zipimporter.get_resource_reader", zipimporter_get_resource_reader)});
  return Value::class_object("zipimporter", std::move(attrs));
}

} // namespace

void register_zipimport_module(Runtime& runtime) {
  Value error_class = Value::class_object(
      "ZipImportError",
      {{"__module__", Value::string("zipimport")},
       {"__qualname__", Value::string("ZipImportError")}},
      runtime.find_builtin("ImportError") != nullptr
          ? *runtime.find_builtin("ImportError")
          : Value::invalid());
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
    Value path_hooks;
    if (module_get_attr(sys, "path_hooks", path_hooks, ignored)) {
      if (auto* hooks = value_as_list(path_hooks)) {
        hooks->items.push_back(zipimporter);
      } else {
        module_set_attr(sys, "path_hooks", Value::list({zipimporter}), ignored);
      }
    } else {
      module_set_attr(sys, "path_hooks", Value::list({zipimporter}), ignored);
    }
  }
}

} // namespace xlang3
