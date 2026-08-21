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
#include "xlang3/sequence.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace xlang3 {

namespace {

struct ModuleFile {
  std::string path;
  std::string package_dir;
  bool is_package = false;
};

std::vector<std::string> split_module_name(const std::string& name) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= name.size()) {
    const auto dot = name.find('.', start);
    if (dot == std::string::npos) {
      parts.push_back(name.substr(start));
      break;
    }
    parts.push_back(name.substr(start, dot - start));
    start = dot + 1;
  }
  return parts;
}

std::string parent_module_name(const std::string& name) {
  const auto dot = name.rfind('.');
  if (dot == std::string::npos) {
    return {};
  }
  return name.substr(0, dot);
}

std::string module_leaf_name(const std::string& name) {
  const auto dot = name.rfind('.');
  if (dot == std::string::npos) {
    return name;
  }
  return name.substr(dot + 1);
}

bool find_module_file(Runtime& runtime, const std::string& name, ModuleFile& out) {
  const auto parts = split_module_name(name);
  std::vector<std::filesystem::path> roots;
  Value sys;
  std::string ignored;
  if (runtime.import_module("sys", sys, ignored)) {
    Value path;
    if (module_get_attr(sys, "path", path, ignored)) {
      if (auto* list = value_as_list(path)) {
        roots.reserve(list->items.size() + runtime.import_roots().size());
        for (const auto& item : list->items) {
          if (auto* string = value_as_string(item)) {
            roots.emplace_back(string_object_view(*string));
          }
        }
      }
    }
  }
  roots.insert(roots.end(), runtime.import_roots().begin(), runtime.import_roots().end());

  for (auto root : roots) {
    if (root.empty()) {
      root = std::filesystem::current_path();
    }
    auto candidate_base = root;
    for (const auto& part : parts) {
      candidate_base /= part;
    }

    auto candidate = candidate_base;
    candidate += ".py";
    VfsStat stat;
    std::string error;
    if (runtime.vfs().stat(candidate.string(), stat, error) && stat.kind == VfsNodeKind::File) {
      out.path = candidate.string();
      out.is_package = false;
      out.package_dir.clear();
      return true;
    }

    auto package_init = candidate_base / "__init__.py";
    if (runtime.vfs().stat(package_init.string(), stat, error) && stat.kind == VfsNodeKind::File) {
      out.path = package_init.string();
      out.package_dir = candidate_base.string();
      out.is_package = true;
      return true;
    }
  }
  return false;
}

bool read_file(Runtime& runtime, const std::string& path, std::string& out, std::string& error) {
  std::vector<uint8_t> bytes;
  if (!runtime.vfs().read_file(path, bytes, error)) {
    error = "cannot open module file " + path + ": " + error;
    return false;
  }
  out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  return true;
}

} // namespace

bool import_python_module(Runtime& runtime, const std::string& name, Value& out, std::string& error) {
  const auto parent_name = parent_module_name(name);
  Value parent_module;
  if (!parent_name.empty() && !runtime.import_module(parent_name, parent_module, error)) {
    return false;
  }

  ModuleFile module_file;
  if (!find_module_file(runtime, name, module_file)) {
    error = "module '" + name + "' not found";
    return false;
  }

  std::string source;
  if (!read_file(runtime, module_file.path, source, error)) {
    return false;
  }

  auto module_value = Value::module(name);
  std::string attr_error;
  module_set_attr(module_value, "__name__", Value::string(name), attr_error);
  module_set_attr(module_value, "__file__", Value::string(module_file.path), attr_error);
  module_set_attr(module_value, "__package__", Value::string(module_file.is_package ? name : parent_name), attr_error);
  if (module_file.is_package) {
    module_set_attr(module_value, "__path__", Value::string(module_file.package_dir), attr_error);
  }

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
  module_ir->source_file = module_file.path;

  runtime.register_module(name, module_value);
  Interpreter interpreter(runtime);
  auto result = interpreter.run_module(*module_ir, module_value, module_ir);
  if (!result.errors.empty()) {
    runtime.unregister_module(name);
    error = "runtime error importing module '" + name + "': " + result.errors.front();
    return false;
  }

  out = std::move(module_value);
  if (!parent_name.empty()) {
    module_set_attr(parent_module, module_leaf_name(name), out, attr_error);
  }
  return true;
}

} // namespace xlang3
