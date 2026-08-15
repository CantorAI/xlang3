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
#include "xlang3/import_loader.h"

#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace xlang3 {

namespace {

bool find_module_file(Runtime& runtime, const std::string& name, std::filesystem::path& out) {
  if (name.find('.') != std::string::npos) {
    return false;
  }
  for (const auto& root : runtime.import_roots()) {
    auto candidate = root / (name + ".py");
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      out = candidate;
      return true;
    }
  }
  return false;
}

bool read_file(const std::filesystem::path& path, std::string& out, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "cannot open module file " + path.string();
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  out = buffer.str();
  return true;
}

} // namespace

bool import_python_module(Runtime& runtime, const std::string& name, Value& out, std::string& error) {
  std::filesystem::path module_path;
  if (!find_module_file(runtime, name, module_path)) {
    error = "module '" + name + "' not found";
    return false;
  }

  std::string source;
  if (!read_file(module_path, source, error)) {
    return false;
  }

  auto module_value = Value::module(name);
  std::string attr_error;
  module_set_attr(module_value, "__name__", Value::string(name), attr_error);
  module_set_attr(module_value, "__file__", Value::string(module_path.string()), attr_error);

  auto parsed = parse_source(source);
  if (!parsed.errors.empty()) {
    error = "parse error importing module '" + name + "': " + parsed.errors.front();
    return false;
  }
  auto lowered = lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    error = "lower error importing module '" + name + "': " + lowered.errors.front();
    return false;
  }
  auto module_ir = std::make_shared<ir::Module>(std::move(lowered.module));

  runtime.register_module(name, module_value);
  Interpreter interpreter(runtime);
  auto result = interpreter.run_module(*module_ir, module_value, module_ir);
  if (!result.errors.empty()) {
    runtime.unregister_module(name);
    error = "runtime error importing module '" + name + "': " + result.errors.front();
    return false;
  }

  out = std::move(module_value);
  return true;
}

} // namespace xlang3
