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

} // namespace

void register_site_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "site");
  builder.function("getsitepackages", site_getsitepackages)
      .function("getusersitepackages", site_getusersitepackages);
  runtime.register_module("site", builder.finish());
}

} // namespace xlang3
