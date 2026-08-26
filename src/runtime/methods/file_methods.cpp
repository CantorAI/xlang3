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

#include "xlang3/functional_iterators.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include <algorithm>
#include <cctype>

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

std::string normalize_name(std::string text) {
  for (char& ch : text) {
    if (ch == '-' || ch == ' ' || ch == '.') {
      ch = '_';
    } else {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
  }
  if (text == "utf8" || text == "u8" || text == "cp65001") {
    return "utf_8";
  }
  if (text == "latin1" || text == "latin_1" || text == "iso8859_1" || text == "iso_8859_1") {
    return "latin_1";
  }
  if (text == "us_ascii" || text == "646") {
    return "ascii";
  }
  return text;
}

std::string translate_newlines_for_write(std::string text, const FileObject& file) {
  if (file.newline_is_none || file.newline.empty() || file.newline == "\n") {
    return text;
  }
  std::string out;
  out.reserve(text.size());
  for (char ch : text) {
    if (ch == '\n') {
      out += file.newline;
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

uint32_t decode_utf8_codepoint(std::string_view text, size_t width) {
  if (width == 1) {
    return static_cast<unsigned char>(text[0]);
  }
  uint32_t codepoint = static_cast<unsigned char>(text[0]) & ((1u << (7 - width)) - 1u);
  for (size_t i = 1; i < width; ++i) {
    codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i]) & 0x3fu);
  }
  return codepoint;
}

bool encode_text_buffer(const FileObject& file, std::string& out, std::string& error) {
  const std::string encoding = normalize_name(file.encoding);
  const std::string errors = normalize_name(file.errors);
  const std::string text = translate_newlines_for_write(file.buffer, file);
  if (encoding == "utf_8" || encoding == "utf_8_sig") {
    out = encoding == "utf_8_sig" ? std::string("\xef\xbb\xbf", 3) + text : text;
    return true;
  }
  if (encoding == "ascii") {
    for (size_t i = 0; i < text.size();) {
      const unsigned char ch = static_cast<unsigned char>(text[i]);
      const size_t width = utf8_codepoint_width(ch);
      const uint32_t codepoint = width == 0 || i + width > text.size() ? ch : decode_utf8_codepoint(std::string_view(text).substr(i), width);
      const size_t advance = width == 0 ? 1 : width;
      if (codepoint < 128) {
        out.push_back(static_cast<char>(codepoint));
      } else if (errors == "ignore") {
      } else if (errors == "replace") {
        out.push_back('?');
      } else {
        error = "ascii codec can't encode character";
        return false;
      }
      i += advance;
    }
    return true;
  }
  if (encoding == "latin_1") {
    for (size_t i = 0; i < text.size();) {
      const unsigned char ch = static_cast<unsigned char>(text[i]);
      const size_t width = utf8_codepoint_width(ch);
      const uint32_t codepoint = width == 0 || i + width > text.size() ? ch : decode_utf8_codepoint(std::string_view(text).substr(i), width);
      const size_t advance = width == 0 ? 1 : width;
      if (codepoint <= 0xff) {
        out.push_back(static_cast<char>(codepoint));
      } else if (errors == "ignore") {
      } else if (errors == "replace") {
        out.push_back('?');
      } else {
        error = "latin-1 codec can't encode character";
        return false;
      }
      i += advance;
    }
    return true;
  }
  error = "unsupported file encoding: " + file.encoding;
  return false;
}

bool flush_file(FileObject& file, std::string& error) {
  if (file.devnull) {
    return true;
  }
  if (!file.writable) {
    return true;
  }
  if (file.fs == nullptr) {
    error = "file has no filesystem";
    return false;
  }
  std::string storage;
  const std::string& bytes = file.binary ? file.buffer : storage;
  if (!file.binary && !encode_text_buffer(file, storage, error)) {
    return false;
  }
  return file.fs->write_file(
      file.path,
      reinterpret_cast<const uint8_t*>(bytes.data()),
      bytes.size(),
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
  if (file->devnull) {
    std::string text;
    if (!get_write_bytes_arg(args[1], file->binary, "file.write data", text, error)) {
      return false;
    }
    const size_t written = file->binary ? text.size() : utf8_codepoint_count(text);
    value_set_int64(out, static_cast<int64_t>(written));
    return true;
  }
  std::string text;
  if (!get_write_bytes_arg(args[1], file->binary, "file.write data", text, error)) {
    return false;
  }
  const size_t written = file->binary ? text.size() : utf8_codepoint_count(text);
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
  value_set_int64(out, static_cast<int64_t>(written));
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

bool file_writelines_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
  if (file->devnull) {
    value_set_none(out);
    return true;
  }
  Value iterator;
  if (!runtime_get_iter(runtime, args[1], iterator, error)) {
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

bool file_readable_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.readable", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.readable", error);
  if (file == nullptr) {
    return false;
  }
  value_set_bool(out, file->readable);
  return true;
}

bool file_writable_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.writable", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.writable", error);
  if (file == nullptr) {
    return false;
  }
  value_set_bool(out, file->writable);
  return true;
}

bool file_seekable_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.seekable", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.seekable", error);
  if (file == nullptr) {
    return false;
  }
  value_set_bool(out, true);
  return true;
}

bool file_isatty_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.isatty", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.isatty", error);
  if (file == nullptr) {
    return false;
  }
  value_set_bool(out, false);
  return true;
}

bool file_fileno_method(Runtime& runtime, const Value* args, uint32_t argc, Value&, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.fileno", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.fileno", error);
  if (file == nullptr) {
    return false;
  }
  error = "fileno";
  runtime.raise_class_error("OSError", error);
  return false;
}

bool file_truncate_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "file.truncate() expected optional size";
    return false;
  }
  auto* file = require_file(args[0], "file.truncate", error);
  if (file == nullptr) {
    return false;
  }
  if (!file->writable) {
    error = "file is not writable";
    return false;
  }
  size_t size = file->cursor;
  if (argc == 2) {
    if (args[1].tag == ValueTag::None) {
      size = file->cursor;
    } else if (args[1].tag == ValueTag::Int64 && args[1].as.i64 >= 0) {
      size = static_cast<size_t>(args[1].as.i64);
    } else {
      error = "file.truncate size must be a non-negative int or None";
      return false;
    }
  }
  file->buffer.resize(size, '\0');
  if (file->cursor > size) {
    file->cursor = size;
  }
  value_set_int64(out, static_cast<int64_t>(size));
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

bool file_iter_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.__iter__", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.__iter__", error);
  if (file == nullptr) {
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool file_next_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!method_check_argc(argc, 1, "file.__next__", error)) {
    return false;
  }
  auto* file = require_file(args[0], "file.__next__", error);
  if (file == nullptr) {
    return false;
  }
  if (file->cursor >= file->buffer.size()) {
    error = "StopIteration";
    runtime.raise_class_error("StopIteration", "");
    return false;
  }
  return read_line(*file, out, error);
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
  auto* file = reinterpret_cast<FileObject*>(object.as.obj);
  if (name == "name") {
    out = Value::string(file->path);
    return true;
  }
  if (name == "mode") {
    out = Value::string(file->mode);
    return true;
  }
  if (name == "closed") {
    value_set_bool(out, file->closed);
    return true;
  }
  if (name == "encoding") {
    out = file->binary ? Value::none() : Value::string(file->encoding);
    return true;
  }
  if (name == "errors") {
    out = file->binary ? Value::none() : Value::string(file->errors);
    return true;
  }
  if (name == "newlines") {
    out = Value::none();
    return true;
  }
  static constexpr BuiltinMethodSpec methods[] = {
      {"__enter__", "file.__enter__", file_enter_method},
      {"__exit__", "file.__exit__", file_exit_method},
      {"__iter__", "file.__iter__", file_iter_method},
      {"__next__", "file.__next__", file_next_method},
      {"close", "file.close", file_close_method},
      {"fileno", "file.fileno", file_fileno_method},
      {"flush", "file.flush", file_flush_method},
      {"isatty", "file.isatty", file_isatty_method},
      {"read", "file.read", file_read_method},
      {"readline", "file.readline", file_readline_method},
      {"readlines", "file.readlines", file_readlines_method},
      {"readable", "file.readable", file_readable_method},
      {"seek", "file.seek", file_seek_method},
      {"seekable", "file.seekable", file_seekable_method},
      {"tell", "file.tell", file_tell_method},
      {"truncate", "file.truncate", file_truncate_method},
      {"write", "file.write", file_write_method},
      {"writable", "file.writable", file_writable_method},
      {"writelines", "file.writelines", file_writelines_method},
  };
  return bind_builtin_method_from_table(object, name, methods, std::size(methods), out);
}

} // namespace xlang3
