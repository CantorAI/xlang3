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

#include "xlang3/object_model.h"
#include "xlang3/vfs.h"

namespace xlang3 {

namespace {

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

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = std::string(name) + " must be a string";
    return false;
  }
  out = string_object_to_string(*reinterpret_cast<StringObject*>(value.as.obj));
  return true;
}

bool get_path_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (get_string_arg(value, name, out, error)) {
    return true;
  }
  std::string ignored;
  Value path_value;
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
  error = std::string(name) + " must be str or path-like";
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
          error = "invalid open mode: " + mode;
          return false;
        }
        saw_action = true;
        out.readable = true;
        break;
      case 'w':
        if (saw_action) {
          error = "invalid open mode: " + mode;
          return false;
        }
        saw_action = true;
        out.writable = true;
        out.create = true;
        out.truncate = true;
        break;
      case 'a':
        if (saw_action) {
          error = "invalid open mode: " + mode;
          return false;
        }
        saw_action = true;
        out.writable = true;
        out.create = true;
        out.append = true;
        break;
      case 'x':
        if (saw_action) {
          error = "invalid open mode: " + mode;
          return false;
        }
        saw_action = true;
        out.writable = true;
        out.create = true;
        out.exclusive = true;
        break;
      case '+':
        if (out.update) {
          error = "invalid open mode: " + mode;
          return false;
        }
        out.update = true;
        break;
      case 't':
        if (saw_text || saw_binary) {
          error = "invalid open mode: " + mode;
          return false;
        }
        saw_text = true;
        break;
      case 'b':
        if (saw_text || saw_binary) {
          error = "invalid open mode: " + mode;
          return false;
        }
        saw_binary = true;
        out.binary = true;
        break;
      default:
        error = "invalid open mode: " + mode;
        return false;
    }
  }
  if (!saw_action) {
    error = "invalid open mode: " + mode;
    return false;
  }
  if (out.update) {
    out.readable = true;
    out.writable = true;
  }
  return true;
}

bool builtin_print(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  (void)error;
  (void)user_data;
  for (uint32_t i = 0; i < argc; ++i) {
    if (i != 0) {
      runtime.write_output(' ');
    }
    runtime.write_output(value_to_string(args[i]));
  }
  runtime.write_output('\n');
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
  for (uint32_t i = 0; i < kwargc; ++i) {
    const char* name = kwargs[i].name;
    const Value* value = kwargs[i].value;
    if (name == nullptr || value == nullptr) {
      error = "print keyword argument is invalid";
      return false;
    }
    if (std::string(name) == "sep") {
      if (!get_string_arg(*value, "print sep", sep, error)) {
        return false;
      }
    } else if (std::string(name) == "end") {
      if (!get_string_arg(*value, "print end", end, error)) {
        return false;
      }
    } else {
      error = std::string("print got unexpected keyword argument '") + name + "'";
      return false;
    }
  }
  for (uint32_t i = 0; i < argc; ++i) {
    if (i != 0) {
      runtime.write_output(sep);
    }
    runtime.write_output(value_to_string(args[i]));
  }
  runtime.write_output(end);
  value_set_none(out);
  return true;
}

bool builtin_open(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void*) {
  if (argc != 1 && argc != 2) {
    error = "open expected 1 or 2 arguments, got " + std::to_string(argc);
    return false;
  }
  std::string path;
  std::string mode = "r";
  if (!get_path_arg(args[0], "open path", path, error)) {
    return false;
  }
  if (argc == 2 && !get_string_arg(args[1], "open mode", mode, error)) {
    return false;
  }

  OpenMode parsed;
  if (!parse_open_mode(mode, parsed, error)) {
    return false;
  }

  ResolvedPath resolved;
  if (!runtime.vfs().resolve(path, resolved, error)) {
    return false;
  }

  std::string buffer;
  VfsStat stat;
  std::string stat_error;
  const bool stat_ok = resolved.fs->stat(resolved.path, stat, stat_error);
  const bool exists = stat_ok && stat.kind == VfsNodeKind::File;
  if (parsed.exclusive && exists) {
    error = "file exists: " + path;
    return false;
  }
  if (parsed.readable || parsed.append) {
    std::vector<uint8_t> bytes;
    if (exists && !parsed.truncate) {
      if (!resolved.fs->read_file(resolved.path, bytes, error)) {
        return false;
      }
      buffer.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    } else if (!parsed.writable) {
      error = "file not found: " + path;
      return false;
    }
  }

  out = Value::file(resolved.fs, resolved.path, mode, std::move(buffer), parsed.writable);
  auto* file = reinterpret_cast<FileObject*>(out.as.obj);
  file->readable = parsed.readable;
  file->writable = parsed.writable;
  file->append = parsed.append;
  file->binary = parsed.binary;
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
  if (argc < 1 || argc > 2) {
    error = "open expected path and optional mode";
    return false;
  }
  std::vector<Value> positional;
  positional.reserve(2);
  positional.push_back(args[0]);
  if (argc == 2) {
    positional.push_back(args[1]);
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
    } else if (key == "encoding" || key == "errors" || key == "newline") {
      if (value_as_string(*value) == nullptr && value->tag != ValueTag::None) {
        error = "open " + key + " must be str or None";
        return false;
      }
    } else if (key == "buffering") {
      if (value->tag != ValueTag::Int64) {
        error = "open buffering must be int";
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
      user_data);
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
          builtin_open_kw));
  runtime.register_builtin(
      "print",
      runtime.make_native_function(
          "print",
          builtin_print,
          nullptr,
          nullptr,
          nullptr,
          false,
          builtin_print_kw));
}

} // namespace xlang3
