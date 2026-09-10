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
#pragma once

#include "xlang3/runtime.h"

#include <string>
#include <vector>

namespace xlang3 {

struct PythonModuleLocation {
  std::string path;
  std::string package_dir;
  std::vector<std::string> namespace_dirs;
  bool is_package = false;
  bool is_namespace_package = false;
  bool is_zip_source = false;
};

bool find_python_module_location(Runtime& runtime, const std::string& name, PythonModuleLocation& out);
bool import_python_module(Runtime& runtime, const std::string& name, Value& out, std::string& error);

} // namespace xlang3
