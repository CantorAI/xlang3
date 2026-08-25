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
#include <cctype>

namespace xlang3 {

namespace {

constexpr const char* kZipFileNativeType = "zipfile.ZipFile";
constexpr const char* kZipExtFileNativeType = "zipfile.ZipExtFile";
constexpr const char* kZipPathNativeType = "zipfile.Path";

constexpr uint16_t kZipStored = 0;
constexpr uint16_t kZipDeflated = 8;
constexpr uint16_t kZipBzip2 = 12;
constexpr uint16_t kZipLzma = 14;
constexpr uint16_t kZipZstandard = 93;

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
  bool writable = false;
  Value owner;
  std::string member_name;
  uint16_t method = 0;
};

struct ZipPathState {
  Value root;
  std::string at;
};

void zipfile_cleanup(void* data) {
  delete static_cast<ZipFileState*>(data);
}

void zipextfile_cleanup(void* data) {
  delete static_cast<ZipExtFileState*>(data);
}

void zippath_cleanup(void* data) {
  delete static_cast<ZipPathState*>(data);
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

void append_le16(std::string& out, uint16_t value) {
  out.push_back(static_cast<char>(value & 0xffu));
  out.push_back(static_cast<char>((value >> 8u) & 0xffu));
}

void append_le32(std::string& out, uint32_t value) {
  out.push_back(static_cast<char>(value & 0xffu));
  out.push_back(static_cast<char>((value >> 8u) & 0xffu));
  out.push_back(static_cast<char>((value >> 16u) & 0xffu));
  out.push_back(static_cast<char>((value >> 24u) & 0xffu));
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

ZipPathState* zippath_state(const Value& self, std::string& error) {
  auto* state = static_cast<ZipPathState*>(instance_get_native_data(self, kZipPathNativeType));
  if (state == nullptr) {
    error = "invalid zipfile.Path object";
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

std::string sanitized_extract_name(std::string name) {
  name = normalized_member_name(std::move(name));
  while (!name.empty() && name.front() == '/') {
    name.erase(name.begin());
  }
  if (name.size() >= 3 && std::isalpha(static_cast<unsigned char>(name[0])) && name[1] == ':' && name[2] == '/') {
    name = name.substr(3);
  }
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= name.size()) {
    const size_t slash = name.find('/', start);
    std::string part = slash == std::string::npos ? name.substr(start) : name.substr(start, slash - start);
    if (!part.empty() && part != "." && part != "..") {
      parts.push_back(std::move(part));
    }
    if (slash == std::string::npos) {
      break;
    }
    start = slash + 1;
  }
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      out.push_back('/');
    }
    out += parts[i];
  }
  if (!name.empty() && name.back() == '/' && !out.empty() && out.back() != '/') {
    out.push_back('/');
  }
  return out;
}

std::string normalized_dir_prefix(std::string name) {
  name = normalized_member_name(std::move(name));
  if (!name.empty() && name.back() != '/') {
    name.push_back('/');
  }
  return name;
}

std::string path_basename(const std::string& path) {
  std::string text = path;
  while (!text.empty() && text.back() == '/') {
    text.pop_back();
  }
  const size_t pos = text.find_last_of('/');
  return pos == std::string::npos ? text : text.substr(pos + 1);
}

std::string path_suffix(const std::string& path) {
  const std::string name = path_basename(path);
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos || dot == 0 ? std::string{} : name.substr(dot);
}

std::string path_stem(const std::string& path) {
  const std::string name = path_basename(path);
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos || dot == 0 ? name : name.substr(0, dot);
}

Value path_suffixes(const std::string& path) {
  const std::string name = path_basename(path);
  std::vector<Value> suffixes;
  size_t pos = name.find('.');
  while (pos != std::string::npos && pos + 1 < name.size()) {
    suffixes.push_back(Value::string(name.substr(pos)));
    pos = name.find('.', pos + 1);
  }
  return Value::list(std::move(suffixes));
}

std::string path_parent(const std::string& path) {
  std::string text = path;
  while (!text.empty() && text.back() == '/') {
    text.pop_back();
  }
  const size_t pos = text.find_last_of('/');
  return pos == std::string::npos ? std::string{} : text.substr(0, pos + 1);
}

bool wildcard_match(std::string_view pattern, std::string_view text) {
  size_t p = 0;
  size_t t = 0;
  size_t star = std::string_view::npos;
  size_t match = 0;
  while (t < text.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
      ++p;
      ++t;
    } else if (p < pattern.size() && pattern[p] == '*') {
      star = p++;
      match = t;
    } else if (star != std::string_view::npos) {
      p = star + 1;
      t = ++match;
    } else {
      return false;
    }
  }
  while (p < pattern.size() && pattern[p] == '*') {
    ++p;
  }
  return p == pattern.size();
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
  if (value.tag != ValueTag::Int64) {
    error = "compression method must be int";
    return UINT16_MAX;
  }
  if (value.as.i64 == kZipStored || value.as.i64 == kZipDeflated) {
    return static_cast<uint16_t>(value.as.i64);
  }
  if (value.as.i64 == kZipBzip2 || value.as.i64 == kZipLzma || value.as.i64 == kZipZstandard) {
    error = "compression method requires an optional codec that is not enabled in this build";
    return UINT16_MAX;
  }
  error = "That compression method is not supported";
  return UINT16_MAX;
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
  object_set_attr(instance, "compress_level", Value::none(), ignored);
  object_set_attr(instance, "_compresslevel", Value::none(), ignored);
  object_set_attr(instance, "CRC", Value::int64(static_cast<int64_t>(member.crc32)), ignored);
  object_set_attr(instance, "extra", Value::bytes(""), ignored);
  object_set_attr(instance, "external_attr", Value::int64(static_cast<int64_t>(member.external_attr)), ignored);
  object_set_attr(instance, "internal_attr", Value::int64(0), ignored);
  object_set_attr(instance, "create_system", Value::int64(0), ignored);
  object_set_attr(instance, "create_version", Value::int64(20), ignored);
  object_set_attr(instance, "extract_version", Value::int64(member.method == 8 ? 20 : 10), ignored);
  object_set_attr(instance, "reserved", Value::int64(0), ignored);
  object_set_attr(instance, "volume", Value::int64(0), ignored);
  object_set_attr(instance, "header_offset", Value::int64(0), ignored);
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

bool has_dir_prefix(const ZipFileState& state, std::string prefix) {
  prefix = normalized_dir_prefix(std::move(prefix));
  if (prefix.empty()) {
    return true;
  }
  for (const auto& member : state.members) {
    if (member.name == prefix || member.name.compare(0, prefix.size(), prefix) == 0) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> child_names(const ZipFileState& state, std::string prefix) {
  prefix = normalized_dir_prefix(std::move(prefix));
  std::vector<std::string> names;
  for (const auto& member : state.members) {
    if (!prefix.empty() && member.name.compare(0, prefix.size(), prefix) != 0) {
      continue;
    }
    std::string rest = prefix.empty() ? member.name : member.name.substr(prefix.size());
    if (rest.empty()) {
      continue;
    }
    const size_t slash = rest.find('/');
    std::string child = prefix + (slash == std::string::npos ? rest : rest.substr(0, slash + 1));
    if (std::find(names.begin(), names.end(), child) == names.end()) {
      names.push_back(std::move(child));
    }
  }
  return names;
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
  if (state->owns_path && state->mode == "x") {
    VfsStat stat;
    std::string stat_error;
    if (runtime.vfs().stat(state->path, stat, stat_error) && stat.kind != VfsNodeKind::Missing) {
      error = "File exists: '" + state->path + "'";
      runtime.raise_class_error("FileExistsError", error);
      delete state;
      return false;
    }
  }
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
  object_set_attr(self, "debug", Value::int64(0), ignored);
  object_set_attr(self, "fp", owns_path ? Value::string(state->path) : fileobj, ignored);
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
    const std::string clean_name = sanitized_extract_name(member.name);
    if (clean_name.empty()) {
      out = Value::string(root.empty() ? "." : root);
      return true;
    }
    std::string target = root.empty() || root == "." ? clean_name : root + "/" + clean_name;
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

bool zipextfile_write(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ZipExtFile.write() expected data";
    return false;
  }
  auto* state = zipextfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (!state->writable) {
    error = "ZipExtFile is not open for writing";
    return false;
  }
  std::string data;
  if (!get_bytes_like(args[1], data)) {
    error = "ZipExtFile.write() argument must be bytes-like";
    return false;
  }
  state->data += data;
  value_set_int64(out, static_cast<int64_t>(data.size()));
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

bool zipextfile_readlines(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "ZipExtFile.readlines() expected optional hint";
    return false;
  }
  std::vector<Value> lines;
  while (true) {
    Value line;
    if (!zipextfile_readline(runtime, args, 1, line, error, nullptr)) {
      return false;
    }
    auto* bytes = value_as_bytes(line);
    if (bytes == nullptr || bytes_object_view(*bytes).empty()) {
      break;
    }
    lines.push_back(std::move(line));
  }
  out = Value::list(std::move(lines));
  return true;
}

bool zipextfile_tell(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "ZipExtFile.tell() expected no arguments";
    return false;
  }
  auto* state = zipextfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(state->cursor));
  return true;
}

bool zipextfile_seek(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3 || args[1].tag != ValueTag::Int64) {
    error = "ZipExtFile.seek() expected offset and optional whence";
    return false;
  }
  auto* state = zipextfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  const int64_t whence = argc == 3 && args[2].tag == ValueTag::Int64 ? args[2].as.i64 : 0;
  int64_t base = 0;
  if (whence == 1) {
    base = static_cast<int64_t>(state->cursor);
  } else if (whence == 2) {
    base = static_cast<int64_t>(state->data.size());
  } else if (whence != 0) {
    error = "invalid whence";
    return false;
  }
  int64_t next = base + args[1].as.i64;
  if (next < 0) {
    next = 0;
  }
  if (static_cast<size_t>(next) > state->data.size()) {
    next = static_cast<int64_t>(state->data.size());
  }
  state->cursor = static_cast<size_t>(next);
  value_set_int64(out, next);
  return true;
}

bool zipextfile_bool_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "ZipExtFile state method expected no arguments";
    return false;
  }
  auto* state = zipextfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  const char* kind = static_cast<const char*>(user_data);
  if (std::string(kind) == "readable") {
    out = Value::boolean(!state->writable);
  } else if (std::string(kind) == "writable") {
    out = Value::boolean(state->writable);
  } else {
    out = Value::boolean(true);
  }
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
  if (!state->closed && state->writable) {
    ZipFileState* owner_state = zipfile_state(state->owner, error);
    if (owner_state == nullptr) {
      return false;
    }
    if (!replace_or_append_member(
            *owner_state,
            member_from_name_data(state->member_name, std::move(state->data), state->method))) {
      error = "ZipExtFile write target must not be empty";
      return false;
    }
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
  if (mode != "r" && mode != "w" && mode != "x") {
    error = "ZipFile.open() mode must be 'r', 'w', or 'x'";
    return false;
  }
  std::string name;
  if (!zipfile_member_name_arg(args[1], name, error)) {
    return false;
  }
  Value pwd = argc >= 4 ? args[3] : kw_value(kwargs, kwargc, "pwd");
  if (pwd.tag != ValueTag::None) {
    error = "encrypted ZIP members are not supported";
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
  ext_state->writable = mode != "r";
  ext_state->owner = args[0];
  ext_state->member_name = normalized_member_name(name);
  ext_state->method = state->default_compression;
  if (mode == "r") {
    const auto* member = find_member(*state, name);
    if (member == nullptr) {
      delete ext_state;
      error = "There is no item named '" + name + "' in the archive";
      return false;
    }
    ext_state->data = member->data;
  } else if (mode == "x" && find_member(*state, name) != nullptr) {
    delete ext_state;
    error = "File already exists: '" + name + "'";
    return false;
  }
  if (!instance_set_native_data(out, kZipExtFileNativeType, ext_state, zipextfile_cleanup, error)) {
    delete ext_state;
    return false;
  }
  object_set_attr(out, "name", Value::string(ext_state->member_name), ignored);
  object_set_attr(out, "mode", Value::string(mode == "r" ? "rb" : "wb"), ignored);
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

bool zipfile_setpassword(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ZipFile.setpassword() expected pwd";
    return false;
  }
  if (args[1].tag != ValueTag::None && value_as_bytes(args[1]) == nullptr) {
    error = "pwd: expected bytes";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "pwd", args[1], ignored);
  value_set_none(out);
  return true;
}

bool pyzipfile_writepy(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "PyZipFile.writepy() expected pathname, basename, and filterfunc";
    return false;
  }
  auto* state = zipfile_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string pathname;
  if (!get_string_arg(args[1], "PyZipFile.writepy pathname", pathname, error)) {
    return false;
  }
  std::string basename;
  if (argc >= 3 && args[2].tag != ValueTag::None && !get_string_arg(args[2], "PyZipFile.writepy basename", basename, error)) {
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(pathname, stat, error)) {
    return false;
  }
  std::vector<std::string> files;
  if (stat.kind == VfsNodeKind::File) {
    files.push_back(pathname);
  } else if (stat.kind == VfsNodeKind::Directory) {
    std::vector<std::string> names;
    if (!runtime.vfs().list_dir(pathname, names, error)) {
      return false;
    }
    for (const auto& name : names) {
      if (name.size() >= 3 && name.substr(name.size() - 3) == ".py") {
        files.push_back(pathname + "/" + name);
      }
    }
  } else {
    error = "PyZipFile.writepy() pathname not found";
    return false;
  }
  for (const auto& file : files) {
    std::vector<uint8_t> bytes;
    if (!runtime.vfs().read_file(file, bytes, error)) {
      return false;
    }
    std::string member_name = path_basename(file);
    if (member_name.size() >= 3 && member_name.substr(member_name.size() - 3) == ".py") {
      member_name.replace(member_name.size() - 3, 3, ".pyc");
    }
    std::string arcname = basename.empty() ? member_name : basename + "/" + member_name;
    std::string data(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!replace_or_append_member(*state, member_from_name_data(std::move(arcname), std::move(data), state->default_compression))) {
      error = "PyZipFile.writepy() archive name must not be empty";
      return false;
    }
  }
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
  if (!state->closed && !flush_archive(runtime, args[0], *state, error)) {
    return false;
  }
  state->closed = true;
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "fp", Value::none(), ignored);
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
  object_set_attr(self, "compress_level", Value::none(), ignored);
  object_set_attr(self, "_compresslevel", Value::none(), ignored);
  object_set_attr(self, "CRC", Value::int64(0), ignored);
  object_set_attr(self, "extra", Value::bytes(""), ignored);
  object_set_attr(self, "external_attr", Value::int64(0), ignored);
  object_set_attr(self, "internal_attr", Value::int64(0), ignored);
  object_set_attr(self, "create_system", Value::int64(0), ignored);
  object_set_attr(self, "create_version", Value::int64(20), ignored);
  object_set_attr(self, "extract_version", Value::int64(10), ignored);
  object_set_attr(self, "reserved", Value::int64(0), ignored);
  object_set_attr(self, "volume", Value::int64(0), ignored);
  object_set_attr(self, "header_offset", Value::int64(0), ignored);
  object_set_attr(self, "flag_bits", Value::int64(0), ignored);
  value_set_none(out);
  return true;
}

bool zipinfo_for_archive(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "ZipInfo._for_archive() expected archive";
    return false;
  }
  auto* state = zipfile_state(args[1], error);
  if (state == nullptr) {
    return false;
  }
  Value self = args[0];
  Value value;
  std::string ignored;
  if (!object_get_attr(self, "compress_type", value, ignored) || value.tag == ValueTag::None) {
    object_set_attr(self, "compress_type", Value::int64(state->default_compression), ignored);
  }
  if (!object_get_attr(self, "date_time", value, ignored)) {
    object_set_attr(self, "date_time", tuple_from_dos_datetime(0, 0), ignored);
  }
  value_assign_fast(out, self);
  return true;
}

bool zipinfo_file_header(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "ZipInfo.FileHeader() expected optional zip64";
    return false;
  }
  std::string filename;
  if (!zipfile_member_name_arg(args[0], filename, error)) {
    return false;
  }
  uint16_t method = 0;
  uint16_t mod_time = 0;
  uint16_t mod_date = 0;
  uint32_t crc = 0;
  uint32_t compressed_size = 0;
  uint32_t file_size = 0;
  Value value;
  std::string ignored;
  if (object_get_attr(args[0], "compress_type", value, ignored) && value.tag == ValueTag::Int64) {
    method = static_cast<uint16_t>(value.as.i64);
  }
  if (object_get_attr(args[0], "date_time", value, ignored)) {
    mod_time = dos_time_from_tuple(value, 0);
    mod_date = dos_date_from_tuple(value, 0);
  }
  if (object_get_attr(args[0], "CRC", value, ignored) && value.tag == ValueTag::Int64) {
    crc = static_cast<uint32_t>(value.as.i64);
  }
  if (object_get_attr(args[0], "compress_size", value, ignored) && value.tag == ValueTag::Int64) {
    compressed_size = static_cast<uint32_t>(value.as.i64);
  }
  if (object_get_attr(args[0], "file_size", value, ignored) && value.tag == ValueTag::Int64) {
    file_size = static_cast<uint32_t>(value.as.i64);
  }
  std::string header;
  append_le32(header, 0x04034b50u);
  append_le16(header, method == kZipDeflated ? 20 : 10);
  append_le16(header, 0);
  append_le16(header, method);
  append_le16(header, mod_time);
  append_le16(header, mod_date);
  append_le32(header, crc);
  append_le32(header, compressed_size);
  append_le32(header, file_size);
  append_le16(header, static_cast<uint16_t>(filename.size()));
  append_le16(header, 0);
  header += filename;
  out = Value::bytes(std::move(header));
  return true;
}

bool zipinfo_from_file(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "ZipInfo.from_file() expected filename, arcname, and strict_timestamps";
    return false;
  }
  std::string filename;
  if (!get_string_arg(args[1], "ZipInfo.from_file filename", filename, error)) {
    return false;
  }
  std::string arcname = filename;
  if (argc >= 3 && args[2].tag != ValueTag::None && !get_string_arg(args[2], "ZipInfo.from_file arcname", arcname, error)) {
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(filename, stat, error)) {
    return false;
  }
  arcname = normalized_member_name(std::move(arcname));
  while (!arcname.empty() && arcname.front() == '/') {
    arcname.erase(arcname.begin());
  }
  if (stat.kind == VfsNodeKind::Directory && !arcname.empty() && arcname.back() != '/') {
    arcname.push_back('/');
  }
  Value klass = args[0];
  out = Value::instance(klass);
  std::string ignored;
  object_set_attr(out, "filename", Value::string(arcname), ignored);
  object_set_attr(out, "orig_filename", Value::string(arcname), ignored);
  object_set_attr(out, "date_time", Value::tuple({Value::int64(1980), Value::int64(1), Value::int64(1), Value::int64(0), Value::int64(0), Value::int64(0)}), ignored);
  object_set_attr(out, "comment", Value::bytes(""), ignored);
  object_set_attr(out, "file_size", Value::int64(static_cast<int64_t>(stat.size)), ignored);
  object_set_attr(out, "compress_size", Value::int64(0), ignored);
  object_set_attr(out, "compress_type", Value::int64(kZipStored), ignored);
  object_set_attr(out, "compress_level", Value::none(), ignored);
  object_set_attr(out, "_compresslevel", Value::none(), ignored);
  object_set_attr(out, "CRC", Value::int64(0), ignored);
  object_set_attr(out, "extra", Value::bytes(""), ignored);
  object_set_attr(out, "external_attr", Value::int64(stat.kind == VfsNodeKind::Directory ? 0x10 : 0), ignored);
  object_set_attr(out, "internal_attr", Value::int64(0), ignored);
  object_set_attr(out, "create_system", Value::int64(0), ignored);
  object_set_attr(out, "create_version", Value::int64(20), ignored);
  object_set_attr(out, "extract_version", Value::int64(10), ignored);
  object_set_attr(out, "reserved", Value::int64(0), ignored);
  object_set_attr(out, "volume", Value::int64(0), ignored);
  object_set_attr(out, "header_offset", Value::int64(0), ignored);
  object_set_attr(out, "flag_bits", Value::int64(0), ignored);
  return true;
}

Value make_zippath_instance(Runtime& runtime, const Value& root, std::string at) {
  Value module;
  std::string ignored;
  if (!runtime.import_module("zipfile", module, ignored)) {
    return Value::none();
  }
  Value klass;
  if (!module_get_attr(module, "Path", klass, ignored)) {
    return Value::none();
  }
  Value instance = Value::instance(klass);
  auto* state = new ZipPathState();
  state->root = root;
  state->at = normalized_member_name(std::move(at));
  if (!instance_set_native_data(instance, kZipPathNativeType, state, zippath_cleanup, ignored)) {
    delete state;
    return Value::none();
  }
  object_set_attr(instance, "root", root, ignored);
  object_set_attr(instance, "at", Value::string(state->at), ignored);
  object_set_attr(instance, "name", Value::string(path_basename(state->at)), ignored);
  object_set_attr(instance, "filename", Value::string(state->at), ignored);
  object_set_attr(instance, "stem", Value::string(path_stem(state->at)), ignored);
  object_set_attr(instance, "suffix", Value::string(path_suffix(state->at)), ignored);
  object_set_attr(instance, "suffixes", path_suffixes(state->at), ignored);
  return instance;
}

std::string zippath_display_name(const ZipPathState& path) {
  std::string ignored;
  auto* zip = static_cast<ZipFileState*>(instance_get_native_data(path.root, kZipFileNativeType));
  if (zip == nullptr || zip->path.empty()) {
    return path.at;
  }
  if (path.at.empty()) {
    return zip->path + "/";
  }
  return zip->path + "/" + path.at;
}

bool zippath_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "Path() expected root and optional at";
    return false;
  }
  std::string at;
  if (argc >= 3 && args[2].tag != ValueTag::None && !get_string_arg(args[2], "Path at", at, error)) {
    return false;
  }
  auto* state = new ZipPathState();
  state->root = args[1];
  state->at = normalized_member_name(std::move(at));
  if (!instance_set_native_data(args[0], kZipPathNativeType, state, zippath_cleanup, error)) {
    delete state;
    return false;
  }
  Value self = args[0];
  std::string ignored;
  object_set_attr(self, "root", state->root, ignored);
  object_set_attr(self, "at", Value::string(state->at), ignored);
  object_set_attr(self, "name", Value::string(path_basename(state->at)), ignored);
  object_set_attr(self, "filename", Value::string(state->at), ignored);
  object_set_attr(self, "stem", Value::string(path_stem(state->at)), ignored);
  object_set_attr(self, "suffix", Value::string(path_suffix(state->at)), ignored);
  object_set_attr(self, "suffixes", path_suffixes(state->at), ignored);
  value_set_none(out);
  return true;
}

bool zippath_string_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path string method expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  out = Value::string(zippath_display_name(*path));
  return true;
}

bool zippath_name_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.name expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  out = Value::string(path_basename(path->at));
  return true;
}

bool zippath_filename_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.filename expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  out = Value::string(zippath_display_name(*path));
  return true;
}

bool zippath_stem_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.stem expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  out = Value::string(path_stem(path->at));
  return true;
}

bool zippath_suffix_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.suffix expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  out = Value::string(path_suffix(path->at));
  return true;
}

bool zippath_suffixes_getter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.suffixes expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  out = path_suffixes(path->at);
  return true;
}

bool zippath_parent_getter(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.parent expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  out = make_zippath_instance(runtime, path->root, path_parent(path->at));
  return true;
}

bool zippath_joinpath(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "Path.joinpath() expected child names";
    return false;
  }
  auto* state = zippath_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string at = state->at;
  for (uint32_t i = 1; i < argc; ++i) {
    std::string part;
    if (!get_string_arg(args[i], "Path child", part, error)) {
      return false;
    }
    if (!at.empty() && at.back() != '/') {
      at.push_back('/');
    }
    at += normalized_member_name(std::move(part));
  }
  out = make_zippath_instance(runtime, state->root, std::move(at));
  return true;
}

bool zippath_exists(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.exists() expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  auto* zip = zipfile_state(path->root, error);
  if (zip == nullptr) {
    return false;
  }
  out = Value::boolean(find_member(*zip, path->at) != nullptr || has_dir_prefix(*zip, path->at));
  return true;
}

bool zippath_is_file(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.is_file() expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  auto* zip = zipfile_state(path->root, error);
  if (zip == nullptr) {
    return false;
  }
  const auto* member = find_member(*zip, path->at);
  out = Value::boolean(member != nullptr && !(member->name.empty() || member->name.back() == '/'));
  return true;
}

bool zippath_is_dir(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.is_dir() expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  auto* zip = zipfile_state(path->root, error);
  if (zip == nullptr) {
    return false;
  }
  out = Value::boolean(has_dir_prefix(*zip, path->at));
  return true;
}

bool zippath_is_symlink(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.is_symlink() expected no arguments";
    return false;
  }
  if (zippath_state(args[0], error) == nullptr) {
    return false;
  }
  out = Value::boolean(false);
  return true;
}

bool zippath_iterdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.iterdir() expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  auto* zip = zipfile_state(path->root, error);
  if (zip == nullptr) {
    return false;
  }
  std::vector<Value> values;
  for (auto& child : child_names(*zip, path->at)) {
    values.push_back(make_zippath_instance(runtime, path->root, std::move(child)));
  }
  out = Value::list(std::move(values));
  return true;
}

bool zippath_read_bytes(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.read_bytes() expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  auto* zip = zipfile_state(path->root, error);
  if (zip == nullptr) {
    return false;
  }
  const auto* member = find_member(*zip, path->at);
  if (member == nullptr) {
    error = "There is no item named '" + path->at + "' in the archive";
    return false;
  }
  out = Value::bytes(member->data);
  return true;
}

bool zippath_read_text(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  Value bytes;
  if (!zippath_read_bytes(runtime, args, argc, bytes, error, user_data)) {
    return false;
  }
  auto* bytes_object = value_as_bytes(bytes);
  out = bytes_object == nullptr ? Value::string("") : Value::string(bytes_object_to_string(*bytes_object));
  return true;
}

bool zippath_parent(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Path.parent() expected no arguments";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  out = make_zippath_instance(runtime, path->root, path_parent(path->at));
  return true;
}

bool zippath_match(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Path.match() expected pattern";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  std::string pattern;
  if (!get_string_arg(args[1], "Path.match pattern", pattern, error)) {
    return false;
  }
  out = Value::boolean(pattern == path->at || wildcard_match(pattern, path_basename(path->at)) || wildcard_match(pattern, path->at));
  return true;
}

bool zippath_glob_common(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, bool recursive) {
  if (argc != 2) {
    error = "Path.glob() expected pattern";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  auto* zip = zipfile_state(path->root, error);
  if (zip == nullptr) {
    return false;
  }
  std::string pattern;
  if (!get_string_arg(args[1], "Path.glob pattern", pattern, error)) {
    return false;
  }
  const std::string prefix = normalized_dir_prefix(path->at);
  std::vector<Value> values;
  if (!recursive) {
    for (auto child : child_names(*zip, path->at)) {
      std::string leaf = child;
      if (!prefix.empty() && leaf.compare(0, prefix.size(), prefix) == 0) {
        leaf = leaf.substr(prefix.size());
      }
      while (!leaf.empty() && leaf.back() == '/') {
        leaf.pop_back();
      }
      if (wildcard_match(pattern, leaf)) {
        values.push_back(make_zippath_instance(runtime, path->root, std::move(child)));
      }
    }
  } else {
    for (const auto& member : zip->members) {
      if (!prefix.empty() && member.name.compare(0, prefix.size(), prefix) != 0) {
        continue;
      }
      std::string rel = prefix.empty() ? member.name : member.name.substr(prefix.size());
      if (wildcard_match(pattern, rel) || wildcard_match(pattern, path_basename(rel))) {
        values.push_back(make_zippath_instance(runtime, path->root, member.name));
      }
    }
  }
  out = Value::list(std::move(values));
  return true;
}

bool zippath_glob(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return zippath_glob_common(runtime, args, argc, out, error, false);
}

bool zippath_rglob(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return zippath_glob_common(runtime, args, argc, out, error, true);
}

bool zippath_relative_to(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "Path.relative_to() expected other";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  std::string other;
  if (auto* other_path = zippath_state(args[1], error)) {
    other = other_path->at;
  } else {
    error.clear();
    if (!get_string_arg(args[1], "Path.relative_to other", other, error)) {
      return false;
    }
  }
  other = normalized_dir_prefix(std::move(other));
  if (!other.empty() && path->at.compare(0, other.size(), other) == 0) {
    out = Value::string(path->at.substr(other.size()));
    return true;
  }
  out = Value::string(path->at);
  return true;
}

bool zippath_open(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "Path.open() expected optional mode";
    return false;
  }
  auto* path = zippath_state(args[0], error);
  if (path == nullptr) {
    return false;
  }
  Value open_args[3] = {path->root, Value::string(path->at), argc == 2 ? args[1] : Value::string("r")};
  return zipfile_open(runtime, open_args, 3, out, error, nullptr);
}

bool zippath_truediv(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return zippath_joinpath(runtime, args, argc, out, error, nullptr);
}

Value make_zipfile_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"__init__", runtime.make_native_function("zipfile.ZipFile.__init__", zipfile_init, nullptr, nullptr, nullptr, false, zipfile_init_kw)});
  attrs.push_back({"__enter__", runtime.make_native_function("zipfile.ZipFile.__enter__", zipfile_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("zipfile.ZipFile.__exit__", zipfile_exit)});
  attrs.push_back({"close", runtime.make_native_function("zipfile.ZipFile.close", zipfile_close)});
  attrs.push_back({"setpassword", runtime.make_native_function("zipfile.ZipFile.setpassword", zipfile_setpassword)});
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

Value make_pyzipfile_class(Runtime& runtime, const Value& zipfile_class) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"__init__", runtime.make_native_function("zipfile.PyZipFile.__init__", zipfile_init, nullptr, nullptr, nullptr, false, zipfile_init_kw)});
  attrs.push_back({"__enter__", runtime.make_native_function("zipfile.PyZipFile.__enter__", zipfile_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("zipfile.PyZipFile.__exit__", zipfile_exit)});
  attrs.push_back({"close", runtime.make_native_function("zipfile.PyZipFile.close", zipfile_close)});
  attrs.push_back({"setpassword", runtime.make_native_function("zipfile.PyZipFile.setpassword", zipfile_setpassword)});
  attrs.push_back({"getinfo", runtime.make_native_function("zipfile.PyZipFile.getinfo", zipfile_getinfo)});
  attrs.push_back({"namelist", runtime.make_native_function("zipfile.PyZipFile.namelist", zipfile_namelist)});
  attrs.push_back({"infolist", runtime.make_native_function("zipfile.PyZipFile.infolist", zipfile_infolist)});
  attrs.push_back({"read", runtime.make_native_function("zipfile.PyZipFile.read", zipfile_read)});
  attrs.push_back({"open", runtime.make_native_function("zipfile.PyZipFile.open", zipfile_open, nullptr, nullptr, nullptr, false, zipfile_open_kw)});
  attrs.push_back({"write", runtime.make_native_function("zipfile.PyZipFile.write", zipfile_write, nullptr, nullptr, nullptr, false, zipfile_write_kw)});
  attrs.push_back({"writestr", runtime.make_native_function("zipfile.PyZipFile.writestr", zipfile_writestr, nullptr, nullptr, nullptr, false, zipfile_writestr_kw)});
  attrs.push_back({"mkdir", runtime.make_native_function("zipfile.PyZipFile.mkdir", zipfile_mkdir)});
  attrs.push_back({"extract", runtime.make_native_function("zipfile.PyZipFile.extract", zipfile_extract)});
  attrs.push_back({"extractall", runtime.make_native_function("zipfile.PyZipFile.extractall", zipfile_extractall)});
  attrs.push_back({"testzip", runtime.make_native_function("zipfile.PyZipFile.testzip", zipfile_testzip)});
  attrs.push_back({"printdir", runtime.make_native_function("zipfile.PyZipFile.printdir", zipfile_printdir)});
  attrs.push_back({"writepy", runtime.make_native_function("zipfile.PyZipFile.writepy", pyzipfile_writepy)});
  return Value::class_object("PyZipFile", std::move(attrs), zipfile_class);
}

Value make_zipextfile_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"read", runtime.make_native_function("zipfile.ZipExtFile.read", zipextfile_read)});
  attrs.push_back({"readline", runtime.make_native_function("zipfile.ZipExtFile.readline", zipextfile_readline)});
  attrs.push_back({"readlines", runtime.make_native_function("zipfile.ZipExtFile.readlines", zipextfile_readlines)});
  attrs.push_back({"write", runtime.make_native_function("zipfile.ZipExtFile.write", zipextfile_write)});
  attrs.push_back({"tell", runtime.make_native_function("zipfile.ZipExtFile.tell", zipextfile_tell)});
  attrs.push_back({"seek", runtime.make_native_function("zipfile.ZipExtFile.seek", zipextfile_seek)});
  attrs.push_back({"readable", runtime.make_native_function("zipfile.ZipExtFile.readable", zipextfile_bool_method, const_cast<char*>("readable"))});
  attrs.push_back({"writable", runtime.make_native_function("zipfile.ZipExtFile.writable", zipextfile_bool_method, const_cast<char*>("writable"))});
  attrs.push_back({"seekable", runtime.make_native_function("zipfile.ZipExtFile.seekable", zipextfile_bool_method, const_cast<char*>("seekable"))});
  attrs.push_back({"close", runtime.make_native_function("zipfile.ZipExtFile.close", zipextfile_close)});
  attrs.push_back({"__enter__", runtime.make_native_function("zipfile.ZipExtFile.__enter__", zipextfile_enter)});
  attrs.push_back({"__exit__", runtime.make_native_function("zipfile.ZipExtFile.__exit__", zipextfile_exit)});
  return Value::class_object("ZipExtFile", std::move(attrs));
}

Value make_zippath_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"__init__", runtime.make_native_function("zipfile.Path.__init__", zippath_init)});
  attrs.push_back({"__str__", runtime.make_native_function("zipfile.Path.__str__", zippath_string_method)});
  attrs.push_back({"__repr__", runtime.make_native_function("zipfile.Path.__repr__", zippath_string_method)});
  attrs.push_back({"__fspath__", runtime.make_native_function("zipfile.Path.__fspath__", zippath_string_method)});
  attrs.push_back({"joinpath", runtime.make_native_function("zipfile.Path.joinpath", zippath_joinpath)});
  attrs.push_back({"__truediv__", runtime.make_native_function("zipfile.Path.__truediv__", zippath_truediv)});
  attrs.push_back({"filename", Value::property(
                                   runtime.make_native_function("zipfile.Path.filename", zippath_filename_getter),
                                   Value::none(),
                                   Value::none(),
                                   Value::none())});
  attrs.push_back({"name", Value::property(
                               runtime.make_native_function("zipfile.Path.name", zippath_name_getter),
                               Value::none(),
                               Value::none(),
                               Value::none())});
  attrs.push_back({"stem", Value::property(
                               runtime.make_native_function("zipfile.Path.stem", zippath_stem_getter),
                               Value::none(),
                               Value::none(),
                               Value::none())});
  attrs.push_back({"suffix", Value::property(
                                 runtime.make_native_function("zipfile.Path.suffix", zippath_suffix_getter),
                                 Value::none(),
                                 Value::none(),
                                 Value::none())});
  attrs.push_back({"suffixes", Value::property(
                                   runtime.make_native_function("zipfile.Path.suffixes", zippath_suffixes_getter),
                                   Value::none(),
                                   Value::none(),
                                   Value::none())});
  attrs.push_back({"parent", Value::property(
                                 runtime.make_native_function("zipfile.Path.parent", zippath_parent_getter),
                                 Value::none(),
                                 Value::none(),
                                 Value::none())});
  attrs.push_back({"exists", runtime.make_native_function("zipfile.Path.exists", zippath_exists)});
  attrs.push_back({"is_file", runtime.make_native_function("zipfile.Path.is_file", zippath_is_file)});
  attrs.push_back({"is_dir", runtime.make_native_function("zipfile.Path.is_dir", zippath_is_dir)});
  attrs.push_back({"is_symlink", runtime.make_native_function("zipfile.Path.is_symlink", zippath_is_symlink)});
  attrs.push_back({"iterdir", runtime.make_native_function("zipfile.Path.iterdir", zippath_iterdir)});
  attrs.push_back({"read_bytes", runtime.make_native_function("zipfile.Path.read_bytes", zippath_read_bytes)});
  attrs.push_back({"read_text", runtime.make_native_function("zipfile.Path.read_text", zippath_read_text)});
  attrs.push_back({"open", runtime.make_native_function("zipfile.Path.open", zippath_open)});
  attrs.push_back({"match", runtime.make_native_function("zipfile.Path.match", zippath_match)});
  attrs.push_back({"glob", runtime.make_native_function("zipfile.Path.glob", zippath_glob)});
  attrs.push_back({"rglob", runtime.make_native_function("zipfile.Path.rglob", zippath_rglob)});
  attrs.push_back({"relative_to", runtime.make_native_function("zipfile.Path.relative_to", zippath_relative_to)});
  return Value::class_object("Path", std::move(attrs));
}

Value make_zipinfo_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("zipfile")});
  attrs.push_back({"__init__", runtime.make_native_function("zipfile.ZipInfo.__init__", zipinfo_init)});
  attrs.push_back({"is_dir", runtime.make_native_function("zipfile.ZipInfo.is_dir", zipinfo_is_dir)});
  attrs.push_back({"_for_archive", runtime.make_native_function("zipfile.ZipInfo._for_archive", zipinfo_for_archive)});
  attrs.push_back({"FileHeader", runtime.make_native_function("zipfile.ZipInfo.FileHeader", zipinfo_file_header)});
  attrs.push_back({"from_file", Value::class_method(runtime.make_native_function("zipfile.ZipInfo.from_file", zipinfo_from_file))});
  return Value::class_object("ZipInfo", std::move(attrs));
}

} // namespace

void register_zipfile_module(Runtime& runtime) {
  Value bad_zip_file = Value::class_object("BadZipFile", {});
  Value large_zip_file = Value::class_object("LargeZipFile", {});
  Value zipfile_class = make_zipfile_class(runtime);
  NativeModuleBuilder builder(runtime, "zipfile");
  builder.function("is_zipfile", zipfile_is_zipfile)
      .value("ZipFile", zipfile_class)
      .value("PyZipFile", make_pyzipfile_class(runtime, zipfile_class))
      .value("ZipExtFile", make_zipextfile_class(runtime))
      .value("Path", make_zippath_class(runtime))
      .value("ZipInfo", make_zipinfo_class(runtime))
      .value("BadZipFile", bad_zip_file)
      .value("BadZipfile", bad_zip_file)
      .value("LargeZipFile", large_zip_file)
      .value("ZIP_STORED", Value::int64(0))
      .value("ZIP_DEFLATED", Value::int64(8))
      .value("ZIP_BZIP2", Value::int64(12))
      .value("ZIP_LZMA", Value::int64(14))
      .value("ZIP_ZSTANDARD", Value::int64(93))
      .value("ZIP64_LIMIT", Value::int64(2147483647))
      .value("ZIP_FILECOUNT_LIMIT", Value::int64(65535))
      .value("ZIP_MAX_COMMENT", Value::int64(65535));
  runtime.register_module("zipfile", builder.finish());
}

} // namespace xlang3
