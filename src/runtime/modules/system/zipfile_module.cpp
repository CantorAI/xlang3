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

#include <algorithm>

namespace xlang3 {

namespace {

constexpr const char* kZipFileNativeType = "zipfile.ZipFile";

struct ZipFileState {
  std::string path;
  std::string mode = "r";
  bool closed = false;
  bool dirty = false;
  std::vector<ZipArchiveMember> members;
};

void zipfile_cleanup(void* data) {
  delete static_cast<ZipFileState*>(data);
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

bool get_bytes_like(const Value& value, std::string& out) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  return false;
}

ZipFileState* zipfile_state(const Value& self, std::string& error) {
  auto* state = static_cast<ZipFileState*>(instance_get_native_data(self, kZipFileNativeType));
  if (state == nullptr) {
    error = "invalid ZipFile object";
    return nullptr;
  }
  if (state->closed) {
    error = "Attempt to use ZIP archive that was already closed";
    return nullptr;
  }
  return state;
}

std::string normalized_member_name(std::string name) {
  for (auto& ch : name) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return name;
}

bool load_existing_archive(Runtime& runtime, ZipFileState& state, std::string& error) {
  std::vector<uint8_t> archive;
  if (!runtime.vfs().read_file(state.path, archive, error)) {
    return state.mode == "w" || state.mode == "x";
  }
  std::vector<ZipArchiveEntry> entries;
  if (!zip_archive_list_entries(archive, entries, error)) {
    return false;
  }
  state.members.clear();
  state.members.reserve(entries.size());
  for (const auto& entry : entries) {
    std::string data;
    if (!zip_archive_extract_stored(archive, entry, data, error)) {
      return false;
    }
    state.members.push_back(ZipArchiveMember{entry.name, std::move(data)});
  }
  return true;
}

bool flush_archive(Runtime& runtime, ZipFileState& state, std::string& error) {
  if (!state.dirty) {
    return true;
  }
  std::string archive;
  if (!zip_archive_build_stored(state.members, archive, error)) {
    return false;
  }
  if (!runtime.vfs().write_file(
          state.path,
          reinterpret_cast<const uint8_t*>(archive.data()),
          archive.size(),
          error)) {
    return false;
  }
  state.dirty = false;
  return true;
}

bool replace_or_append_member(ZipFileState& state, std::string name, std::string data) {
  name = normalized_member_name(std::move(name));
  for (auto& member : state.members) {
    if (member.name == name) {
      member.data = std::move(data);
      state.dirty = true;
      return true;
    }
  }
  state.members.push_back(ZipArchiveMember{std::move(name), std::move(data)});
  state.dirty = true;
  return true;
}

Value make_zipinfo(Runtime& runtime, const ZipArchiveMember& member) {
  Value klass;
  std::string ignored;
  if (!runtime.import_module("zipfile", klass, ignored)) {
    return Value::none();
  }
  Value zip_info_class;
  if (!module_get_attr(klass, "ZipInfo", zip_info_class, ignored)) {
    return Value::none();
  }
  Value instance = Value::instance(zip_info_class);
  object_set_attr(instance, "filename", Value::string(member.name), ignored);
  object_set_attr(instance, "file_size", Value::int64(static_cast<int64_t>(member.data.size())), ignored);
  object_set_attr(instance, "compress_size", Value::int64(static_cast<int64_t>(member.data.size())), ignored);
  object_set_attr(instance, "compress_type", Value::int64(0), ignored);
  return instance;
}

bool zipfile_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "ZipFile() expected file, optional mode, and optional compression";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[1], "ZipFile file", path, error)) {
    return false;
  }
  std::string mode = "r";
  if (argc >= 3 && args[2].tag != ValueTag::None && !get_string_arg(args[2], "ZipFile mode", mode, error)) {
    return false;
  }
  if (mode != "r" && mode != "w" && mode != "a" && mode != "x") {
    error = "ZipFile mode must be 'r', 'w', 'a', or 'x'";
    return false;
  }
  if (argc >= 4 && args[3].tag == ValueTag::Int64 && args[3].as.i64 != 0) {
    error = "ZipFile currently supports ZIP_STORED only";
    return false;
  }

  auto* state = new ZipFileState();
  state->path = std::move(path);
  state->mode = std::move(mode);
  if ((state->mode == "r" || state->mode == "a") && !load_existing_archive(runtime, *state, error)) {
    delete state;
    return false;
  }
  if (!instance_set_native_data(args[0], kZipFileNativeType, state, zipfile_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool zipfile_namelist(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipFile.namelist() expected no arguments";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> names;
  names.reserve(state->members.size());
  for (const auto& member : state->members) {
    names.push_back(Value::string(member.name));
  }
  out = Value::list(std::move(names));
  return true;
}

bool zipfile_infolist(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipFile.infolist() expected no arguments";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(state->members.size());
  for (const auto& member : state->members) {
    values.push_back(make_zipinfo(runtime, member));
  }
  out = Value::list(std::move(values));
  return true;
}

bool zipfile_read(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "ZipFile.read() expected name and optional pwd";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string name;
  if (!get_string_arg(args[1], "ZipFile.read name", name, error)) {
    return false;
  }
  name = normalized_member_name(std::move(name));
  for (const auto& member : state->members) {
    if (member.name == name) {
      out = Value::bytes(member.data);
      return true;
    }
  }
  error = "There is no item named '" + name + "' in the archive";
  return false;
}

bool zipfile_writestr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 4) {
    error = "ZipFile.writestr() expected name and data";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->mode == "r") {
    error = "write() requires mode 'w', 'x', or 'a'";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[1], "ZipFile.writestr name", name, error)) {
    return false;
  }
  std::string data;
  if (!get_bytes_like(args[2], data)) {
    error = "ZipFile.writestr data must be str or bytes-like";
    return false;
  }
  replace_or_append_member(*state, std::move(name), std::move(data));
  value_set_none(out);
  return true;
}

bool zipfile_write(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "ZipFile.write() expected filename and optional arcname";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->mode == "r") {
    error = "write() requires mode 'w', 'x', or 'a'";
    return false;
  }
  std::string filename;
  if (!get_string_arg(args[1], "ZipFile.write filename", filename, error)) {
    return false;
  }
  std::string arcname = filename;
  if (argc >= 3 && args[2].tag != ValueTag::None && !get_string_arg(args[2], "ZipFile.write arcname", arcname, error)) {
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!runtime.vfs().read_file(filename, bytes, error)) {
    return false;
  }
  std::string data(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  replace_or_append_member(*state, std::move(arcname), std::move(data));
  value_set_none(out);
  return true;
}

bool zipfile_close(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipFile.close() expected no arguments";
    return false;
  }
  auto* state = static_cast<ZipFileState*>(instance_get_native_data(args[0], kZipFileNativeType));
  if (state == nullptr) {
    error = "invalid ZipFile object";
    return false;
  }
  if (!state->closed && !flush_archive(runtime, *state, error)) {
    return false;
  }
  state->closed = true;
  value_set_none(out);
  return true;
}

bool zipfile_enter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipFile.__enter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool zipfile_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "ZipFile.__exit__() expected exc_type, exc, and tb";
    return false;
  }
  Value close_result;
  if (!zipfile_close(runtime, args, 1, close_result, error, nullptr)) {
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool zipinfo_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "ZipInfo() expected optional filename";
    return false;
  }
  std::string filename;
  if (argc == 2 && args[1].tag != ValueTag::None && !get_string_arg(args[1], "ZipInfo filename", filename, error)) {
    return false;
  }
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "filename", Value::string(filename), ignored);
  object_set_attr(self, "file_size", Value::int64(0), ignored);
  object_set_attr(self, "compress_size", Value::int64(0), ignored);
  object_set_attr(self, "compress_type", Value::int64(0), ignored);
  value_set_none(out);
  return true;
}

Value make_zipfile_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"__init__", runtime.make_native_function("zipfile.ZipFile.__init__", zipfile_init)});
  attrs.push_back({"__enter__", runtime.make_native_function("zipfile.ZipFile.__enter__", zipfile_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("zipfile.ZipFile.__exit__", zipfile_exit)});
  attrs.push_back({"close", runtime.make_native_function("zipfile.ZipFile.close", zipfile_close)});
  attrs.push_back({"namelist", runtime.make_native_function("zipfile.ZipFile.namelist", zipfile_namelist)});
  attrs.push_back({"infolist", runtime.make_native_function("zipfile.ZipFile.infolist", zipfile_infolist)});
  attrs.push_back({"read", runtime.make_native_function("zipfile.ZipFile.read", zipfile_read)});
  attrs.push_back({"write", runtime.make_native_function("zipfile.ZipFile.write", zipfile_write)});
  attrs.push_back({"writestr", runtime.make_native_function("zipfile.ZipFile.writestr", zipfile_writestr)});
  return Value::class_object("ZipFile", std::move(attrs));
}

Value make_zipinfo_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"__init__", runtime.make_native_function("zipfile.ZipInfo.__init__", zipinfo_init)});
  return Value::class_object("ZipInfo", std::move(attrs));
}

} // namespace

void register_zipfile_module(Runtime& runtime) {
  Value bad_zip_file = Value::class_object("BadZipFile", {});
  NativeModuleBuilder builder(runtime, "zipfile");
  builder.value("ZipFile", make_zipfile_class(runtime))
      .value("ZipInfo", make_zipinfo_class(runtime))
      .value("BadZipFile", bad_zip_file)
      .value("BadZipfile", bad_zip_file)
      .value("ZIP_STORED", Value::int64(0))
      .value("ZIP_DEFLATED", Value::int64(8))
      .value("ZIP_BZIP2", Value::int64(12))
      .value("ZIP_LZMA", Value::int64(14));
  runtime.register_module("zipfile", builder.finish());
}

} // namespace xlang3
