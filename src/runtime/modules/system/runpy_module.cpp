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

#include "xlang3/interpreter.h"
#include "xlang3/module_object.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"
#include "xlang3/vfs.h"

#include <memory>

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

Value module_dict_snapshot(const Value& module) {
  std::vector<std::pair<Value, Value>> entries;
  if (auto* object = value_as_module(module)) {
    entries.reserve(object->name_to_slot.size());
    for (const auto& pair : object->name_to_slot) {
      if (pair.second < object->slots.size()) {
        entries.push_back({Value::string(pair.first), object->slots[pair.second]});
      }
    }
  }
  return Value::dict(std::move(entries));
}

bool run_source_as_module(
    Runtime& runtime,
    const std::string& source,
    const std::string& module_name,
    const std::string& file_name,
    Value& out,
    std::string& error) {
  auto parsed = parse_source(source);
  if (!parsed.errors.empty()) {
    error = "runpy parse error: " + parsed.errors.front();
    return false;
  }
  auto lowered = lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    error = "runpy lower error: " + lowered.errors.front();
    return false;
  }

  auto module_ir = std::make_shared<ir::Module>(std::move(lowered.module));
  module_ir->source_file = file_name;
  Value module = Value::module(module_name);
  std::string attr_error;
  module_set_attr(module, "__name__", Value::string(module_name), attr_error);
  module_set_attr(module, "__file__", Value::string(file_name), attr_error);
  module_set_attr(module, "__package__", Value::string(""), attr_error);

  Interpreter interpreter(runtime);
  RuntimeResult result = interpreter.run_module(*module_ir, module, module_ir);
  if (!result.errors.empty()) {
    error = result.errors.front();
    return false;
  }
  out = module_dict_snapshot(module);
  return true;
}

bool runpy_run_module(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "runpy.run_module() expected module name and optional arguments";
    return false;
  }
  std::string module_name;
  if (!get_string_arg(args[0], "runpy.run_module module name", module_name, error)) {
    return false;
  }
  Value module;
  if (!runtime.import_module(module_name, module, error)) {
    return false;
  }
  out = module_dict_snapshot(module);
  return true;
}

bool runpy_run_path(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "runpy.run_path() expected path and optional run_name";
    return false;
  }
  std::string path;
  if (!get_string_arg(args[0], "runpy.run_path path", path, error)) {
    return false;
  }
  std::string run_name = "<run_path>";
  if (argc == 2 && args[1].tag != ValueTag::None && !get_string_arg(args[1], "runpy.run_path run_name", run_name, error)) {
    return false;
  }
  std::vector<uint8_t> bytes;
  if (!runtime.vfs().read_file(path, bytes, error)) {
    return false;
  }
  std::string source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return run_source_as_module(runtime, source, run_name, path, out, error);
}

} // namespace

void register_runpy_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "runpy");
  builder.function("run_module", runpy_run_module)
      .function("run_path", runpy_run_path);
  runtime.register_module("runpy", builder.finish());
}

} // namespace xlang3
