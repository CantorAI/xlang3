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
#include "xlang3/vfs.h"

#include <cstdlib>

namespace xlang3 {

namespace {

bool get_string_arg(const Value& value, const char* name, std::string& out, std::string& error) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  error = std::string(name) + " must be str";
  return false;
}

bool no_args(uint32_t argc, const char* name, std::string& error) {
  if (argc == 0) {
    return true;
  }
  error = std::string(name) + "() expected no arguments";
  return false;
}

bool os_getcwd(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (!no_args(argc, "os.getcwd", error)) {
    return false;
  }
  out = Value::string(runtime.vfs().cwd());
  return true;
}

bool os_chdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.chdir() expected one argument";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "os.chdir path", path, error)) {
    return false;
  }
  if (!runtime.vfs().chdir(path, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool os_listdir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "os.listdir() expected at most one argument";
    return false;
  }
  std::string path = ".";
  if (argc == 1 && !get_string_arg(args[0], "os.listdir path", path, error)) {
    return false;
  }
  std::vector<std::string> names;
  if (!runtime.vfs().list_dir(path, names, error)) {
    return false;
  }
  std::vector<Value> values;
  values.reserve(names.size());
  for (auto& name : names) {
    values.push_back(Value::string(std::move(name)));
  }
  out = Value::list(std::move(values));
  return true;
}

bool os_remove(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.remove() expected one argument";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "os.remove path", path, error)) {
    return false;
  }
  if (!runtime.vfs().remove(path, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool os_stat(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.stat() expected one argument";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "os.stat path", path, error)) {
    return false;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(path, stat, error)) {
    return false;
  }
  const int64_t mode = stat.kind == VfsNodeKind::Directory ? 0040000 : stat.kind == VfsNodeKind::File ? 0100000 : 0;
  out = Value::tuple({
      Value::int64(mode),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
      Value::int64(static_cast<int64_t>(stat.size)),
      Value::int64(0),
      Value::int64(0),
      Value::int64(0),
  });
  return true;
}

bool os_getenv(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "os.getenv() expected one or two arguments";
    return false;
  }
  std::string name;
  if (!get_string_arg(args[0], "os.getenv key", name, error)) {
    return false;
  }
  const char* value = std::getenv(name.c_str());
  if (value != nullptr) {
    out = Value::string(value);
  } else if (argc == 2) {
    value_assign_fast(out, args[1]);
  } else {
    value_set_none(out);
  }
  return true;
}

bool os_fspath(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "os.fspath() expected one argument";
    return false;
  }
  if (value_as_string(args[0]) == nullptr) {
    error = "expected str path";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

} // namespace

void register_os_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "os");
  builder.function("getcwd", os_getcwd)
      .function("chdir", os_chdir)
      .function("listdir", os_listdir)
      .function("remove", os_remove)
      .function("unlink", os_remove)
      .function("stat", os_stat)
      .function("getenv", os_getenv)
      .function("fspath", os_fspath)
#if defined(_WIN32)
      .value("name", Value::string("nt"))
      .value("sep", Value::string("\\"))
      .value("pathsep", Value::string(";"))
      .value("curdir", Value::string("."))
      .value("pardir", Value::string(".."));
#else
      .value("name", Value::string("posix"))
      .value("sep", Value::string("/"))
      .value("pathsep", Value::string(":"))
      .value("curdir", Value::string("."))
      .value("pardir", Value::string(".."));
#endif
  auto module = builder.finish();
  runtime.register_module("os", module);
#if defined(_WIN32)
  runtime.register_module("nt", std::move(module));
#else
  runtime.register_module("posix", std::move(module));
#endif
}

} // namespace xlang3
