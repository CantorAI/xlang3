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
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include "zip_archive.h"

#include <algorithm>
#include <cstdio>

namespace xlang3 {

namespace {

constexpr const char* kZipFileNativeType = "zipfile.ZipFile";
constexpr const char* kZipExtFileNativeType = "zipfile.ZipExtFile";

struct ZipFileState {
  std::string path;
  Value fileobj;
  bool owns_path = true;
  std::string mode = "r";
  uint16_t default_compression = 0;
  int compresslevel = -1;
  std::string comment;
  bool closed = false;
  bool dirty = false;
  std::vector<ZipArchiveMember> members;
};

struct ZipExtFileState {
  std::string data;
  size_t cursor = 0;
  bool closed = false;
};

void zipfile_cleanup(void* data) {
  delete static_cast<ZipFileState*>(data);
}

void zipextfile_cleanup(void* data) {
  delete static_cast<ZipExtFileState*>(data);
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

bool get_int_arg(const Value& value, int64_t& out) {
  if (value.tag != ValueTag::Int64) {
    return false;
  }
  out = value.as.i64;
  return true;
}

Value kw_value(const NativeKeywordArg* kwargs, uint32_t kwargc, const char* name, Value fallback = Value::none()) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string(kwargs[i].name) == name && kwargs[i].value != nullptr) {
      return *kwargs[i].value;
    }
  }
  return fallback;
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

bool read_file_like(Runtime& runtime, const Value& object, std::vector<uint8_t>& bytes, std::string& error) {
  Value read_method;
  if (!object_get_attr(object, "read", read_method, error)) {
    return false;
  }
  Value data;
  if (!runtime_call_callable(runtime, read_method, nullptr, 0, data, error)) {
    return false;
  }
  std::string text;
  if (!get_bytes_like(data, text)) {
    error = "zip file-like read() must return bytes-like data";
    return false;
  }
  bytes.assign(text.begin(), text.end());
  return true;
}

bool write_file_like(Runtime& runtime, const Value& object, const std::string& data, std::string& error) {
  Value write_method;
  if (!object_get_attr(object, "write", write_method, error)) {
    return false;
  }
  Value arg = Value::bytes(data);
  Value ignored;
  return runtime_call_callable(runtime, write_method, &arg, 1, ignored, error);
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

ZipExtFileState* zipextfile_state(const Value& self, std::string& error) {
  auto* state = static_cast<ZipExtFileState*>(instance_get_native_data(self, kZipExtFileNativeType));
  if (state == nullptr) {
    error = "invalid ZipExtFile object";
    return nullptr;
  }
  if (state->closed) {
    error = "I/O operation on closed ZipExtFile";
    return nullptr;
  }
  return state;
}

bool zipfile_member_name_arg(const Value& value, std::string& out, std::string& error) {
  if (get_string_arg(value, "zip member name", out, error)) {
    return true;
  }
  error.clear();
  Value filename;
  std::string ignored;
  if (object_get_attr(value, "filename", filename, ignored)) {
    return get_string_arg(filename, "zip member filename", out, error);
  }
  error = "zip member must be str or ZipInfo";
  return false;
}

std::string normalized_member_name(std::string name) {
  for (auto& ch : name) {
    if (ch == '\\') {
      ch = '/';
    }
  }
  return name;
}

uint16_t dos_time_from_tuple(const Value& value, uint16_t fallback) {
  auto* tuple = value_as_tuple(value);
  auto* list = value_as_list(value);
  const uint32_t size = tuple != nullptr
      ? tuple->items.size()
      : (list != nullptr ? static_cast<uint32_t>(list->items.size()) : 0);
  if (size < 6) {
    return fallback;
  }
  const Value& hour_value = tuple != nullptr ? tuple->items[3] : list->items[3];
  const Value& minute_value = tuple != nullptr ? tuple->items[4] : list->items[4];
  const Value& second_value = tuple != nullptr ? tuple->items[5] : list->items[5];
  const int64_t hour = hour_value.tag == ValueTag::Int64 ? hour_value.as.i64 : 0;
  const int64_t minute = minute_value.tag == ValueTag::Int64 ? minute_value.as.i64 : 0;
  const int64_t second = second_value.tag == ValueTag::Int64 ? second_value.as.i64 : 0;
  return static_cast<uint16_t>(((hour & 0x1f) << 11) | ((minute & 0x3f) << 5) | ((second / 2) & 0x1f));
}

uint16_t dos_date_from_tuple(const Value& value, uint16_t fallback) {
  auto* tuple = value_as_tuple(value);
  auto* list = value_as_list(value);
  const uint32_t size = tuple != nullptr
      ? tuple->items.size()
      : (list != nullptr ? static_cast<uint32_t>(list->items.size()) : 0);
  if (size < 3) {
    return fallback;
  }
  const Value& year_value = tuple != nullptr ? tuple->items[0] : list->items[0];
  const Value& month_value = tuple != nullptr ? tuple->items[1] : list->items[1];
  const Value& day_value = tuple != nullptr ? tuple->items[2] : list->items[2];
  const int64_t year = year_value.tag == ValueTag::Int64 ? year_value.as.i64 : 1980;
  const int64_t month = month_value.tag == ValueTag::Int64 ? month_value.as.i64 : 1;
  const int64_t day = day_value.tag == ValueTag::Int64 ? day_value.as.i64 : 1;
  return static_cast<uint16_t>((((year - 1980) & 0x7f) << 9) | ((month & 0xf) << 5) | (day & 0x1f));
}

Value tuple_from_dos_datetime(uint16_t date, uint16_t time) {
  const int year = 1980 + ((date >> 9) & 0x7f);
  const int month = (date >> 5) & 0xf;
  const int day = date & 0x1f;
  const int hour = (time >> 11) & 0x1f;
  const int minute = (time >> 5) & 0x3f;
  const int second = (time & 0x1f) * 2;
  return Value::tuple({
      Value::int64(year),
      Value::int64(month == 0 ? 1 : month),
      Value::int64(day == 0 ? 1 : day),
      Value::int64(hour),
      Value::int64(minute),
      Value::int64(second)});
}

uint16_t compression_from_value(const Value& value, uint16_t fallback, std::string& error) {
  if (value.tag == ValueTag::None) {
    return fallback;
  }
  if (value.tag != ValueTag::Int64 || (value.as.i64 != 0 && value.as.i64 != 8)) {
    error = "ZipFile supports ZIP_STORED and ZIP_DEFLATED";
    return UINT16_MAX;
  }
  return static_cast<uint16_t>(value.as.i64);
}

bool load_existing_archive(Runtime& runtime, ZipFileState& state, std::string& error) {
  std::vector<uint8_t> archive;
  if (state.owns_path) {
    if (!runtime.vfs().read_file(state.path, archive, error)) {
      return state.mode == "w" || state.mode == "x";
    }
  } else if (!read_file_like(runtime, state.fileobj, archive, error)) {
    return false;
  }
  std::string archive_comment;
  std::string ignored_comment_error;
  if (zip_archive_read_comment(archive, archive_comment, ignored_comment_error)) {
    state.comment = std::move(archive_comment);
  }
  std::vector<ZipArchiveEntry> entries;
  if (!zip_archive_list_entries(archive, entries, error)) {
    return false;
  }
  state.members.clear();
  state.members.reserve(entries.size());
  for (const auto& entry : entries) {
    std::string data;
    if (!zip_archive_extract_member(archive, entry, data, error)) {
      return false;
    }
    state.members.push_back(ZipArchiveMember{
        entry.name,
        entry.comment,
        std::move(data),
        entry.method,
        entry.mod_time,
        entry.mod_date,
        entry.crc32,
        entry.compressed_size,
        entry.external_attr});
  }
  return true;
}

bool flush_archive(Runtime& runtime, Value self, ZipFileState& state, std::string& error) {
  Value comment_attr;
  std::string ignored;
  const std::string old_comment = state.comment;
  if (object_get_attr(self, "comment", comment_attr, ignored)) {
    std::string comment_bytes;
    if (get_bytes_like(comment_attr, comment_bytes)) {
      state.comment = std::move(comment_bytes);
    }
  }
  if (!state.dirty && state.comment == old_comment) {
    return true;
  }
  std::string archive;
  if (!zip_archive_build(state.members, state.comment, archive, error)) {
    return false;
  }
  if (state.owns_path) {
    if (!runtime.vfs().write_file(
            state.path,
            reinterpret_cast<const uint8_t*>(archive.data()),
            archive.size(),
            error)) {
      return false;
    }
  } else if (!write_file_like(runtime, state.fileobj, archive, error)) {
    return false;
  }
  state.dirty = false;
  return true;
}

bool replace_or_append_member(ZipFileState& state, ZipArchiveMember member) {
  member.name = normalized_member_name(std::move(member.name));
  if (member.name.empty()) {
    return false;
  }
  const std::string target_name = member.name;
  for (auto& existing : state.members) {
    if (existing.name == target_name) {
      existing = std::move(member);
      state.dirty = true;
      return true;
    }
  }
  state.members.push_back(std::move(member));
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
  object_set_attr(instance, "orig_filename", Value::string(member.name), ignored);
  object_set_attr(instance, "date_time", tuple_from_dos_datetime(member.mod_date, member.mod_time), ignored);
  object_set_attr(instance, "comment", Value::bytes(member.comment), ignored);
  object_set_attr(instance, "file_size", Value::int64(static_cast<int64_t>(member.data.size())), ignored);
  const uint32_t compressed_size = member.compressed_size == 0 ? static_cast<uint32_t>(member.data.size()) : member.compressed_size;
  object_set_attr(instance, "compress_size", Value::int64(static_cast<int64_t>(compressed_size)), ignored);
  object_set_attr(instance, "compress_type", Value::int64(member.method), ignored);
  object_set_attr(instance, "CRC", Value::int64(static_cast<int64_t>(member.crc32)), ignored);
  object_set_attr(instance, "external_attr", Value::int64(static_cast<int64_t>(member.external_attr)), ignored);
  object_set_attr(instance, "create_system", Value::int64(0), ignored);
  object_set_attr(instance, "extract_version", Value::int64(member.method == 8 ? 20 : 10), ignored);
  object_set_attr(instance, "flag_bits", Value::int64(0), ignored);
  return instance;
}

const ZipArchiveMember* find_member(const ZipFileState& state, std::string name) {
  name = normalized_member_name(std::move(name));
  for (const auto& member : state.members) {
    if (member.name == name) {
      return &member;
    }
  }
  return nullptr;
}

ZipArchiveMember member_from_name_data(
    std::string name,
    std::string data,
    uint16_t method,
    uint16_t mod_time = 0,
    uint16_t mod_date = 0,
    std::string comment = {},
    uint32_t external_attr = 0) {
  return ZipArchiveMember{
      normalized_member_name(std::move(name)),
      std::move(comment),
      std::move(data),
      method,
      mod_time,
      mod_date,
      0,
      0,
      external_attr};
}

bool zipinfo_member_fields(const Value& zinfo, ZipArchiveMember& member, std::string& error) {
  Value value;
  std::string ignored;
  if (object_get_attr(zinfo, "filename", value, ignored)) {
    if (!get_string_arg(value, "ZipInfo.filename", member.name, error)) {
      return false;
    }
  }
  if (object_get_attr(zinfo, "compress_type", value, ignored) && value.tag == ValueTag::Int64) {
    member.method = static_cast<uint16_t>(value.as.i64);
  }
  if (object_get_attr(zinfo, "date_time", value, ignored)) {
    member.mod_time = dos_time_from_tuple(value, member.mod_time);
    member.mod_date = dos_date_from_tuple(value, member.mod_date);
  }
  if (object_get_attr(zinfo, "comment", value, ignored)) {
    get_bytes_like(value, member.comment);
  }
  if (object_get_attr(zinfo, "external_attr", value, ignored) && value.tag == ValueTag::Int64) {
    member.external_attr = static_cast<uint32_t>(value.as.i64);
  }
  return true;
}

bool zipfile_is_zipfile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "zipfile.is_zipfile() expected filename";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "zipfile.is_zipfile filename", path, error)) {
    return false;
  }
  std::vector<uint8_t> archive;
  std::string ignored;
  if (!runtime.vfs().read_file(path, archive, ignored)) {
    out = Value::boolean(false);
    return true;
  }
  std::vector<ZipArchiveEntry> entries;
  out = Value::boolean(zip_archive_list_entries(archive, entries, ignored));
  return true;
}

bool zipfile_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 7) {
    error = "ZipFile() expected file, mode, compression, allowZip64, compresslevel, strict_timestamps, metadata_encoding";
    return false;
  }
  std::string path;
  bool owns_path = true;
  Value fileobj = Value::none();
  if (!get_string_arg(args[1], "ZipFile file", path, error)) {
    error.clear();
    owns_path = false;
    fileobj = args[1];
  }
  std::string mode = "r";
  Value mode_value = argc >= 3 ? args[2] : kw_value(kwargs, kwargc, "mode");
  if (mode_value.tag != ValueTag::None && !get_string_arg(mode_value, "ZipFile mode", mode, error)) {
    return false;
  }
  if (mode != "r" && mode != "w" && mode != "a" && mode != "x") {
    error = "ZipFile mode must be 'r', 'w', 'a', or 'x'";
    return false;
  }
  Value compression_value = argc >= 4 ? args[3] : kw_value(kwargs, kwargc, "compression");
  uint16_t compression = compression_from_value(compression_value, 0, error);
  if (compression == UINT16_MAX) {
    return false;
  }
  int compresslevel = -1;
  Value compresslevel_value = argc >= 6 ? args[5] : kw_value(kwargs, kwargc, "compresslevel");
  if (compresslevel_value.tag != ValueTag::None) {
    int64_t level = 0;
    if (!get_int_arg(compresslevel_value, level)) {
      error = "ZipFile compresslevel must be int or None";
      return false;
    }
    compresslevel = static_cast<int>(level);
  }

  auto* state = new ZipFileState();
  state->path = std::move(path);
  state->fileobj = fileobj;
  state->owns_path = owns_path;
  state->mode = std::move(mode);
  state->default_compression = compression;
  state->compresslevel = compresslevel;
  if ((state->mode == "r" || state->mode == "a") && !load_existing_archive(runtime, *state, error)) {
    delete state;
    return false;
  }
  if (!instance_set_native_data(args[0], kZipFileNativeType, state, zipfile_cleanup, error)) {
    delete state;
    return false;
  }
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "filename", owns_path ? Value::string(state->path) : Value::none(), ignored);
  object_set_attr(self, "mode", Value::string(state->mode), ignored);
  object_set_attr(self, "comment", Value::bytes(state->comment), ignored);
  object_set_attr(self, "compression", Value::int64(state->default_compression), ignored);
  object_set_attr(self, "compresslevel", state->compresslevel < 0 ? Value::none() : Value::int64(state->compresslevel), ignored);
  value_set_none(out);
  return true;
}

bool zipfile_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return zipfile_init_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool zipfile_getinfo(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ZipFile.getinfo() expected name";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string name;
  if (!zipfile_member_name_arg(args[1], name, error)) {
    return false;
  }
  name = normalized_member_name(std::move(name));
  if (const auto* member = find_member(*state, name)) {
    out = make_zipinfo(runtime, *member);
    return true;
  }
  error = "There is no item named '" + name + "' in the archive";
  return false;
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
  if (!zipfile_member_name_arg(args[1], name, error)) {
    return false;
  }
  name = normalized_member_name(std::move(name));
  if (const auto* member = find_member(*state, name)) {
    out = Value::bytes(member->data);
    return true;
  }
  error = "There is no item named '" + name + "' in the archive";
  return false;
}

bool zipfile_extract_one(
    Runtime& runtime,
    ZipFileState& state,
    const std::string& member_name,
    const std::string& root,
    Value& out,
    std::string& error) {
  const std::string name = normalized_member_name(member_name);
  for (const auto& member : state.members) {
    if (member.name != name) {
      continue;
    }
    std::string target = root.empty() || root == "." ? member.name : root + "/" + member.name;
    for (auto& ch : target) {
      if (ch == '\\') {
        ch = '/';
      }
    }
    if (!runtime.vfs().write_file(
            target,
            reinterpret_cast<const uint8_t*>(member.data.data()),
            member.data.size(),
            error)) {
      return false;
    }
    out = Value::string(target);
    return true;
  }
  error = "There is no item named '" + name + "' in the archive";
  return false;
}

bool zipfile_mkdir(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "ZipFile.mkdir() expected zinfo_or_directory and optional mode";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->mode == "r") {
    error = "mkdir() requires mode 'w', 'x', or 'a'";
    return false;
  }
  std::string name;
  if (!zipfile_member_name_arg(args[1], name, error)) {
    return false;
  }
  ZipArchiveMember member = member_from_name_data(std::move(name), "", 0);
  if (!zipinfo_member_fields(args[1], member, error)) {
    return false;
  }
  if (!member.name.empty() && member.name.back() != '/') {
    member.name.push_back('/');
  }
  member.external_attr = member.external_attr == 0 ? 0x10u : member.external_attr;
  if (!replace_or_append_member(*state, std::move(member))) {
    error = "ZipFile.mkdir() directory name must not be empty";
    return false;
  }
  value_set_none(out);
  return true;
}

bool zipfile_printdir(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipFile.printdir() expected no arguments";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::printf("File Name                                             Modified             Size\n");
  for (const auto& member : state->members) {
    std::printf("%-46s %04d-%02d-%02d %02d:%02d:%02d %8zu\n",
        member.name.c_str(),
        1980 + ((member.mod_date >> 9) & 0x7f),
        (member.mod_date >> 5) & 0xf,
        member.mod_date & 0x1f,
        (member.mod_time >> 11) & 0x1f,
        (member.mod_time >> 5) & 0x3f,
        (member.mod_time & 0x1f) * 2,
        member.data.size());
  }
  value_set_none(out);
  return true;
}

bool zipfile_extract(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "ZipFile.extract() expected member, optional path, and optional pwd";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string member;
  if (!zipfile_member_name_arg(args[1], member, error)) {
    return false;
  }
  std::string root = ".";
  if (argc >= 3 && args[2].tag != ValueTag::None && !get_string_arg(args[2], "ZipFile.extract path", root, error)) {
    return false;
  }
  return zipfile_extract_one(runtime, *state, member, root, out, error);
}

bool zipfile_extractall(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 4) {
    error = "ZipFile.extractall() expected optional path, members, and pwd";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string root = ".";
  if (argc >= 2 && args[1].tag != ValueTag::None && !get_string_arg(args[1], "ZipFile.extractall path", root, error)) {
    return false;
  }
  if (argc >= 3 && args[2].tag != ValueTag::None) {
    auto* list = value_as_list(args[2]);
    auto* tuple = value_as_tuple(args[2]);
    if (list == nullptr && tuple == nullptr) {
      error = "ZipFile.extractall members must be list or tuple in this foundation";
      return false;
    }
    const auto& items = list != nullptr ? list->items : tuple->items;
    for (const auto& item : items) {
      std::string member;
      if (!zipfile_member_name_arg(item, member, error)) {
        return false;
      }
      Value ignored_out;
      if (!zipfile_extract_one(runtime, *state, member, root, ignored_out, error)) {
        return false;
      }
    }
  } else {
    for (const auto& member : state->members) {
      Value ignored_out;
      if (!zipfile_extract_one(runtime, *state, member.name, root, ignored_out, error)) {
        return false;
      }
    }
  }
  value_set_none(out);
  return true;
}

bool zipfile_testzip(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipFile.testzip() expected no arguments";
    return false;
  }
  if (zipfile_state(args[0], error) == nullptr) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool zipextfile_read(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ZipExtFile.read() expected optional size";
    return false;
  }
  auto* state = zipextfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  size_t remaining = state->data.size() - std::min(state->cursor, state->data.size());
  if (argc == 2 && args[1].tag == ValueTag::Int64 && args[1].as.i64 >= 0) {
    remaining = std::min<size_t>(remaining, static_cast<size_t>(args[1].as.i64));
  }
  out = Value::bytes(state->data.substr(state->cursor, remaining));
  state->cursor += remaining;
  return true;
}

bool zipextfile_readline(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ZipExtFile.readline() expected optional size";
    return false;
  }
  auto* state = zipextfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  size_t limit = state->data.size() - std::min(state->cursor, state->data.size());
  if (argc == 2 && args[1].tag == ValueTag::Int64 && args[1].as.i64 >= 0) {
    limit = std::min<size_t>(limit, static_cast<size_t>(args[1].as.i64));
  }
  size_t count = 0;
  while (count < limit && state->cursor + count < state->data.size()) {
    ++count;
    if (state->data[state->cursor + count - 1] == '\n') {
      break;
    }
  }
  out = Value::bytes(state->data.substr(state->cursor, count));
  state->cursor += count;
  return true;
}

bool zipextfile_close(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipExtFile.close() expected no arguments";
    return false;
  }
  auto* state = static_cast<ZipExtFileState*>(instance_get_native_data(args[0], kZipExtFileNativeType));
  if (state == nullptr) {
    error = "invalid ZipExtFile object";
    return false;
  }
  state->closed = true;
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "closed", Value::boolean(true), ignored);
  value_set_none(out);
  return true;
}

bool zipextfile_enter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipExtFile.__enter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool zipextfile_exit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "ZipExtFile.__exit__() expected exc_type, exc, and tb";
    return false;
  }
  Value ignored;
  if (!zipextfile_close(runtime, args, 1, ignored, error, nullptr)) {
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool zipfile_open_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 6) {
    error = "ZipFile.open() expected name, mode, pwd, and force_zip64";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string mode = "r";
  Value mode_value = argc >= 3 ? args[2] : kw_value(kwargs, kwargc, "mode");
  if (mode_value.tag != ValueTag::None && !get_string_arg(mode_value, "ZipFile.open mode", mode, error)) {
    return false;
  }
  if (mode != "r") {
    error = "ZipFile.open() write mode is not implemented yet";
    return false;
  }
  std::string name;
  if (!zipfile_member_name_arg(args[1], name, error)) {
    return false;
  }
  const auto* member = find_member(*state, name);
  if (member == nullptr) {
    error = "There is no item named '" + name + "' in the archive";
    return false;
  }
  Value module;
  std::string ignored;
  if (!runtime.import_module("zipfile", module, ignored)) {
    return false;
  }
  Value klass;
  if (!module_get_attr(module, "ZipExtFile", klass, ignored)) {
    return false;
  }
  out = Value::instance(klass);
  auto* ext_state = new ZipExtFileState();
  ext_state->data = member->data;
  if (!instance_set_native_data(out, kZipExtFileNativeType, ext_state, zipextfile_cleanup, error)) {
    delete ext_state;
    return false;
  }
  object_set_attr(out, "name", Value::string(member->name), ignored);
  object_set_attr(out, "mode", Value::string("r"), ignored);
  object_set_attr(out, "closed", Value::boolean(false), ignored);
  return true;
}

bool zipfile_open(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return zipfile_open_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool zipfile_writestr_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 3 || argc > 4) {
    error = "ZipFile.writestr() expected zinfo_or_arcname, data, compress_type, and compresslevel";
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
  std::string data;
  if (!get_bytes_like(args[2], data)) {
    error = "ZipFile.writestr data must be str or bytes-like";
    return false;
  }
  std::string name;
  if (!zipfile_member_name_arg(args[1], name, error)) {
    return false;
  }
  ZipArchiveMember member = member_from_name_data(std::move(name), std::move(data), state->default_compression);
  if (!zipinfo_member_fields(args[1], member, error)) {
    return false;
  }
  Value compression_value = argc >= 4 ? args[3] : kw_value(kwargs, kwargc, "compress_type");
  member.method = compression_from_value(compression_value, member.method, error);
  if (member.method == UINT16_MAX) {
    return false;
  }
  if (!replace_or_append_member(*state, std::move(member))) {
    error = "ZipFile.writestr() member name must not be empty";
    return false;
  }
  value_set_none(out);
  return true;
}

bool zipfile_writestr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return zipfile_writestr_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool zipfile_write_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || argc > 4) {
    error = "ZipFile.write() expected filename, arcname, compress_type, and compresslevel";
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
  Value arcname_value = argc >= 3 ? args[2] : kw_value(kwargs, kwargc, "arcname");
  if (arcname_value.tag != ValueTag::None && !get_string_arg(arcname_value, "ZipFile.write arcname", arcname, error)) {
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!runtime.vfs().read_file(filename, bytes, error)) {
    return false;
  }
  std::string data(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  Value compression_value = argc >= 4 ? args[3] : kw_value(kwargs, kwargc, "compress_type");
  const uint16_t method = compression_from_value(compression_value, state->default_compression, error);
  if (method == UINT16_MAX) {
    return false;
  }
  if (!replace_or_append_member(*state, member_from_name_data(std::move(arcname), std::move(data), method))) {
    error = "ZipFile.write() arcname must not be empty";
    return false;
  }
  value_set_none(out);
  return true;
}

bool zipfile_write(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  return zipfile_write_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
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
  if (!state->closed && !flush_archive(runtime, args[0], *state, error)) {
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

bool zipinfo_is_dir(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipInfo.is_dir() expected no arguments";
    return false;
  }
  std::string name;
  if (!zipfile_member_name_arg(args[0], name, error)) {
    return false;
  }
  out = Value::boolean(!name.empty() && (name.back() == '/' || name.back() == '\\'));
  return true;
}

bool zipinfo_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 3) {
    error = "ZipInfo() expected optional filename and date_time";
    return false;
  }
  std::string filename;
  if (argc >= 2 && args[1].tag != ValueTag::None && !get_string_arg(args[1], "ZipInfo filename", filename, error)) {
    return false;
  }
  Value date_time = argc >= 3 && args[2].tag != ValueTag::None
      ? args[2]
      : Value::tuple({
            Value::int64(1980),
            Value::int64(1),
            Value::int64(1),
            Value::int64(0),
            Value::int64(0),
            Value::int64(0)});
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "filename", Value::string(filename), ignored);
  object_set_attr(self, "orig_filename", Value::string(filename), ignored);
  object_set_attr(self, "date_time", date_time, ignored);
  object_set_attr(self, "comment", Value::bytes(""), ignored);
  object_set_attr(self, "file_size", Value::int64(0), ignored);
  object_set_attr(self, "compress_size", Value::int64(0), ignored);
  object_set_attr(self, "compress_type", Value::int64(0), ignored);
  object_set_attr(self, "CRC", Value::int64(0), ignored);
  object_set_attr(self, "external_attr", Value::int64(0), ignored);
  object_set_attr(self, "create_system", Value::int64(0), ignored);
  object_set_attr(self, "extract_version", Value::int64(10), ignored);
  object_set_attr(self, "flag_bits", Value::int64(0), ignored);
  value_set_none(out);
  return true;
}

Value make_zipfile_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"__init__", runtime.make_native_function("zipfile.ZipFile.__init__", zipfile_init, nullptr, nullptr, nullptr, false, zipfile_init_kw)});
  attrs.push_back({"__enter__", runtime.make_native_function("zipfile.ZipFile.__enter__", zipfile_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("zipfile.ZipFile.__exit__", zipfile_exit)});
  attrs.push_back({"close", runtime.make_native_function("zipfile.ZipFile.close", zipfile_close)});
  attrs.push_back({"getinfo", runtime.make_native_function("zipfile.ZipFile.getinfo", zipfile_getinfo)});
  attrs.push_back({"namelist", runtime.make_native_function("zipfile.ZipFile.namelist", zipfile_namelist)});
  attrs.push_back({"infolist", runtime.make_native_function("zipfile.ZipFile.infolist", zipfile_infolist)});
  attrs.push_back({"read", runtime.make_native_function("zipfile.ZipFile.read", zipfile_read)});
  attrs.push_back({"open", runtime.make_native_function("zipfile.ZipFile.open", zipfile_open, nullptr, nullptr, nullptr, false, zipfile_open_kw)});
  attrs.push_back({"write", runtime.make_native_function("zipfile.ZipFile.write", zipfile_write, nullptr, nullptr, nullptr, false, zipfile_write_kw)});
  attrs.push_back({"writestr", runtime.make_native_function("zipfile.ZipFile.writestr", zipfile_writestr, nullptr, nullptr, nullptr, false, zipfile_writestr_kw)});
  attrs.push_back({"mkdir", runtime.make_native_function("zipfile.ZipFile.mkdir", zipfile_mkdir)});
  attrs.push_back({"extract", runtime.make_native_function("zipfile.ZipFile.extract", zipfile_extract)});
  attrs.push_back({"extractall", runtime.make_native_function("zipfile.ZipFile.extractall", zipfile_extractall)});
  attrs.push_back({"testzip", runtime.make_native_function("zipfile.ZipFile.testzip", zipfile_testzip)});
  attrs.push_back({"printdir", runtime.make_native_function("zipfile.ZipFile.printdir", zipfile_printdir)});
  return Value::class_object("ZipFile", std::move(attrs));
}

Value make_zipextfile_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"read", runtime.make_native_function("zipfile.ZipExtFile.read", zipextfile_read)});
  attrs.push_back({"readline", runtime.make_native_function("zipfile.ZipExtFile.readline", zipextfile_readline)});
  attrs.push_back({"close", runtime.make_native_function("zipfile.ZipExtFile.close", zipextfile_close)});
  attrs.push_back({"__enter__", runtime.make_native_function("zipfile.ZipExtFile.__enter__", zipextfile_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("zipfile.ZipExtFile.__exit__", zipextfile_exit)});
  return Value::class_object("ZipExtFile", std::move(attrs));
}

Value make_zipinfo_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"__init__", runtime.make_native_function("zipfile.ZipInfo.__init__", zipinfo_init)});
  attrs.push_back({"is_dir", runtime.make_native_function("zipfile.ZipInfo.is_dir", zipinfo_is_dir)});
  return Value::class_object("ZipInfo", std::move(attrs));
}

} // namespace

void register_zipfile_module(Runtime& runtime) {
  Value bad_zip_file = Value::class_object("BadZipFile", {});
  NativeModuleBuilder builder(runtime, "zipfile");
  builder.function("is_zipfile", zipfile_is_zipfile)
      .value("ZipFile", make_zipfile_class(runtime))
      .value("ZipExtFile", make_zipextfile_class(runtime))
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
