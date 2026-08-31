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
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"
#include "xlang3/sequence.h"

#include "zip_archive.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

namespace xlang3 {

namespace {

struct ModuleFile {
  std::string path;
  std::string package_dir;
  std::vector<std::string> namespace_dirs;
  std::string source;
  bool is_package = false;
  bool is_namespace_package = false;
  bool is_zip_source = false;
  std::string path_importer_cache_key;
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

std::string python_path_string(const std::filesystem::path& path) {
  return path.lexically_normal().generic_string();
}

std::string module_member_base(const std::vector<std::string>& parts) {
  std::string member;
  for (const auto& part : parts) {
    if (!member.empty()) {
      member += "/";
    }
    member += part;
  }
  return member;
}

bool find_zip_module_file(Runtime& runtime, const std::filesystem::path& archive_path, const std::vector<std::string>& parts, ModuleFile& out) {
  VfsStat stat;
  std::string error;
  if (!runtime.vfs().stat(archive_path.string(), stat, error) || stat.kind != VfsNodeKind::File) {
    return false;
  }
  const auto archive_string = python_path_string(archive_path);
  const auto extension = archive_path.extension().string();
  if (extension != ".zip") {
    return false;
  }
  std::vector<uint8_t> archive;
  if (!runtime.vfs().read_file(archive_string, archive, error)) {
    return false;
  }
  const auto base = module_member_base(parts);
  ZipArchiveEntry entry;
  std::string source;
  if (zip_archive_find_entry(archive, base + ".py", entry, error) &&
      zip_archive_extract_member(archive, entry, source, error)) {
    out.path = archive_string + "/" + base + ".py";
    out.source = std::move(source);
    out.is_package = false;
    out.is_namespace_package = false;
    out.is_zip_source = true;
    out.path_importer_cache_key = archive_string;
    return true;
  }
  error.clear();
  const auto init_member = base + "/__init__.py";
  if (zip_archive_find_entry(archive, init_member, entry, error) &&
      zip_archive_extract_member(archive, entry, source, error)) {
    out.path = archive_string + "/" + init_member;
    out.package_dir = archive_string + "/" + base;
    out.source = std::move(source);
    out.is_package = true;
    out.is_namespace_package = false;
    out.is_zip_source = true;
    out.path_importer_cache_key = archive_string;
    return true;
  }
  return false;
}

void cache_zip_path_importer(Runtime& runtime, const std::string& archive_path) {
  if (archive_path.empty()) {
    return;
  }
  Value sys;
  std::string ignored;
  if (!runtime.import_module("sys", sys, ignored)) {
    return;
  }
  Value cache;
  if (!module_get_attr(sys, "path_importer_cache", cache, ignored) || value_as_dict(cache) == nullptr) {
    return;
  }
  Value existing;
  if (mapping_get_item(cache, Value::string(archive_path), existing, ignored)) {
    return;
  }
  Value zipimport_module;
  if (!runtime.import_module("zipimport", zipimport_module, ignored)) {
    return;
  }
  Value zipimporter_class;
  if (!module_get_attr(zipimport_module, "zipimporter", zipimporter_class, ignored) ||
      value_as_class(zipimporter_class) == nullptr) {
    return;
  }
  Value importer = Value::instance(std::move(zipimporter_class));
  object_set_attr(importer, "archive", Value::string(archive_path), ignored);
  object_set_attr(importer, "prefix", Value::string(""), ignored);
  mapping_set_item(cache, Value::string(archive_path), importer, ignored);
}

bool find_module_file(Runtime& runtime, const std::string& name, ModuleFile& out) {
  const auto parts = split_module_name(name);
  const auto parent_name = parent_module_name(name);
  std::vector<std::filesystem::path> namespace_dirs;
  if (!parent_name.empty()) {
    Value parent;
    std::string ignored;
    if (runtime.import_module(parent_name, parent, ignored)) {
      Value package_path;
      if (module_get_attr(parent, "__path__", package_path, ignored)) {
        std::vector<std::filesystem::path> package_roots;
        if (auto* string = value_as_string(package_path)) {
          package_roots.emplace_back(string_object_view(*string));
        } else if (auto* list = value_as_list(package_path)) {
          for (const auto& item : list->items) {
            if (auto* item_string = value_as_string(item)) {
              package_roots.emplace_back(string_object_view(*item_string));
            }
          }
        }
        for (const auto& package_root : package_roots) {
          auto candidate_base = package_root / module_leaf_name(name);
          auto candidate = candidate_base;
          candidate += ".py";
          VfsStat stat;
          std::string error;
          if (runtime.vfs().stat(candidate.string(), stat, error) && stat.kind == VfsNodeKind::File) {
            out.path = python_path_string(candidate);
            out.is_package = false;
            out.package_dir.clear();
            return true;
          }
          auto package_init = candidate_base / "__init__.py";
          if (runtime.vfs().stat(package_init.string(), stat, error) && stat.kind == VfsNodeKind::File) {
            out.path = python_path_string(package_init);
            out.package_dir = python_path_string(candidate_base);
            out.is_package = true;
            out.is_namespace_package = false;
            return true;
          }
          if (runtime.vfs().stat(candidate_base.string(), stat, error) && stat.kind == VfsNodeKind::Directory) {
            namespace_dirs.push_back(candidate_base);
          }
        }
      }
    }
  }

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
    if (find_zip_module_file(runtime, root, parts, out)) {
      return true;
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
      out.path = python_path_string(candidate);
      out.is_package = false;
      out.package_dir.clear();
      return true;
    }

    auto package_init = candidate_base / "__init__.py";
    if (runtime.vfs().stat(package_init.string(), stat, error) && stat.kind == VfsNodeKind::File) {
      out.path = python_path_string(package_init);
      out.package_dir = python_path_string(candidate_base);
      out.is_package = true;
      out.is_namespace_package = false;
      return true;
    }

    if (runtime.vfs().stat(candidate_base.string(), stat, error) && stat.kind == VfsNodeKind::Directory) {
      namespace_dirs.push_back(candidate_base);
    }
  }
  if (!namespace_dirs.empty()) {
    out.path.clear();
    out.package_dir = python_path_string(namespace_dirs.front());
    out.namespace_dirs.reserve(namespace_dirs.size());
    for (const auto& dir : namespace_dirs) {
      const auto normalized = python_path_string(dir);
      if (std::find(out.namespace_dirs.begin(), out.namespace_dirs.end(), normalized) == out.namespace_dirs.end()) {
        out.namespace_dirs.push_back(normalized);
      }
    }
    out.is_package = true;
    out.is_namespace_package = true;
    return true;
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

bool import_timings_enabled() {
  return std::getenv("XLANG3_IMPORT_TIMINGS") != nullptr;
}

double seconds_since(std::chrono::steady_clock::time_point start) {
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return std::chrono::duration<double>(elapsed).count();
}

void trace_import_timing(const std::string& name, const char* phase, std::chrono::steady_clock::time_point start) {
  if (!import_timings_enabled()) {
    return;
  }
  std::cerr << "xlang3 import timing: " << name << " " << phase << " " << seconds_since(start) << "s\n";
}

} // namespace

bool import_python_module(Runtime& runtime, const std::string& name, Value& out, std::string& error) {
  const auto import_start = std::chrono::steady_clock::now();
  trace_import_timing(name, "begin", import_start);
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
  if (module_file.is_zip_source) {
    cache_zip_path_importer(runtime, module_file.path_importer_cache_key);
  }
  trace_import_timing(name, "found", import_start);

  if (module_file.is_namespace_package) {
    auto module_value = Value::module(name);
    std::string attr_error;
    module_set_attr(module_value, "__name__", Value::string(name), attr_error);
    module_set_attr(module_value, "__file__", Value::none(), attr_error);
    module_set_attr(module_value, "__package__", Value::string(name), attr_error);
    std::vector<Value> path_items;
    path_items.reserve(module_file.namespace_dirs.empty() ? 1 : module_file.namespace_dirs.size());
    if (module_file.namespace_dirs.empty()) {
      path_items.push_back(Value::string(module_file.package_dir));
    } else {
      for (const auto& dir : module_file.namespace_dirs) {
        path_items.push_back(Value::string(dir));
      }
    }
    module_set_attr(module_value, "__path__", Value::list(std::move(path_items)), attr_error);
    runtime.register_module(name, module_value);
    out = std::move(module_value);
    if (!parent_name.empty()) {
      module_set_attr(parent_module, module_leaf_name(name), out, attr_error);
    }
    trace_import_timing(name, "namespace-done", import_start);
    return true;
  }

  std::string source = module_file.source;
  if (!module_file.is_zip_source && !read_file(runtime, module_file.path, source, error)) {
    return false;
  }
  trace_import_timing(name, "read", import_start);

  auto module_value = Value::module(name);
  std::string attr_error;
  module_set_attr(module_value, "__name__", Value::string(name), attr_error);
  module_set_attr(module_value, "__file__", Value::string(module_file.path), attr_error);
  module_set_attr(module_value, "__package__", Value::string(module_file.is_package ? name : parent_name), attr_error);
  module_set_attr(module_value, "__annotations__", Value::dict({}), attr_error);
  if (module_file.is_package) {
    module_set_attr(module_value, "__path__", Value::string(module_file.package_dir), attr_error);
  }

  trace_import_timing(name, "parse-begin", import_start);
  auto parsed = parse_source(source);
  trace_import_timing(name, "parse-end", import_start);
  if (!parsed.errors.empty()) {
    error = "parse error importing module '" + name + "': " + parsed.errors.front();
    return false;
  }
  trace_import_timing(name, "lower-begin", import_start);
  auto lowered = lower_to_ir(parsed.module);
  trace_import_timing(name, "lower-end", import_start);
  if (!lowered.errors.empty()) {
    error = "lower error importing module '" + name + "': " + lowered.errors.front();
    return false;
  }
  auto module_ir = std::make_shared<ir::Module>(std::move(lowered.module));
  module_ir->source_file = module_file.path;

  runtime.register_module(name, module_value);
  Interpreter interpreter(runtime);
  trace_import_timing(name, "exec-begin", import_start);
  auto result = interpreter.run_module(*module_ir, module_value, module_ir);
  trace_import_timing(name, "exec-end", import_start);
  if (!result.errors.empty()) {
    runtime.unregister_module(name);
    error = "runtime error importing module '" + name + "': " + result.errors.front();
    return false;
  }

  out = std::move(module_value);
  if (!parent_name.empty()) {
    module_set_attr(parent_module, module_leaf_name(name), out, attr_error);
  }
  trace_import_timing(name, "done", import_start);
  return true;
}

} // namespace xlang3
