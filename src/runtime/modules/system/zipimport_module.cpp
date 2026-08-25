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

#include "zip_archive.h"

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

bool zipimporter_find_spec(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "zipimporter.find_spec expected fullname, optional target";
    return false;
  }
  value_set_none(out);
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

bool zipimporter_get_filename(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.get_filename expected fullname";
    return false;
  }
  Value archive;
  std::string ignored;
  if (!object_get_attr(args[0], "archive", archive, ignored)) {
    error = "zipimporter has no archive";
    return false;
  }
  std::string fullname;
  if (!zip_get_string_arg(args[1], "fullname", fullname, error)) {
    return false;
  }
  out = Value::string(value_to_string(archive) + "/" + fullname + ".py");
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
          !zip_archive_extract_stored(archive_bytes, entry, extracted, error)) {
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

bool zipimporter_is_package(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "zipimporter.is_package expected fullname";
    return false;
  }
  out = Value::boolean(false);
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
  attrs.push_back({"get_code", runtime.make_native_function("zipimport.zipimporter.get_code", zipimporter_return_none)});
  attrs.push_back({"get_source", runtime.make_native_function("zipimport.zipimporter.get_source", zipimporter_return_none)});
  attrs.push_back({"load_module", runtime.make_native_function("zipimport.zipimporter.load_module", zipimporter_return_none)});
  attrs.push_back({"is_package", runtime.make_native_function("zipimport.zipimporter.is_package", zipimporter_is_package)});
  return Value::class_object("zipimporter", std::move(attrs));
}

} // namespace

void register_zipimport_module(Runtime& runtime) {
  Value error_class = Value::class_object("ZipImportError", {});
  NativeModuleBuilder builder(runtime, "zipimport");
  builder.value("zipimporter", make_zipimporter_class(runtime))
      .value("ZipImportError", error_class)
      .value("_zip_directory_cache", Value::dict({}));
  runtime.register_module("zipimport", builder.finish());
}

} // namespace xlang3
