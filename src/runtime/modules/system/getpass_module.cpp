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

#include <cstdlib>

namespace xlang3 {

namespace {

bool getpass_getpass(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "getpass.getpass() expected at most 2 arguments";
    return false;
  }
  out = Value::string("");
  return true;
}

bool getpass_getuser(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "getpass.getuser() expected no arguments";
    return false;
  }
  const char* names[] = {"LOGNAME", "USER", "LNAME", "USERNAME"};
  for (const char* name : names) {
    if (const char* value = std::getenv(name)) {
      if (value[0] != '\0') {
        out = Value::string(value);
        return true;
      }
    }
  }
  out = Value::string("");
  return true;
}

} // namespace

void register_getpass_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "getpass");
  builder.function("getpass", getpass_getpass)
      .function("default_getpass", getpass_getpass)
      .function("fallback_getpass", getpass_getpass)
      .function("getuser", getpass_getuser);
  runtime.register_module("getpass", builder.finish());
}

} // namespace xlang3
