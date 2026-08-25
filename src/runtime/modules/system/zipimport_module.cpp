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

#include <cstdint>

namespace xlang3 {

namespace {

struct ZipEntry {
  std::string name;
  uint16_t method = 0;
  uint32_t compressed_size = 0;
  uint32_t uncompressed_size = 0;
  uint32_t local_header_offset = 0;
};

uint16_t zip_u16(const std::vector<uint8_t>& data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8u);
}

uint32_t zip_u32(const std::vector<uint8_t>& data, size_t offset) {
  return static_cast<uint32_t>(data[offset]) |
         (static_cast<uint32_t>(data[offset + 1]) << 8u) |
         (static_cast<uint32_t>(data[offset + 2]) << 16u) |
         (static_cast<uint32_t>(data[offset + 3]) << 24u);
}

bool zip_find_entry(
    const std::vector<uint8_t>& archive,
    const std::string& member,
    ZipEntry& out,
    std::string& error) {
  if (archive.size() < 22) {
    error = "zip archive is too small";
    return false;
  }

  size_t eocd = std::string::npos;
  const size_t search_start = archive.size() > 66000 ? archive.size() - 66000 : 0;
  for (size_t pos = archive.size() - 22; pos + 4 <= archive.size() && pos >= search_start; --pos) {
    if (zip_u32(archive, pos) == 0x06054b50u) {
      eocd = pos;
      break;
    }
    if (pos == 0) {
      break;
    }
  }
  if (eocd == std::string::npos) {
    error = "zip end-of-central-directory not found";
    return false;
  }

  const uint16_t entry_count = zip_u16(archive, eocd + 10);
  const uint32_t central_offset = zip_u32(archive, eocd + 16);
  size_t pos = central_offset;
  for (uint16_t i = 0; i < entry_count; ++i) {
    if (pos + 46 > archive.size() || zip_u32(archive, pos) != 0x02014b50u) {
      error = "zip central-directory entry is invalid";
      return false;
    }
    const uint16_t method = zip_u16(archive, pos + 10);
    const uint32_t compressed_size = zip_u32(archive, pos + 20);
    const uint32_t uncompressed_size = zip_u32(archive, pos + 24);
    const uint16_t name_len = zip_u16(archive, pos + 28);
    const uint16_t extra_len = zip_u16(archive, pos + 30);
    const uint16_t comment_len = zip_u16(archive, pos + 32);
    const uint32_t local_header_offset = zip_u32(archive, pos + 42);
    if (pos + 46u + name_len + extra_len + comment_len > archive.size()) {
      error = "zip central-directory entry exceeds archive size";
      return false;
    }
    std::string name(reinterpret_cast<const char*>(archive.data() + pos + 46), name_len);
    if (name == member) {
      out.name = std::move(name);
      out.method = method;
      out.compressed_size = compressed_size;
      out.uncompressed_size = uncompressed_size;
      out.local_header_offset = local_header_offset;
      return true;
    }
    pos += 46u + name_len + extra_len + comment_len;
  }
  error = "zip member not found: " + member;
  return false;
}

bool zip_extract_stored(
    const std::vector<uint8_t>& archive,
    const ZipEntry& entry,
    std::string& out,
    std::string& error) {
  if (entry.method != 0) {
    error = "zip member uses unsupported compression method " + std::to_string(entry.method);
    return false;
  }
  const size_t local = entry.local_header_offset;
  if (local + 30 > archive.size() || zip_u32(archive, local) != 0x04034b50u) {
    error = "zip local header is invalid";
    return false;
  }
  const uint16_t name_len = zip_u16(archive, local + 26);
  const uint16_t extra_len = zip_u16(archive, local + 28);
  const size_t data_offset = local + 30u + name_len + extra_len;
  if (data_offset + entry.compressed_size > archive.size()) {
    error = "zip member data exceeds archive size";
    return false;
  }
  if (entry.compressed_size != entry.uncompressed_size) {
    error = "zip stored member has mismatched sizes";
    return false;
  }
  out.assign(reinterpret_cast<const char*>(archive.data() + data_offset), entry.uncompressed_size);
  return true;
}

bool zip_split_archive_member(const std::string& archive, const std::string& path, std::string& member) {
  if (path.size() <= archive.size() || path.compare(0, archive.size(), archive) != 0) {
    return false;
  }
  char separator = path[archive.size()];
  if (separator != '/' && separator != '\\') {
    return false;
  }
  member = path.substr(archive.size() + 1);
  for (auto& ch : member) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return !member.empty();
}

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
    if (zip_split_archive_member(archive_path, path, member)) {
      std::vector<uint8_t> archive_bytes;
      if (!runtime.vfs().read_file(archive_path, archive_bytes, error)) {
        return false;
      }
      ZipEntry entry;
      std::string extracted;
      if (!zip_find_entry(archive_bytes, member, entry, error) ||
          !zip_extract_stored(archive_bytes, entry, extracted, error)) {
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
