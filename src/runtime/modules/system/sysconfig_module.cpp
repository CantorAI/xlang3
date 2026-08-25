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

#include <string>
#include <utility>
#include <vector>

namespace xlang3 {

namespace {

std::string sysconfig_root(Runtime& runtime) {
  const auto roots = runtime.import_roots();
  if (!roots.empty()) {
    return roots.front().string();
  }
  return ".";
}

Value sysconfig_path_value(Runtime& runtime, const std::string& name) {
  const std::string root = sysconfig_root(runtime);
  if (name == "stdlib" || name == "platstdlib") {
    return Value::string(root);
  }
  if (name == "purelib" || name == "platlib") {
    return Value::string(root + "/site-packages");
  }
  if (name == "include" || name == "platinclude") {
    return Value::string(root + "/include");
  }
  if (name == "scripts") {
    return Value::string(root + "/Scripts");
  }
  if (name == "data") {
    return Value::string(root);
  }
  return Value::string(root);
}

Value config_var_value(const std::string& name) {
  if (name == "py_version" || name == "VERSION") {
    return Value::string("3.14");
  }
  if (name == "Py_DEBUG") {
    return Value::int64(0);
  }
  if (name == "SOABI") {
    return Value::string("xlang3-314");
  }
#if defined(_WIN32)
  if (name == "EXT_SUFFIX") {
    return Value::string(".pyd");
  }
  if (name == "EXE") {
    return Value::string(".exe");
  }
#else
  if (name == "EXT_SUFFIX") {
    return Value::string(".so");
  }
  if (name == "EXE") {
    return Value::string("");
  }
#endif
  return Value::none();
}

bool sysconfig_get_path(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 4) {
    error = "sysconfig.get_path() expected name and optional scheme/vars/expand";
    return false;
  }
  auto* name = value_as_string(args[0]);
  if (name == nullptr) {
    error = "sysconfig.get_path() name must be str";
    return false;
  }
  out = sysconfig_path_value(runtime, string_object_to_string(*name));
  return true;
}

bool sysconfig_get_path_names(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.get_path_names() expected no arguments";
    return false;
  }
  out = Value::tuple({
      Value::string("stdlib"),
      Value::string("platstdlib"),
      Value::string("purelib"),
      Value::string("platlib"),
      Value::string("include"),
      Value::string("platinclude"),
      Value::string("scripts"),
      Value::string("data"),
  });
  return true;
}

bool sysconfig_get_paths(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 4) {
    error = "sysconfig.get_paths() expected optional scheme/vars/expand";
    return false;
  }
  std::vector<std::pair<Value, Value>> entries;
  const char* names[] = {"stdlib", "platstdlib", "purelib", "platlib", "include", "platinclude", "scripts", "data"};
  for (const char* name : names) {
    entries.push_back({Value::string(name), sysconfig_path_value(runtime, name)});
  }
  out = Value::dict(std::move(entries));
  return true;
}

bool sysconfig_get_config_var(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "sysconfig.get_config_var() expected one argument";
    return false;
  }
  auto* name = value_as_string(args[0]);
  if (name == nullptr) {
    error = "sysconfig.get_config_var() name must be str";
    return false;
  }
  out = config_var_value(string_object_to_string(*name));
  return true;
}

bool sysconfig_get_config_vars(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc == 0) {
    out = Value::dict({
        {Value::string("py_version"), Value::string("3.14")},
        {Value::string("VERSION"), Value::string("3.14")},
        {Value::string("Py_DEBUG"), Value::int64(0)},
        {Value::string("SOABI"), Value::string("xlang3-314")},
        {Value::string("EXT_SUFFIX"), config_var_value("EXT_SUFFIX")},
    });
    return true;
  }
  std::vector<Value> values;
  values.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    auto* name = value_as_string(args[i]);
    if (name == nullptr) {
      error = "sysconfig.get_config_vars() names must be str";
      return false;
    }
    values.push_back(config_var_value(string_object_to_string(*name)));
  }
  out = Value::list(std::move(values));
  return true;
}

bool sysconfig_get_platform(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.get_platform() expected no arguments";
    return false;
  }
#if defined(_WIN32)
  out = Value::string("win-amd64");
#elif defined(__APPLE__)
  out = Value::string("macosx");
#else
  out = Value::string("linux");
#endif
  return true;
}

bool sysconfig_get_python_version(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.get_python_version() expected no arguments";
    return false;
  }
  out = Value::string("3.14");
  return true;
}

bool sysconfig_get_default_scheme(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.get_default_scheme() expected no arguments";
    return false;
  }
#if defined(_WIN32)
  out = Value::string("nt");
#else
  out = Value::string("posix_prefix");
#endif
  return true;
}

bool sysconfig_get_preferred_scheme(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "sysconfig.get_preferred_scheme() expected optional key";
    return false;
  }
  if (argc == 1) {
    auto* key = value_as_string(args[0]);
    if (key == nullptr) {
      error = "sysconfig.get_preferred_scheme() key must be str";
      return false;
    }
    const std::string key_text = string_object_to_string(*key);
    if (key_text == "user") {
#if defined(_WIN32)
      out = Value::string("nt_user");
#else
      out = Value::string("posix_user");
#endif
      return true;
    }
  }
  return sysconfig_get_default_scheme(runtime, nullptr, 0, out, error, nullptr);
}

bool sysconfig_get_scheme_names(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.get_scheme_names() expected no arguments";
    return false;
  }
  out = Value::tuple({
      Value::string("nt"),
      Value::string("nt_user"),
      Value::string("posix_prefix"),
      Value::string("posix_user"),
      Value::string("venv"),
  });
  return true;
}

bool sysconfig_is_python_build(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.is_python_build() expected no arguments";
    return false;
  }
  out = Value::boolean(false);
  return true;
}

} // namespace

void register_sysconfig_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "sysconfig");
  builder.function("get_path", sysconfig_get_path)
      .function("get_path_names", sysconfig_get_path_names)
      .function("get_paths", sysconfig_get_paths)
      .function("get_config_var", sysconfig_get_config_var)
      .function("get_config_vars", sysconfig_get_config_vars)
      .function("get_platform", sysconfig_get_platform)
      .function("get_python_version", sysconfig_get_python_version)
      .function("get_default_scheme", sysconfig_get_default_scheme)
      .function("get_preferred_scheme", sysconfig_get_preferred_scheme)
      .function("get_scheme_names", sysconfig_get_scheme_names)
      .function("is_python_build", sysconfig_is_python_build);
  runtime.register_module("sysconfig", builder.finish());
}

} // namespace xlang3
