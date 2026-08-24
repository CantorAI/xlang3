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
#include "xlang3/sequence.h"

namespace xlang3 {

namespace {

Value site_packages_from_roots(Runtime& runtime) {
  std::vector<Value> paths;
  for (const auto& root : runtime.import_roots()) {
    if (root.filename().string() == "site-packages") {
      paths.push_back(Value::string(root.string()));
    }
  }
  return Value::list(std::move(paths));
}

bool append_sys_path(Runtime& runtime, const std::string& path, std::string& error) {
  Value sys;
  if (!runtime.import_module("sys", sys, error)) {
    return false;
  }
  Value sys_path;
  if (!module_get_attr(sys, "path", sys_path, error)) {
    return false;
  }
  auto* list = value_as_list(sys_path);
  if (list == nullptr) {
    error = "sys.path is not a list";
    return false;
  }
  for (const auto& existing : list->items) {
    auto* text = value_as_string(existing);
    if (text != nullptr && string_object_to_string(*text) == path) {
      return true;
    }
  }
  list->items.push_back(Value::string(path));
  runtime.add_import_root(path);
  return true;
}

bool site_getsitepackages(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "site.getsitepackages() expected no arguments";
    return false;
  }
  out = site_packages_from_roots(runtime);
  return true;
}

bool site_getusersitepackages(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "site.getusersitepackages() expected no arguments";
    return false;
  }
  out = Value::string("");
  return true;
}

bool site_addsitedir(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "site.addsitedir() expected sitedir and optional known_paths";
    return false;
  }
  auto* path = value_as_string(args[0]);
  if (path == nullptr) {
    error = "site.addsitedir() sitedir must be str";
    return false;
  }
  if (!append_sys_path(runtime, string_object_to_string(*path), error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool site_addsitepackages(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 1) {
    error = "site.addsitepackages() expected optional known_paths";
    return false;
  }
  Value paths = site_packages_from_roots(runtime);
  auto* list = value_as_list(paths);
  if (list != nullptr) {
    for (const auto& item : list->items) {
      auto* text = value_as_string(item);
      if (text != nullptr && !append_sys_path(runtime, string_object_to_string(*text), error)) {
        return false;
      }
    }
  }
  value_assign_fast(out, argc == 1 ? args[0] : Value::none());
  return true;
}

} // namespace

void register_site_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "site");
  builder.function("getsitepackages", site_getsitepackages)
      .function("getusersitepackages", site_getusersitepackages)
      .function("addsitedir", site_addsitedir)
      .function("addsitepackages", site_addsitepackages)
      .value("PREFIXES", site_packages_from_roots(runtime))
      .value("USER_SITE", Value::string(""))
      .value("USER_BASE", Value::string(""))
      .value("ENABLE_USER_SITE", Value::boolean(true));
  runtime.register_module("site", builder.finish());
}

} // namespace xlang3
