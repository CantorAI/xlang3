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

#include "xlang3/vfs.h"

namespace xlang3 {

namespace {

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = std::string(name) + " must be a string";
    return false;
  }
  out = string_object_to_string(*reinterpret_cast<StringObject*>(value.as.obj));
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
  if (!get_string_arg(args[0], "open path", path, error)) {
    return false;
  }
  if (argc == 2 && !get_string_arg(args[1], "open mode", mode, error)) {
    return false;
  }

  const bool readable = mode == "r" || mode == "rt" || mode == "a" || mode == "at";
  const bool writable = mode == "w" || mode == "wt" || mode == "a" || mode == "at";
  const bool append = mode == "a" || mode == "at";
  if (!readable && !writable) {
    error = "unsupported open mode: " + mode;
    return false;
  }

  ResolvedPath resolved;
  if (!runtime.vfs().resolve(path, resolved, error)) {
    return false;
  }

  std::string buffer;
  if (readable) {
    std::vector<uint8_t> bytes;
    VfsStat stat;
    if (resolved.fs->stat(resolved.path, stat, error) && stat.kind == VfsNodeKind::File) {
      if (!resolved.fs->read_file(resolved.path, bytes, error)) {
        return false;
      }
      buffer.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    } else if (!writable) {
      error = "file not found: " + path;
      return false;
    }
  }

  out = Value::file(resolved.fs, resolved.path, mode, std::move(buffer), writable);
  auto* file = reinterpret_cast<FileObject*>(out.as.obj);
  file->cursor = append ? file->buffer.size() : 0;
  return true;
}

} // namespace

void register_io_builtins(Runtime& runtime) {
  runtime.register_native_builtin("open", builtin_open);
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
