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

#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include <algorithm>

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
  out = string_object_to_string(*reinterpret_cast<StringObject*>(value.as.obj));
  return true;
}

bool get_write_bytes_arg(const Value& value, bool binary, const char* name, std::string& out, std::string& error) {
  if (!binary) {
    return get_string_arg(value, name, out, error);
  }
  if (auto* bytes = value_as_bytes(value)) {
    auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  error = std::string(name) + " must be bytes-like";
  return false;
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

void file_read_result(FileObject& file, std::string data, Value& out) {
  if (file.binary) {
    out = Value::bytes(std::move(data));
  } else {
    out = Value::string(std::move(data));
  }
}

bool file_read_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "file.read() expected optional size";
    return false;
  }
  auto* file = require_file(args[0], "file.read", error);
  if (file == nullptr) {
    return false;
  }
  if (!file->readable) {
    error = "file is not readable";
    return false;
  }
  size_t size = file->buffer.size() - std::min(file->cursor, file->buffer.size());
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      error = "file.read size must be int";
      return false;
    }
    if (args[1].as.i64 >= 0) {
      size = std::min<size_t>(size, static_cast<size_t>(args[1].as.i64));
    }
  }
  const size_t start = std::min(file->cursor, file->buffer.size());
  file_read_result(*file, file->buffer.substr(start, size), out);
  file->cursor = start + size;
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
  if (!get_write_bytes_arg(args[1], file->binary, "file.write data", text, error)) {
    return false;
  }
  if (file->append) {
    file->cursor = file->buffer.size();
  }
  if (file->cursor > file->buffer.size()) {
    file->cursor = file->buffer.size();
  }
  if (file->cursor + text.size() > file->buffer.size()) {
    file->buffer.resize(file->cursor + text.size(), '\0');
  }
  std::copy(text.begin(), text.end(), file->buffer.begin() + static_cast<std::ptrdiff_t>(file->cursor));
  file->cursor += text.size();
  value_set_int64(out, static_cast<int64_t>(text.size()));
  return true;
}

bool file_readline_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "file.readline() expected optional size";
    return false;
  }
  auto* file = require_file(args[0], "file.readline", error);
  if (file == nullptr) {
    return false;
  }
  if (!file->readable) {
    error = "file is not readable";
    return false;
  }
  size_t limit = file->buffer.size();
  if (argc == 2) {
    if (args[1].tag != ValueTag::Int64) {
      error = "file.readline size must be int";
      return false;
    }
    if (args[1].as.i64 >= 0) {
      limit = std::min(file->buffer.size(), file->cursor + static_cast<size_t>(args[1].as.i64));
    }
  }
  const size_t start = std::min(file->cursor, file->buffer.size());
  size_t end = start;
  while (end < limit && end < file->buffer.size()) {
    ++end;
    if (file->buffer[end - 1] == '\n') {
      break;
    }
  }
  file_read_result(*file, file->buffer.substr(start, end - start), out);
  file->cursor = end;
  return true;
}

bool read_line(FileObject& file, Value& out, std::string& error) {
  const size_t start = std::min(file.cursor, file.buffer.size());
  size_t end = start;
  while (end < file.buffer.size()) {
    ++end;
    if (file.buffer[end - 1] == '\n') {
      break;
    }
  }
  file_read_result(file, file.buffer.substr(start, end - start), out);
  file.cursor = end;
  return true;
}

bool file_readlines_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "file.readlines() expected optional hint";
    return false;
  }
  auto* file = require_file(args[0], "file.readlines", error);
  if (file == nullptr) {
    return false;
  }
  if (!file->readable) {
    error = "file is not readable";
    return false;
  }
  std::vector<Value> lines;
  while (file->cursor < file->buffer.size()) {
    Value line;
    if (!read_line(*file, line, error)) {
      return false;
    }
    lines.push_back(std::move(line));
  }
  out = Value::list(std::move(lines));
  return true;
}

bool write_bytes(FileObject& file, const Value& value, Value& out, std::string& error) {
  std::string text;
  if (!get_write_bytes_arg(value, file.binary, "file.write data", text, error)) {
    return false;
  }
  if (file.append) {
    file.cursor = file.buffer.size();
  }
  if (file.cursor > file.buffer.size()) {
    file.cursor = file.buffer.size();
  }
  if (file.cursor + text.size() > file.buffer.size()) {
    file.buffer.resize(file.cursor + text.size(), '\0');
  }
  std::copy(text.begin(), text.end(), file.buffer.begin() + static_cast<std::ptrdiff_t>(file.cursor));
  file.cursor += text.size();
  value_set_int64(out, static_cast<int64_t>(text.size()));
  return true;
}

bool file_writelines_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "file.writelines() expected iterable";
    return false;
  }
  auto* file = require_file(args[0], "file.writelines", error);
  if (file == nullptr) {
    return false;
  }
  if (!file->writable) {
    error = "file is not writable";
    return false;
  }
  Value iterator;
  if (!sequence_get_iter(args[1], iterator, error)) {
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      break;
    }
    Value ignored;
    if (!write_bytes(*file, item, ignored, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool file_seek_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3 || args[1].tag != ValueTag::Int64) {
    error = "file.seek() expected offset and optional whence";
    return false;
  }
  auto* file = require_file(args[0], "file.seek", error);
  if (file == nullptr) {
    return false;
  }
  int64_t base = 0;
  const int64_t whence = argc == 3 && args[2].tag == ValueTag::Int64 ? args[2].as.i64 : 0;
  if (whence == 0) {
    base = 0;
  } else if (whence == 1) {
    base = static_cast<int64_t>(file->cursor);
  } else if (whence == 2) {
    base = static_cast<int64_t>(file->buffer.size());
  } else {
    error = "invalid whence";
    return false;
  }
  int64_t next = base + args[1].as.i64;
  if (next < 0) {
    next = 0;
  }
  file->cursor = static_cast<size_t>(next);
  value_set_int64(out, static_cast<int64_t>(file->cursor));
  return true;
}

bool file_tell_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.tell", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.tell", error);
  if (file == nullptr) {
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(file->cursor));
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

bool file_closed_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.closed", error)) {
    return false;
  }
  if (args[0].tag != ValueTag::Object || args[0].as.obj == nullptr || args[0].as.obj->kind != ObjectKind::File) {
    error = "file.closed target is not a file";
    return false;
  }
  auto* file = reinterpret_cast<FileObject*>(args[0].as.obj);
  value_set_bool(out, file->closed);
  return true;
}

bool file_enter_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.__enter__", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.__enter__", error);
  if (file == nullptr) {
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool file_exit_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 4, "file.__exit__", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.__exit__", error);
  if (file == nullptr || !flush_file(*file, error)) {
    return false;
  }
  file->closed = true;
  out = Value::boolean(false);
  return true;
}

} // namespace

bool file_get_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag != ValueTag::Object || object.as.obj == nullptr || object.as.obj->kind != ObjectKind::File) {
    return false;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"__enter__", "file.__enter__", file_enter_method},
      {"__exit__", "file.__exit__", file_exit_method},
      {"close", "file.close", file_close_method},
      {"closed", "file.closed", file_closed_method},
      {"flush", "file.flush", file_flush_method},
      {"read", "file.read", file_read_method},
      {"readline", "file.readline", file_readline_method},
      {"readlines", "file.readlines", file_readlines_method},
      {"seek", "file.seek", file_seek_method},
      {"tell", "file.tell", file_tell_method},
      {"write", "file.write", file_write_method},
      {"writelines", "file.writelines", file_writelines_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
