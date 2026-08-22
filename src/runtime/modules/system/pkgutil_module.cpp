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
#include "xlang3/object_model.h"

namespace xlang3 {

namespace {

bool pkgutil_extend_path(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "pkgutil.extend_path() expected path and name";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool pkgutil_walk_packages(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 3) {
    error = "pkgutil.walk_packages() expected at most 3 arguments";
    return false;
  }
  out = Value::list({});
  return true;
}

bool pkgutil_iter_modules(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "pkgutil.iter_modules() expected at most 2 arguments";
    return false;
  }
  out = Value::list({});
  return true;
}

bool pkgutil_get_loader(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pkgutil.get_loader() expected one argument";
    return false;
  }
  value_set_none(out);
  return true;
}

bool pkgutil_find_loader(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return pkgutil_get_loader(runtime, args, argc, out, error, nullptr);
}

bool pkgutil_read_code(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pkgutil.read_code() expected one argument";
    return false;
  }
  value_set_none(out);
  return true;
}

} // namespace

void register_pkgutil_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "pkgutil");
  builder.function("extend_path", pkgutil_extend_path)
      .function("walk_packages", pkgutil_walk_packages)
      .function("iter_modules", pkgutil_iter_modules)
      .function("get_loader", pkgutil_get_loader)
      .function("find_loader", pkgutil_find_loader)
      .function("read_code", pkgutil_read_code);
  runtime.register_module("pkgutil", builder.finish());
}

} // namespace xlang3
