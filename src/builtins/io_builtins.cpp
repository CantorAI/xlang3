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

#include "xlang3/attribute.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/vfs.h"
#include "source_encoding.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace xlang3 {

namespace {

#if defined(_WIN32)
void ignore_open_invalid_parameter(
    const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {}

bool descriptor_is_valid(int fd) {
  struct _stat64 stat_buffer {};
  const auto previous =
      _set_thread_local_invalid_parameter_handler(ignore_open_invalid_parameter);
  const bool valid = _fstat64(fd, &stat_buffer) == 0;
  _set_thread_local_invalid_parameter_handler(previous);
  return valid;
}
#else
bool descriptor_is_valid(int fd) {
  struct stat stat_buffer {};
  return ::fstat(fd, &stat_buffer) == 0;
}
#endif

bool raise_bad_file_descriptor(Runtime& runtime, std::string& error) {
  error = "Bad file descriptor";
  Value exception = runtime.make_exception("OSError", error);
  std::string ignored;
  object_set_attr(exception, "errno", Value::int64(9), ignored);
  object_set_attr(exception, "strerror", Value::string(error), ignored);
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool warn_bool_file_descriptor(Runtime& runtime, std::string& error) {
  Value warnings;
  Value warn;
  const Value* warning_class = runtime.find_builtin("RuntimeWarning");
  if (warning_class == nullptr ||
      !runtime.import_module("warnings", warnings, error) ||
      !module_get_attr(warnings, "warn", warn, error)) {
    return false;
  }
  Value warning_args[] = {
      Value::string("bool is used as a file descriptor"),
      *warning_class,
  };
  Value ignored;
  return runtime_call_callable(runtime, warn, warning_args, 2, ignored, error);
}

bool warn_binary_line_buffering(Runtime& runtime, std::string& error) {
  Value warnings;
  Value warn;
  const Value* warning_class = runtime.find_builtin("RuntimeWarning");
  if (warning_class == nullptr ||
      !runtime.import_module("warnings", warnings, error) ||
      !module_get_attr(warnings, "warn", warn, error)) {
    return false;
  }
  Value warning_args[] = {
      Value::string("line buffering isn't supported in binary mode"),
      *warning_class,
  };
  Value ignored;
  return runtime_call_callable(runtime, warn, warning_args, 2, ignored, error);
}

bool raise_file_not_found(Runtime& runtime, const std::string& path, std::string& error) {
  error = "file not found: " + path;
  Value exception = runtime.make_exception("FileNotFoundError", error);
  std::string ignored;
  object_set_attr(exception, "errno", Value::int64(2), ignored);
  object_set_attr(exception, "strerror", Value::string("No such file or directory"), ignored);
  object_set_attr(exception, "filename", Value::string(path), ignored);
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool raise_open_os_error(
    Runtime& runtime,
    const std::string& path,
    int error_number,
    std::string& error) {
  const char* exception_name = "OSError";
  if (error_number == ENOENT) {
    exception_name = "FileNotFoundError";
  } else if (error_number == EACCES) {
    exception_name = "PermissionError";
  } else if (error_number == EEXIST) {
    exception_name = "FileExistsError";
#if defined(EISDIR)
  } else if (error_number == EISDIR) {
    exception_name = "IsADirectoryError";
#endif
  }
  const char* description = std::strerror(error_number);
  error = description != nullptr ? description : "cannot open file";
  Value exception = runtime.make_exception(exception_name, error);
  std::string ignored;
  object_set_attr(exception, "errno", Value::int64(error_number), ignored);
  object_set_attr(exception, "strerror", Value::string(error), ignored);
  object_set_attr(exception, "filename", Value::string(path), ignored);
  runtime.set_pending_exception(std::move(exception));
  return false;
}

struct OpenMode {
  bool readable = false;
  bool writable = false;
  bool append = false;
  bool create = false;
  bool truncate = false;
  bool exclusive = false;
  bool update = false;
  bool binary = false;
};

struct OpenOptions {
  std::string encoding = "utf-8";
  std::string errors = "strict";
  std::string newline;
  bool newline_is_none = true;
  bool encoding_non_none = false;
  bool errors_non_none = false;
  bool newline_non_none = false;
  int64_t buffering = -1;
  bool closefd = true;
  Value opener = Value::none();
};

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
  if (text == "locale") {
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

bool append_utf8(uint32_t codepoint, std::string& out) {
  if (codepoint <= 0x7f) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  }
  return true;
}

std::string normalize_newlines_for_read(std::string text, const OpenOptions& options) {
  if (!options.newline_is_none) {
    return text;
  }
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      if (i + 1 < text.size() && text[i + 1] == '\n') {
        ++i;
      }
      out.push_back('\n');
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

bool decode_file_text(const std::string& bytes, const OpenOptions& options, std::string& out, std::string& error) {
  const std::string encoding = normalize_name(options.encoding);
  const std::string errors = normalize_name(options.errors);
  std::string decoded;
  if (encoding == "utf_8" || encoding == "utf_8_sig") {
    size_t start = 0;
    if (encoding == "utf_8_sig" && bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xef &&
        static_cast<unsigned char>(bytes[1]) == 0xbb &&
        static_cast<unsigned char>(bytes[2]) == 0xbf) {
      start = 3;
    }
    for (size_t i = start; i < bytes.size();) {
      const unsigned char lead = static_cast<unsigned char>(bytes[i]);
      if (lead < 0x80u) {
        decoded.push_back(static_cast<char>(lead));
        ++i;
        continue;
      }
      const size_t width = utf8_codepoint_width(lead);
      bool valid = width >= 2 && i + width <= bytes.size();
      if (valid) {
        for (size_t j = 1; j < width; ++j) {
          if ((static_cast<unsigned char>(bytes[i + j]) & 0xc0u) != 0x80u) {
            valid = false;
            break;
          }
        }
      }
      if (valid) {
        uint32_t codepoint = lead & (0x7fu >> width);
        for (size_t j = 1; j < width; ++j) {
          codepoint = (codepoint << 6) |
              (static_cast<unsigned char>(bytes[i + j]) & 0x3fu);
        }
        const uint32_t minimum = width == 2 ? 0x80u : width == 3 ? 0x800u : 0x10000u;
        valid = codepoint >= minimum && codepoint <= 0x10ffffu &&
            !(codepoint >= 0xd800u && codepoint <= 0xdfffu);
      }
      if (valid) {
        decoded.append(bytes, i, width);
        i += width;
      } else if (errors == "ignore") {
        ++i;
      } else if (errors == "replace") {
        decoded += "\xef\xbf\xbd";
        ++i;
      } else {
        error = "utf-8 codec can't decode byte";
        return false;
      }
    }
  } else if (encoding == "latin_1") {
    for (unsigned char ch : bytes) {
      append_utf8(ch, decoded);
    }
  } else if (encoding == "ascii") {
    for (unsigned char ch : bytes) {
      if (ch < 128) {
        decoded.push_back(static_cast<char>(ch));
      } else if (errors == "ignore") {
        continue;
      } else if (errors == "replace") {
        decoded += "\xef\xbf\xbd";
      } else {
        error = "ascii codec can't decode byte";
        return false;
      }
    }
  } else if (encoding == "gbk" || encoding == "cp936") {
    if (!decode_gbk_bytes(bytes, decoded, error)) {
      return false;
    }
  } else {
    error = "unsupported file encoding: " + options.encoding;
    return false;
  }
  out = normalize_newlines_for_read(std::move(decoded), options);
  return true;
}

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = std::string(name) + " must be a string";
    return false;
  }
  out = string_object_to_string(*reinterpret_cast<StringObject*>(value.as.obj));
  return true;
}

bool get_path_arg(Runtime& runtime, const Value& value, const char* name, std::string& out, std::string& error) {
  if (get_string_arg(value, name, out, error)) {
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_to_string(*bytes);
    return true;
  }
  std::string ignored;
  Value path_value;
  if (object_get_attr(value, "__fspath__", path_value, ignored) &&
      path_value.tag != ValueTag::None) {
    Value result;
    std::string call_error;
    if (!runtime_call_callable(runtime, path_value, nullptr, 0, result, call_error)) {
      error = call_error.empty() ? std::string(name) + " __fspath__ failed" : call_error;
      return false;
    }
    if (get_string_arg(result, name, out, error)) {
      return true;
    }
    if (auto* bytes = value_as_bytes(result)) {
      out = bytes_object_to_string(*bytes);
      return true;
    }
    error = "expected " + std::string(value_binary_type_name(value)) +
        ".__fspath__() to return str or bytes, not " +
        value_binary_type_name(result);
    return false;
  }
  if (object_get_attr(value, "__xlang3_string_value__", path_value, ignored) && value_as_string(path_value) != nullptr) {
    out = string_object_to_string(*value_as_string(path_value));
    error.clear();
    return true;
  }
  if (object_get_attr(value, "_path", path_value, ignored) && value_as_string(path_value) != nullptr) {
    out = string_object_to_string(*value_as_string(path_value));
    error.clear();
    return true;
  }
  error = "expected str, bytes or os.PathLike object, not " +
      std::string(value_binary_type_name(value));
  return false;
}

bool parse_open_mode(const std::string& mode, OpenMode& out, std::string& error) {
  if (mode.empty()) {
    error = "empty open mode";
    return false;
  }
  bool saw_action = false;
  bool saw_text = false;
  bool saw_binary = false;
  for (char ch : mode) {
    switch (ch) {
      case 'r':
        if (saw_action) {
          error = "must have exactly one of create/read/write/append mode";
          return false;
        }
        saw_action = true;
        out.readable = true;
        break;
      case 'w':
        if (saw_action) {
          error = "must have exactly one of create/read/write/append mode";
          return false;
        }
        saw_action = true;
        out.writable = true;
        out.create = true;
        out.truncate = true;
        break;
      case 'a':
        if (saw_action) {
          error = "must have exactly one of create/read/write/append mode";
          return false;
        }
        saw_action = true;
        out.writable = true;
        out.create = true;
        out.append = true;
        break;
      case 'x':
        if (saw_action) {
          error = "must have exactly one of create/read/write/append mode";
          return false;
        }
        saw_action = true;
        out.writable = true;
        out.create = true;
        out.exclusive = true;
        break;
      case '+':
        if (out.update) {
          error = "invalid mode: " + mode;
          return false;
        }
        out.update = true;
        break;
      case 't':
        if (saw_binary) {
          error = "can't have text and binary mode at once";
          return false;
        }
        if (saw_text) {
          error = "invalid mode: " + mode;
          return false;
        }
        saw_text = true;
        break;
      case 'b':
        if (saw_text) {
          error = "can't have text and binary mode at once";
          return false;
        }
        if (saw_binary) {
          error = "invalid mode: " + mode;
          return false;
        }
        saw_binary = true;
        out.binary = true;
        break;
      default:
        error = "invalid mode: " + mode;
        return false;
    }
  }
  if (!saw_action) {
    error = "invalid mode: " + mode;
    return false;
  }
  if (out.update) {
    out.readable = true;
    out.writable = true;
  }
  return true;
}

void initialize_fd_append_position(FileObject& file, bool append) {
  file.append = append;
  if (!append || !file.fd_backed) {
    return;
  }
#if defined(_WIN32)
  const __int64 position = _lseeki64(file.fd, 0, SEEK_END);
#else
  const off_t position = lseek(file.fd, 0, SEEK_END);
#endif
  if (position >= 0) {
    file.cursor = static_cast<size_t>(position);
  }
}

bool apply_open_option(const std::string& key, const Value& value, OpenOptions& options, std::string& error) {
  if (key == "buffering") {
    if (value.tag != ValueTag::Int64) {
      error = "open buffering must be int";
      return false;
    }
    options.buffering = value.as.i64;
    return true;
  }
  if (key == "encoding") {
    if (value.tag == ValueTag::None) {
      options.encoding = "utf-8";
      options.encoding_non_none = false;
      return true;
    }
    if (value_as_string(value) == nullptr) {
      error = "open encoding must be str or None";
      return false;
    }
    options.encoding = string_object_to_string(*value_as_string(value));
    options.encoding_non_none = true;
    return true;
  }
  if (key == "errors") {
    if (value.tag == ValueTag::None) {
      options.errors = "strict";
      options.errors_non_none = false;
      return true;
    }
    if (value_as_string(value) == nullptr) {
      error = "open errors must be str or None";
      return false;
    }
    options.errors = string_object_to_string(*value_as_string(value));
    options.errors_non_none = true;
    return true;
  }
  if (key == "newline") {
    if (value.tag == ValueTag::None) {
      options.newline_is_none = true;
      options.newline.clear();
      options.newline_non_none = false;
      return true;
    }
    if (value_as_string(value) == nullptr) {
      error = "open newline must be str or None";
      return false;
    }
    options.newline_is_none = false;
    options.newline = string_object_to_string(*value_as_string(value));
    options.newline_non_none = true;
    if (!(options.newline.empty() || options.newline == "\n" || options.newline == "\r" || options.newline == "\r\n")) {
      error = "illegal newline value";
      return false;
    }
    return true;
  }
  if (key == "closefd") {
    if (value.tag != ValueTag::Bool) {
      error = "open closefd must be bool";
      return false;
    }
    options.closefd = value.as.b;
    return true;
  }
  if (key == "opener") {
    value_assign_fast(options.opener, value);
    return true;
  }
  error = "open got unsupported keyword argument '" + key + "'";
  return false;
}

bool apply_open_positional_options(const Value* args, uint32_t argc, OpenOptions& options, std::string& error) {
  static constexpr const char* kNames[] = {"buffering", "encoding", "errors", "newline", "closefd", "opener"};
  for (uint32_t i = 2; i < argc; ++i) {
    if (!apply_open_option(kNames[i - 2], args[i], options, error)) {
      return false;
    }
  }
  return true;
}

bool is_devnull_path(const std::string& path) {
#if defined(_WIN32)
  return path == "NUL" || path == "nul";
#else
  return path == "/dev/null";
#endif
}

bool print_value_text(Runtime& runtime, const Value& value, std::string& out, std::string& error) {
  Value text_value;
  if (!builtin_str_from_value(runtime, value, text_value, error)) {
    return false;
  }
  auto* text = value_as_string(text_value);
  if (text == nullptr) {
    error = "str() returned non-string";
    return false;
  }
  out = string_object_to_string(*text);
  return true;
}

bool print_write_text(Runtime& runtime, const Value& file, const std::string& text, std::string& error) {
  if (file.tag == ValueTag::None) {
    Value sys;
    Value stdout_stream;
    if (!runtime.import_module("sys", sys, error) ||
        !module_get_attr(sys, "stdout", stdout_stream, error)) {
      return false;
    }
    if (stdout_stream.tag == ValueTag::None) {
      error = "lost sys.stdout";
      runtime.raise_class_error("RuntimeError", error);
      return false;
    }
    return print_write_text(runtime, stdout_stream, text, error);
  }
  Value write_method;
  if (!attribute_get(file, "write", write_method, error)) {
    error = "print file must have a write method";
    return false;
  }
  Value text_arg = Value::string(text);
  Value ignored;
  return runtime_call_callable(runtime, write_method, &text_arg, 1, ignored, error);
}

bool print_flush_file(Runtime& runtime, const Value& file, std::string& error) {
  if (file.tag == ValueTag::None) {
    return true;
  }
  Value flush_method;
  if (!attribute_get(file, "flush", flush_method, error)) {
    error = "print file must have a flush method";
    return false;
  }
  Value ignored;
  return runtime_call_callable(runtime, flush_method, nullptr, 0, ignored, error);
}

bool builtin_print(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)user_data;
  std::string output;
  for (uint32_t i = 0; i < argc; ++i) {
    if (i != 0) output.push_back(' ');
    std::string text;
    if (!print_value_text(runtime, args[i], text, error)) {
      return false;
    }
    output += text;
  }
  output.push_back('\n');
  if (!print_write_text(runtime, Value::none(), output, error)) return false;
  value_set_none(out);
  return true;
}

bool builtin_print_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  std::string sep = " ";
  std::string end = "\n";
  Value file;
  value_set_none(file);
  bool flush = false;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const char* name = kwargs[i].name;
    const Value* value = kwargs[i].value;
    if (name == nullptr || value == nullptr) {
      error = "print keyword argument is invalid";
      return false;
    }
    if (std::string(name) == "sep") {
      if (value->tag == ValueTag::None) {
        sep = " ";
      } else if (!get_string_arg(*value, "print sep", sep, error)) {
        return false;
      }
    } else if (std::string(name) == "end") {
      if (value->tag == ValueTag::None) {
        end = "\n";
      } else if (!get_string_arg(*value, "print end", end, error)) {
        return false;
      }
    } else if (std::string(name) == "file") {
      file = *value;
    } else if (std::string(name) == "flush") {
      flush = value_truthy(*value);
    } else {
      error = std::string("print got unexpected keyword argument '") + name + "'";
      return false;
    }
  }
  std::string output;
  for (uint32_t i = 0; i < argc; ++i) {
    if (i != 0) {
      output += sep;
    }
    std::string text;
    if (!print_value_text(runtime, args[i], text, error)) {
      return false;
    }
    output += text;
  }
  output += end;
  if (!print_write_text(runtime, file, output, error)) {
    return false;
  }
  if (flush && !print_flush_file(runtime, file, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool builtin_open(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1 || argc > 8) {
    error = "open expected between 1 and 8 arguments, got " + std::to_string(argc);
    return false;
  }
  std::string mode = "r";
  if (argc >= 2 && !get_string_arg(args[1], "open mode", mode, error)) {
    return false;
  }
  OpenOptions options;
  if (user_data != nullptr) {
    options = *static_cast<OpenOptions*>(user_data);
  } else if (!apply_open_positional_options(args, argc, options, error)) {
    return false;
  }

  OpenMode parsed;
  if (!parse_open_mode(mode, parsed, error)) {
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (parsed.binary && options.encoding_non_none) {
    error = "binary mode doesn't take an encoding argument";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (parsed.binary && options.errors_non_none) {
    error = "binary mode doesn't take an errors argument";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (parsed.binary && options.newline_non_none) {
    error = "binary mode doesn't take a newline argument";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (parsed.binary && options.buffering == 1 && !warn_binary_line_buffering(runtime, error)) {
    return false;
  }

  if (!options.closefd && args[0].tag != ValueTag::Int64 && args[0].tag != ValueTag::Bool) {
    error = "Cannot use closefd=False with file name";
    runtime.raise_class_error("ValueError", error);
    return false;
  }

  if (options.opener.tag != ValueTag::None) {
    if (args[0].tag == ValueTag::Int64) {
      error = "opener can only be used with a file path";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    int flags = parsed.update ? O_RDWR : parsed.writable ? O_WRONLY : O_RDONLY;
    if (parsed.append) flags |= O_APPEND;
    if (parsed.create) flags |= O_CREAT;
    if (parsed.truncate) flags |= O_TRUNC;
    if (parsed.exclusive) flags |= O_EXCL;
#if defined(_WIN32)
    flags |= parsed.binary ? _O_BINARY : _O_TEXT;
#endif
    Value opener_args[] = {args[0], Value::int64(flags)};
    Value descriptor;
    if (!runtime_call_callable(runtime, options.opener, opener_args, 2, descriptor, error)) {
      return false;
    }
    int64_t fd_value = 0;
    if (!value_int_like_to_i64(descriptor, fd_value) || fd_value < 0 ||
        fd_value > static_cast<int64_t>(INT_MAX)) {
      error = "opener returned an invalid file descriptor";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    out = Value::fd_file(
        static_cast<int>(fd_value), std::to_string(fd_value), mode,
        parsed.readable, parsed.writable, parsed.binary, options.closefd);
    auto* file = reinterpret_cast<FileObject*>(out.as.obj);
    file->runtime = &runtime;
    initialize_fd_append_position(*file, parsed.append);
    file->encoding = options.encoding;
    file->errors = options.errors;
    file->newline = options.newline;
    file->newline_is_none = options.newline_is_none;
    file->buffering = options.buffering;
    return true;
  }

  if (args[0].tag == ValueTag::Int64 || args[0].tag == ValueTag::Bool) {
    if (args[0].tag == ValueTag::Bool && !warn_bool_file_descriptor(runtime, error)) {
      return false;
    }
    const int64_t fd_value = args[0].tag == ValueTag::Bool
        ? (args[0].as.b ? 1 : 0)
        : args[0].as.i64;
    if (fd_value < 0 || fd_value > static_cast<int64_t>(INT_MAX)) {
      return raise_bad_file_descriptor(runtime, error);
    }
    if (!descriptor_is_valid(static_cast<int>(fd_value))) {
      return raise_bad_file_descriptor(runtime, error);
    }
    out = Value::fd_file(
        static_cast<int>(fd_value),
        std::to_string(fd_value),
        mode,
        parsed.readable,
        parsed.writable,
        parsed.binary,
        options.closefd);
    auto* file = reinterpret_cast<FileObject*>(out.as.obj);
    file->runtime = &runtime;
    initialize_fd_append_position(*file, parsed.append);
    file->encoding = options.encoding;
    file->errors = options.errors;
    file->newline = options.newline;
    file->newline_is_none = options.newline_is_none;
    file->buffering = options.buffering;
    return true;
  }

  std::string path;
  if (!get_path_arg(runtime, args[0], "open path", path, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      runtime.set_pending_exception(std::move(pending));
    } else {
      runtime.raise_class_error("TypeError", error);
    }
    return false;
  }
  if (path.find('\0') != std::string::npos) {
    error = "embedded null byte";
    runtime.raise_class_error("ValueError", error);
    return false;
  }

  if (is_devnull_path(path)) {
    int flags = parsed.update ? O_RDWR : parsed.writable ? O_WRONLY : O_RDONLY;
    if (parsed.append) flags |= O_APPEND;
#if defined(_WIN32)
    flags |= _O_BINARY | _O_NOINHERIT;
    const int fd = _wopen(L"NUL", flags);
#else
    const int fd = ::open("/dev/null", flags);
#endif
    if (fd < 0) {
      return raise_open_os_error(runtime, path, errno, error);
    }
    out = Value::fd_file(fd, path, mode, parsed.readable, parsed.writable,
                         parsed.binary, options.closefd);
    auto* file = reinterpret_cast<FileObject*>(out.as.obj);
    file->runtime = &runtime;
    file->readable = parsed.readable;
    file->writable = parsed.writable;
    file->append = parsed.append;
    file->binary = parsed.binary;
    file->encoding = options.encoding;
    file->errors = options.errors;
    file->newline = options.newline;
    file->newline_is_none = options.newline_is_none;
    file->buffering = options.buffering;
    return true;
  }

  ResolvedPath resolved;
  if (!runtime.vfs().resolve(path, resolved, error)) {
    return false;
  }

  std::string native_path;
  if (resolved.fs->native_path(resolved.path, native_path)) {
    int flags = parsed.update ? O_RDWR : parsed.writable ? O_WRONLY : O_RDONLY;
    if (parsed.append) flags |= O_APPEND;
    if (parsed.create) flags |= O_CREAT;
    if (parsed.truncate) flags |= O_TRUNC;
    if (parsed.exclusive) flags |= O_EXCL;
#if defined(_WIN32)
    // FileObject performs text decoding and newline translation itself.
    flags |= _O_BINARY | _O_NOINHERIT;
    const int fd = _wopen(
        std::filesystem::u8path(native_path).c_str(),
        flags,
        _S_IREAD | _S_IWRITE);
#else
    const int fd = ::open(native_path.c_str(), flags, 0666);
#endif
    if (fd < 0) {
      return raise_open_os_error(runtime, path, errno, error);
    }
#if defined(_WIN32)
    if (_stricmp(native_path.c_str(), "conout$") == 0) {
      CONSOLE_SCREEN_BUFFER_INFO console_info{};
      const intptr_t handle = _get_osfhandle(fd);
      if (handle == -1 ||
          !GetConsoleScreenBufferInfo(reinterpret_cast<HANDLE>(handle), &console_info)) {
        _close(fd);
        error = "cannot open console output";
        runtime.raise_class_error("OSError", error);
        return false;
      }
    }
#endif
    out = Value::fd_file(
        fd, path, mode, parsed.readable, parsed.writable, parsed.binary,
        options.closefd);
    auto* file = reinterpret_cast<FileObject*>(out.as.obj);
    file->runtime = &runtime;
    initialize_fd_append_position(*file, parsed.append);
    file->encoding = options.encoding;
    file->errors = options.errors;
    file->newline = options.newline;
    file->newline_is_none = options.newline_is_none;
    file->buffering = options.buffering;
    return true;
  }

  std::string buffer;
  VfsStat stat;
  std::string stat_error;
  const bool stat_ok = resolved.fs->stat(resolved.path, stat, stat_error);
  if (stat_ok && stat.kind == VfsNodeKind::Directory) {
    error = "is a directory: " + path;
    runtime.raise_class_error("IsADirectoryError", error);
    return false;
  }
  const bool exists = stat_ok && stat.kind == VfsNodeKind::File;
  if (parsed.exclusive && exists) {
    error = "file exists: " + path;
    return false;
  }
  if (parsed.readable || parsed.append) {
    std::vector<uint8_t> bytes;
    if (exists && !parsed.truncate) {
      if (!resolved.fs->read_file(resolved.path, bytes, error)) {
        runtime.raise_class_error("OSError", error);
        return false;
      }
      buffer.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (!parsed.binary && !decode_file_text(buffer, options, buffer, error)) {
        return false;
      }
    } else if (!parsed.writable) {
      return raise_file_not_found(runtime, path, error);
    }
  }

  if ((parsed.create && !exists) || parsed.truncate) {
    if (!resolved.fs->write_file(resolved.path, nullptr, 0, error)) {
      runtime.raise_class_error("OSError", error);
      return false;
    }
  }

  out = Value::file(resolved.fs, resolved.path, mode, std::move(buffer), parsed.writable);
  auto* file = reinterpret_cast<FileObject*>(out.as.obj);
  file->readable = parsed.readable;
  file->writable = parsed.writable;
  file->append = parsed.append;
  file->binary = parsed.binary;
  file->encoding = options.encoding;
  file->errors = options.errors;
  file->newline = options.newline;
  file->newline_is_none = options.newline_is_none;
  file->buffering = options.buffering;
  file->cursor = parsed.append ? file->buffer.size() : 0;
  return true;
}

bool builtin_open_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc < 1 || argc > 8) {
    error = "open expected between 1 and 8 arguments";
    return false;
  }
  std::vector<Value> positional;
  OpenOptions options;
  positional.reserve(2);
  positional.push_back(args[0]);
  if (argc >= 2) {
    positional.push_back(args[1]);
  }
  if (!apply_open_positional_options(args, argc, options, error)) {
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const char* name = kwargs[i].name;
    const Value* value = kwargs[i].value;
    if (name == nullptr || value == nullptr) {
      error = "open keyword argument is invalid";
      return false;
    }
    const std::string key(name);
    if (key == "mode") {
      if (positional.size() == 2) {
        error = "open got multiple values for argument 'mode'";
        return false;
      }
      positional.push_back(*value);
    } else if (key == "buffering" || key == "encoding" || key == "errors" || key == "newline" || key == "closefd" || key == "opener") {
      uint32_t positional_index = 0;
      if (key == "buffering") {
        positional_index = 2;
      } else if (key == "encoding") {
        positional_index = 3;
      } else if (key == "errors") {
        positional_index = 4;
      } else if (key == "newline") {
        positional_index = 5;
      } else if (key == "closefd") {
        positional_index = 6;
      } else {
        positional_index = 7;
      }
      if (argc > positional_index) {
        error = "open got multiple values for argument '" + key + "'";
        return false;
      }
      if (!apply_open_option(key, *value, options, error)) {
        return false;
      }
    } else {
      error = "open got unsupported keyword argument '" + key + "'";
      return false;
    }
  }
  return builtin_open(
      runtime,
      positional.data(),
      static_cast<uint32_t>(positional.size()),
      out,
      error,
      &options);
}

} // namespace

void register_io_builtins(Runtime& runtime) {
  runtime.register_builtin(
      "open",
      runtime.make_native_function(
          "open",
          builtin_open,
          nullptr,
          nullptr,
          nullptr,
          false,
          builtin_open_kw,
          false));
  runtime.register_builtin(
      "print",
      runtime.make_native_function(
          "print",
          builtin_print,
          nullptr,
          nullptr,
          nullptr,
          false,
          builtin_print_kw,
          false));
}

} // namespace xlang3
