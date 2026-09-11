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
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/native_package_loader.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sema.h"
#include "xlang3/sequence.h"

#include "zip_archive.h"

#include <algorithm>
#include <chrono>
#include <cctype>
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
  bool is_bytecode = false;
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
  return path.lexically_normal().string();
}

std::string bytecode_cache_path(const std::string& source) {
  const std::filesystem::path source_path(source);
  return (source_path.parent_path() / "__pycache__" /
          (source_path.stem().string() + ".xlang3-314.pyc")).string();
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
  std::string path_entry = archive_path.string();
  std::string lower_entry = path_entry;
  std::transform(lower_entry.begin(), lower_entry.end(), lower_entry.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  const auto zip_pos = lower_entry.find(".zip");
  if (zip_pos == std::string::npos) return false;
  const std::string archive_name = path_entry.substr(0, zip_pos + 4);
  std::string prefix = path_entry.substr(zip_pos + 4);
  while (!prefix.empty() && (prefix.front() == '/' || prefix.front() == '\\')) prefix.erase(prefix.begin());
  std::replace(prefix.begin(), prefix.end(), '\\', '/');
  VfsStat stat;
  std::string error;
  if (!runtime.vfs().stat(archive_name, stat, error) || stat.kind != VfsNodeKind::File) {
    return false;
  }
  const auto archive_string = python_path_string(archive_name);
  auto virtual_path = [&](const std::string& member) {
    auto result = std::filesystem::path(archive_string) / std::filesystem::path(member);
    result.make_preferred();
    return result.string();
  };
  std::vector<uint8_t> archive;
  if (!runtime.vfs().read_file(archive_string, archive, error)) {
    return false;
  }
  auto base = module_member_base(parts);
  if (!prefix.empty()) base = prefix + "/" + base;
  ZipArchiveEntry entry;
  std::string source;
  if (zip_archive_find_entry(archive, base + ".pyc", entry, error) &&
      zip_archive_extract_member(archive, entry, source, error) &&
      source.size() >= 4 && source.compare(0, 4, "\x33\x58\x0d\x0a", 4) == 0) {
    out.path = virtual_path(base + ".pyc");
    out.source = std::move(source);
    out.is_package = false;
    out.is_namespace_package = false;
    out.is_zip_source = true;
    out.is_bytecode = true;
    out.path_importer_cache_key = archive_string;
    return true;
  }
  error.clear();
  if (zip_archive_find_entry(archive, base + ".py", entry, error) &&
      zip_archive_extract_member(archive, entry, source, error)) {
    out.path = virtual_path(base + ".py");
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
    out.path = virtual_path(init_member);
    out.package_dir = virtual_path(base);
    out.source = std::move(source);
    out.is_package = true;
    out.is_namespace_package = false;
    out.is_zip_source = true;
    out.path_importer_cache_key = archive_string;
    return true;
  }
  error.clear();
  const auto init_bytecode_member = base + "/__init__.pyc";
  if (zip_archive_find_entry(archive, init_bytecode_member, entry, error) &&
      zip_archive_extract_member(archive, entry, source, error)) {
    out.path = virtual_path(init_bytecode_member);
    out.package_dir = virtual_path(base);
    out.source = std::move(source);
    out.is_package = true;
    out.is_namespace_package = false;
    out.is_zip_source = true;
    out.is_bytecode = true;
    out.path_importer_cache_key = archive_string;
    return true;
  }
  std::vector<ZipArchiveEntry> entries;
  error.clear();
  const std::string namespace_prefix = base + "/";
  if (zip_archive_list_entries(archive, entries, error) &&
      std::any_of(entries.begin(), entries.end(), [&](const ZipArchiveEntry& candidate) {
        return candidate.name.size() >= namespace_prefix.size() &&
            candidate.name.compare(0, namespace_prefix.size(), namespace_prefix) == 0;
      })) {
    out.path.clear();
    out.package_dir = virtual_path(base);
    out.namespace_dirs = {out.package_dir};
    out.is_package = true;
    out.is_namespace_package = true;
    out.is_zip_source = true;
    out.path_importer_cache_key = archive_string;
    return true;
  }
  return false;
}

Value cache_zip_path_importer(Runtime& runtime, const std::string& archive_path) {
  if (archive_path.empty()) {
    return Value::invalid();
  }
  Value sys;
  std::string ignored;
  if (!runtime.import_module("sys", sys, ignored)) {
    return Value::invalid();
  }
  Value cache;
  if (!module_get_attr(sys, "path_importer_cache", cache, ignored) || value_as_dict(cache) == nullptr) {
    return Value::invalid();
  }
  Value existing;
  if (mapping_get_item(cache, Value::string(archive_path), existing, ignored)) {
    return existing;
  }
  Value zipimport_module;
  if (!runtime.import_module("zipimport", zipimport_module, ignored)) {
    return Value::invalid();
  }
  Value zipimporter_class;
  if (!module_get_attr(zipimport_module, "zipimporter", zipimporter_class, ignored) ||
      value_as_class(zipimporter_class) == nullptr) {
    return Value::invalid();
  }
  Value importer = Value::instance(std::move(zipimporter_class));
  object_set_attr(importer, "archive", Value::string(archive_path), ignored);
  object_set_attr(importer, "prefix", Value::string(""), ignored);
  mapping_set_item(cache, Value::string(archive_path), importer, ignored);
  return importer;
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
      Value parent_file;
      bool parent_is_namespace = false;
      if (module_get_attr(parent, "__file__", parent_file, ignored) && parent_file.tag == ValueTag::None) {
        parent_is_namespace = true;
        ModuleFile refreshed_parent;
        if (find_module_file(runtime, parent_name, refreshed_parent) &&
            refreshed_parent.is_namespace_package && !refreshed_parent.namespace_dirs.empty()) {
          std::vector<Value> refreshed_paths;
          refreshed_paths.reserve(refreshed_parent.namespace_dirs.size());
          for (const auto& directory : refreshed_parent.namespace_dirs) {
            refreshed_paths.push_back(Value::string(directory));
          }
          module_set_attr(parent, "__path__", Value::list(std::move(refreshed_paths)), ignored);
        }
      }
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
          ModuleFile zip_candidate;
          if (find_zip_module_file(runtime, package_root, {module_leaf_name(name)}, zip_candidate)) {
            if (!zip_candidate.is_namespace_package) {
              out = std::move(zip_candidate);
              return true;
            }
            for (const auto& directory : zip_candidate.namespace_dirs) {
              namespace_dirs.emplace_back(directory);
            }
            continue;
          }
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
          auto bytecode_candidate = candidate_base;
          bytecode_candidate += ".pyc";
          if (runtime.vfs().stat(bytecode_candidate.string(), stat, error) && stat.kind == VfsNodeKind::File) {
            out.path = python_path_string(bytecode_candidate);
            out.is_package = false;
            out.is_bytecode = true;
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
          auto package_bytecode = candidate_base / "__init__.pyc";
          if (runtime.vfs().stat(package_bytecode.string(), stat, error) && stat.kind == VfsNodeKind::File) {
            out.path = python_path_string(package_bytecode);
            out.package_dir = python_path_string(candidate_base);
            out.is_package = true;
            out.is_bytecode = true;
            out.is_namespace_package = false;
            return true;
          }
          if (runtime.vfs().stat(candidate_base.string(), stat, error) && stat.kind == VfsNodeKind::Directory) {
            namespace_dirs.push_back(candidate_base);
          }
        }
        if (!parent_is_namespace) {
          return false;
        }
      } else {
        return false;
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
    ModuleFile zip_candidate;
    if (find_zip_module_file(runtime, root, parts, zip_candidate)) {
      if (!zip_candidate.is_namespace_package) {
        out = std::move(zip_candidate);
        return true;
      }
      for (const auto& directory : zip_candidate.namespace_dirs) {
        namespace_dirs.emplace_back(directory);
      }
      continue;
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

    auto bytecode_candidate = candidate_base;
    bytecode_candidate += ".pyc";
    if (runtime.vfs().stat(bytecode_candidate.string(), stat, error) && stat.kind == VfsNodeKind::File) {
      out.path = python_path_string(bytecode_candidate);
      out.is_package = false;
      out.is_bytecode = true;
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

    auto package_bytecode = candidate_base / "__init__.pyc";
    if (runtime.vfs().stat(package_bytecode.string(), stat, error) && stat.kind == VfsNodeKind::File) {
      out.path = python_path_string(package_bytecode);
      out.package_dir = python_path_string(candidate_base);
      out.is_package = true;
      out.is_bytecode = true;
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

std::string python_import_miss_key(Runtime& runtime, const std::string& name) {
  std::string key = name;
  Value sys;
  Value path;
  std::string ignored;
  if (runtime.import_module("sys", sys, ignored) && module_get_attr(sys, "path", path, ignored)) {
    key.push_back('\n');
    key += value_to_string(path);
  }
  return key;
}

void append_pyc_u32(std::string& data, uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    data.push_back(static_cast<char>((value >> (i * 8)) & 0xffu));
  }
}

void write_source_bytecode_cache(
    Runtime& runtime,
    const ModuleFile& module_file,
    const std::shared_ptr<const ir::Module>& module_ir) {
  if (module_file.is_zip_source || module_file.path.empty() || module_ir == nullptr) {
    return;
  }
  Value sys;
  Value dont_write;
  std::string ignored;
  if (runtime.import_module("sys", sys, ignored) &&
      module_get_attr(sys, "dont_write_bytecode", dont_write, ignored) &&
      value_truthy(dont_write)) {
    return;
  }
  Value marshal_module;
  Value dumps;
  Value marshaled;
  Value code = Value::code(module_ir, module_ir->entry, "exec");
  if (!runtime.import_module("marshal", marshal_module, ignored) ||
      !module_get_attr(marshal_module, "dumps", dumps, ignored) ||
      !runtime_call_callable(runtime, dumps, &code, 1, marshaled, ignored)) {
    Value pending;
    runtime.take_pending_exception(pending);
    return;
  }
  auto* marshaled_bytes = value_as_bytes(marshaled);
  if (marshaled_bytes == nullptr) {
    return;
  }
  VfsStat stat;
  if (!runtime.vfs().stat(module_file.path, stat, ignored)) {
    return;
  }
  std::string pyc("\x33\x58\x0d\x0a", 4);
  append_pyc_u32(pyc, 0);
  append_pyc_u32(pyc, static_cast<uint32_t>(stat.mtime_ns / 1000000000ll));
  append_pyc_u32(pyc, static_cast<uint32_t>(stat.size));
  pyc.append(bytes_object_view(*marshaled_bytes));

  const std::filesystem::path cache_path(bytecode_cache_path(module_file.path));
  const std::filesystem::path cache_dir = cache_path.parent_path();
  if (!runtime.vfs().make_dirs(cache_dir.string(), true, ignored)) {
    return;
  }
  (void)runtime.vfs().write_file(
      cache_path.string(),
      reinterpret_cast<const uint8_t*>(pyc.data()),
      pyc.size(),
      ignored);
}

} // namespace

bool find_python_module_location(Runtime& runtime, const std::string& name, PythonModuleLocation& out) {
  ModuleFile module_file;
  if (!find_module_file(runtime, name, module_file)) {
    return false;
  }
  out.path = std::move(module_file.path);
  out.package_dir = std::move(module_file.package_dir);
  out.namespace_dirs = std::move(module_file.namespace_dirs);
  out.is_package = module_file.is_package;
  out.is_namespace_package = module_file.is_namespace_package;
  out.is_zip_source = module_file.is_zip_source;
  return true;
}

bool import_python_module(Runtime& runtime, const std::string& name, Value& out, std::string& error) {
  const auto import_start = std::chrono::steady_clock::now();
  trace_import_timing(name, "begin", import_start);
  const auto parent_name = parent_module_name(name);
  Value parent_module;
  if (!parent_name.empty() && !runtime.import_module(parent_name, parent_module, error)) {
    return false;
  }
  if (!parent_name.empty()) {
    Value registry_module;
    std::string registry_error;
    if (runtime.module_registry_dict().tag != ValueTag::Invalid &&
        mapping_get_item(runtime.module_registry_dict(), Value::string(name), registry_module, registry_error) &&
        value_as_module(registry_module) != nullptr) {
      value_assign_fast(out, registry_module);
      trace_import_timing(name, "registry-after-parent", import_start);
      return true;
    }
  }

  const std::string miss_key = python_import_miss_key(runtime, name);
  if (runtime.has_python_import_miss(miss_key)) {
    error = "module '" + name + "' not found";
    return false;
  }
  ModuleFile module_file;
  if (!find_module_file(runtime, name, module_file)) {
    runtime.remember_python_import_miss(miss_key);
    error = "module '" + name + "' not found";
    return false;
  }
  Value zip_loader;
  if (module_file.is_zip_source) {
    zip_loader = cache_zip_path_importer(runtime, module_file.path_importer_cache_key);
  }
  trace_import_timing(name, "found", import_start);

  if (module_file.is_namespace_package) {
    // A data-only directory must not hide a concrete native module.
    bool library_found = false;
    std::string native_error;
    if (import_native_package(runtime, name, NativePackageLookupMode::IncludeXlangPrefixFallback,
                              out, native_error, &library_found)) {
      return true;
    }
    if (library_found) {
      error = std::move(native_error);
      return false;
    }
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
  if (!module_file.is_bytecode) {
    std::string decoded_source;
    if (!runtime.decode_python_source(source, decoded_source, error)) {
      error = "cannot decode module '" + name + "': " + error;
      return false;
    }
    source = std::move(decoded_source);
  }
  trace_import_timing(name, "read", import_start);

  auto module_value = Value::module(name);
  std::string attr_error;
  module_set_attr(module_value, "__name__", Value::string(name), attr_error);
  module_set_attr(module_value, "__file__", Value::string(module_file.path), attr_error);
  module_set_attr(
      module_value,
      "__cached__",
      Value::string(module_file.is_bytecode ? module_file.path : bytecode_cache_path(module_file.path)),
      attr_error);
  module_set_attr(module_value, "__package__", Value::string(module_file.is_package ? name : parent_name), attr_error);
  module_set_attr(module_value, "__annotations__", Value::dict({}), attr_error);
  if (module_file.is_zip_source && zip_loader.tag != ValueTag::Invalid) {
    module_set_attr(module_value, "__loader__", zip_loader, attr_error);
  }
  if (module_file.is_package) {
    module_set_attr(
        module_value,
        "__path__",
        Value::list({Value::string(module_file.package_dir)}),
        attr_error);
  }

  std::shared_ptr<const ir::Module> module_ir;
  if (module_file.is_bytecode) {
    if (source.size() < 16 || source.compare(0, 4, "\x33\x58\x0d\x0a", 4) != 0) {
      error = "bad magic number in bytecode file '" + module_file.path + "'";
      return false;
    }
    Value marshal_module;
    Value loads;
    Value code_value;
    Value payload = Value::bytes(source.substr(16));
    if (!runtime.import_module("marshal", marshal_module, error) ||
        !module_get_attr(marshal_module, "loads", loads, error) ||
        !runtime_call_callable(runtime, loads, &payload, 1, code_value, error)) {
      return false;
    }
    auto* code = value_as_code(code_value);
    if (code == nullptr || code->module == nullptr) {
      error = "bytecode file does not contain a code object";
      return false;
    }
    module_ir = code->module;
  } else {
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
    auto mutable_module_ir = std::make_shared<ir::Module>(std::move(lowered.module));
    mutable_module_ir->source_file = module_file.path;
    module_ir = std::move(mutable_module_ir);
  }

  runtime.register_module(name, module_value);
  Interpreter interpreter(runtime);
  trace_import_timing(name, "exec-begin", import_start);
  auto result = interpreter.run_module(*module_ir, module_value, module_ir);
  trace_import_timing(name, "exec-end", import_start);
  if (!result.errors.empty()) {
    runtime.unregister_module(name);
    if (result.exception.tag != ValueTag::Invalid) {
      runtime.set_pending_exception(result.exception);
    }
    error = "runtime error importing module '" + name + "': " + result.errors.front();
    return false;
  }

  if (!module_file.is_bytecode) {
    write_source_bytecode_cache(runtime, module_file, module_ir);
  }

  Value final_module;
  std::string registry_error;
  if (runtime.module_registry_dict().tag != ValueTag::Invalid &&
      mapping_get_item(runtime.module_registry_dict(), Value::string(name), final_module, registry_error) &&
      final_module.tag != ValueTag::Invalid) {
    value_assign_fast(out, final_module);
    if (value_as_module(final_module) != nullptr) {
      runtime.register_module(name, final_module);
    }
  } else {
    out = std::move(module_value);
  }
  if (!parent_name.empty()) {
    module_set_attr(parent_module, module_leaf_name(name), out, attr_error);
  }
  trace_import_timing(name, "done", import_start);
  return true;
}

} // namespace xlang3
