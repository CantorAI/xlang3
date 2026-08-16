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
#include "xlang3/builtin_methods.h"

#include "xlang3/vfs.h"

namespace xlang3 {
namespace {

FileObject* require_file(const Value& value, const char* name, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::File) {
    error = std::string(name) + " target is not a file";
    return nullptr;
  }
  auto* file = reinterpret_cast<FileObject*>(value.as.obj);
  if (file->closed) {
    error = std::string(name) + " on closed file";
    return nullptr;
  }
  return file;
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = std::string(name) + " must be a string";
    return false;
  }
  out = reinterpret_cast<StringObject*>(value.as.obj)->value;
  return true;
}

bool flush_file(FileObject& file, std::string& error) {
  if (!file.writable) {
    return true;
  }
  if (file.fs == nullptr) {
    error = "file has no filesystem";
    return false;
  }
  return file.fs->write_file(
      file.path,
      reinterpret_cast<const uint8_t*>(file.buffer.data()),
      file.buffer.size(),
      error);
}

bool file_read_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.read", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.read", error);
  if (file == nullptr) {
    return false;
  }
  out = Value::string(file->buffer.substr(file->cursor));
  file->cursor = file->buffer.size();
  return true;
}

bool file_write_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 2, "file.write", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.write", error);
  if (file == nullptr) {
    return false;
  }
  if (!file->writable) {
    error = "file is not writable";
    return false;
  }
  std::string text;
  if (!get_string_arg(args[1], "file.write data", text, error)) {
    return false;
  }
  if (file->cursor > file->buffer.size()) {
    file->cursor = file->buffer.size();
  }
  file->buffer.insert(file->cursor, text);
  file->cursor += text.size();
  value_set_int64(out, static_cast<int64_t>(text.size()));
  return true;
}

bool file_flush_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.flush", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.flush", error);
  if (file == nullptr || !flush_file(*file, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool file_close_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.close", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.close", error);
  if (file == nullptr || !flush_file(*file, error)) {
    return false;
  }
  file->closed = true;
  value_set_none(out);
  return true;
}

} // namespace

bool file_get_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag != ValueTag::Object || object.as.obj == nullptr || object.as.obj->kind != ObjectKind::File) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"close", "file.close", file_close_method},
      {"flush", "file.flush", file_flush_method},
      {"read", "file.read", file_read_method},
      {"write", "file.write", file_write_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
