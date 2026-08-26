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

#include "xlang3/mapping.h"
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

Value sysconfig_path_value_from_root(const std::string& base, const std::string& platbase, const std::string& name);

Value sysconfig_path_value(Runtime& runtime, const std::string& name) {
  const std::string root = sysconfig_root(runtime);
  return sysconfig_path_value_from_root(root, root, name);
}

Value sysconfig_path_value_from_root(const std::string& base, const std::string& platbase, const std::string& name) {
  if (name == "stdlib" || name == "platstdlib") {
    return Value::string(name == "platstdlib" ? platbase : base);
  }
  if (name == "purelib" || name == "platlib") {
    return Value::string((name == "platlib" ? platbase : base) + "/site-packages");
  }
  if (name == "include" || name == "platinclude") {
    return Value::string(base + "/include");
  }
  if (name == "scripts") {
    return Value::string(base + "/Scripts");
  }
  if (name == "data") {
    return Value::string(base);
  }
  return Value::string(base);
}

Value dict_get_string_key(const Value& dict_value, const char* key, const Value& fallback) {
  auto* dict = value_as_dict(dict_value);
  if (dict == nullptr) {
    return fallback;
  }
  for (const auto& entry : dict->entries) {
    auto* entry_key = value_as_string(entry.first);
    if (entry_key != nullptr && string_object_to_string(*entry_key) == key) {
      return entry.second;
    }
  }
  return fallback;
}

std::string dict_get_string_text(const Value& dict_value, const char* key, const std::string& fallback) {
  Value value = dict_get_string_key(dict_value, key, Value::string(fallback));
  auto* text = value_as_string(value);
  if (text == nullptr) {
    return fallback;
  }
  return string_object_to_string(*text);
}

Value config_var_value(const std::string& name) {
  if (name == "py_version" || name == "VERSION") {
    return Value::string("3.14");
  }
  if (name == "implementation") {
    return Value::string("xlang3");
  }
  if (name == "abiflags" || name == "MULTIARCH" || name == "PYTHONFRAMEWORK") {
    return Value::string("");
  }
  if (name == "prefix" || name == "exec_prefix" || name == "base" || name == "platbase" ||
      name == "installed_base" || name == "installed_platbase" || name == "projectbase" ||
      name == "srcdir") {
    return Value::string(".");
  }
  if (name == "BINDIR") {
    return Value::string("./Scripts");
  }
  if (name == "LIBDIR" || name == "LIBDEST" || name == "BINLIBDEST") {
    return Value::string(".");
  }
  if (name == "INCLUDEPY" || name == "CONFINCLUDEPY") {
    return Value::string("./include");
  }
  if (name == "LIBPL") {
    return Value::string("./config");
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

void add_config_entry(std::vector<std::pair<Value, Value>>& entries, const char* name) {
  entries.push_back({Value::string(name), config_var_value(name)});
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

bool sysconfig_expand_vars(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "sysconfig._expand_vars() expected scheme and optional vars";
    return false;
  }
  auto* scheme = value_as_string(args[0]);
  if (scheme == nullptr) {
    error = "sysconfig._expand_vars() scheme must be str";
    return false;
  }
  const std::string root = sysconfig_root(runtime);
  const Value empty_vars = Value::dict({});
  const Value& vars = argc >= 2 ? args[1] : empty_vars;
  if (argc >= 2 && value_as_dict(vars) == nullptr) {
    error = "sysconfig._expand_vars() vars must be dict";
    return false;
  }
  const std::string base = dict_get_string_text(vars, "base", root);
  const std::string platbase = dict_get_string_text(vars, "platbase", base);
  std::vector<std::pair<Value, Value>> entries;
  const char* names[] = {"stdlib", "platstdlib", "purelib", "platlib", "include", "platinclude", "scripts", "data"};
  for (const char* name : names) {
    entries.push_back({Value::string(name), sysconfig_path_value_from_root(base, platbase, name)});
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
    std::vector<std::pair<Value, Value>> entries;
    const char* names[] = {
        "py_version", "VERSION", "implementation", "abiflags", "MULTIARCH", "prefix", "exec_prefix",
        "base", "platbase", "installed_base", "installed_platbase", "projectbase", "srcdir", "BINDIR",
        "LIBDIR", "INCLUDEPY", "CONFINCLUDEPY", "LIBDEST", "BINLIBDEST", "LIBPL", "PYTHONFRAMEWORK",
        "Py_DEBUG", "SOABI", "EXT_SUFFIX", "EXE",
    };
    for (const char* name : names) {
      add_config_entry(entries, name);
    }
    out = Value::dict(std::move(entries));
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

bool sysconfig_get_makefile_filename(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.get_makefile_filename() expected no arguments";
    return false;
  }
  out = Value::string(sysconfig_root(runtime) + "/config/Makefile");
  return true;
}

bool sysconfig_get_config_h_filename(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.get_config_h_filename() expected no arguments";
    return false;
  }
  out = Value::string(sysconfig_root(runtime) + "/include/pyconfig.h");
  return true;
}

bool sysconfig_expand_makefile_vars(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "sysconfig.expand_makefile_vars() expected string and vars";
    return false;
  }
  auto* text = value_as_string(args[0]);
  auto* vars = value_as_dict(args[1]);
  if (text == nullptr || vars == nullptr) {
    error = "sysconfig.expand_makefile_vars() expected str and dict";
    return false;
  }
  std::string result = string_object_to_string(*text);
  for (const auto& entry : vars->entries) {
    auto* key = value_as_string(entry.first);
    if (key == nullptr || entry.second.tag == ValueTag::None) {
      continue;
    }
    const std::string name = string_object_to_string(*key);
    const std::string value = value_to_string(entry.second);
    const std::string paren_pattern = "$(" + name + ")";
    const std::string brace_pattern = "${" + name + "}";
    size_t pos = 0;
    while ((pos = result.find(paren_pattern, pos)) != std::string::npos) {
      result.replace(pos, paren_pattern.size(), value);
      pos += value.size();
    }
    pos = 0;
    while ((pos = result.find(brace_pattern, pos)) != std::string::npos) {
      result.replace(pos, brace_pattern.size(), value);
      pos += value.size();
    }
  }
  out = Value::string(std::move(result));
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
    if (key_text == "prefix") {
#if defined(_WIN32)
      out = Value::string("nt");
#else
      out = Value::string("posix_prefix");
#endif
      return true;
    }
    if (key_text == "home") {
      out = Value::string("posix_home");
      return true;
    }
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

bool sysconfig_get_preferred_schemes(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig._get_preferred_schemes() expected no arguments";
    return false;
  }
#if defined(_WIN32)
  out = Value::dict({
      {Value::string("prefix"), Value::string("nt")},
      {Value::string("home"), Value::string("posix_home")},
      {Value::string("user"), Value::string("nt_user")},
  });
#else
  out = Value::dict({
      {Value::string("prefix"), Value::string("posix_prefix")},
      {Value::string("home"), Value::string("posix_home")},
      {Value::string("user"), Value::string("posix_user")},
  });
#endif
  return true;
}

bool sysconfig_get_scheme_names(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig.get_scheme_names() expected no arguments";
    return false;
  }
  out = Value::tuple({
      Value::string("nt"),
      Value::string("nt_user"),
      Value::string("nt_venv"),
      Value::string("osx_framework_user"),
      Value::string("posix_home"),
      Value::string("posix_prefix"),
      Value::string("posix_user"),
      Value::string("posix_venv"),
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

bool sysconfig_get_sysconfigdata_name(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "sysconfig._get_sysconfigdata_name() expected no arguments";
    return false;
  }
#if defined(_WIN32)
  out = Value::string("_sysconfigdata__win32_xlang3");
#elif defined(__APPLE__)
  out = Value::string("_sysconfigdata__darwin_xlang3");
#else
  out = Value::string("_sysconfigdata__linux_xlang3");
#endif
  return true;
}

} // namespace

void register_sysconfig_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "sysconfig");
  builder.function("get_path", sysconfig_get_path)
      .function("get_path_names", sysconfig_get_path_names)
      .function("get_paths", sysconfig_get_paths)
      .function("_expand_vars", sysconfig_expand_vars)
      .function("get_config_var", sysconfig_get_config_var)
      .function("get_config_vars", sysconfig_get_config_vars)
      .function("get_makefile_filename", sysconfig_get_makefile_filename)
      .function("get_config_h_filename", sysconfig_get_config_h_filename)
      .function("expand_makefile_vars", sysconfig_expand_makefile_vars)
      .function("get_platform", sysconfig_get_platform)
      .function("get_python_version", sysconfig_get_python_version)
      .function("get_default_scheme", sysconfig_get_default_scheme)
      .function("get_preferred_scheme", sysconfig_get_preferred_scheme)
      .function("_get_preferred_schemes", sysconfig_get_preferred_schemes)
      .function("get_scheme_names", sysconfig_get_scheme_names)
      .function("is_python_build", sysconfig_is_python_build)
      .function("_get_sysconfigdata_name", sysconfig_get_sysconfigdata_name);
  runtime.register_module("sysconfig", builder.finish());
}

} // namespace xlang3
