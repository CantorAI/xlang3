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
#include "xlang3/sequence.h"
#include "xlang3/vfs.h"

#include <algorithm>
#include <string>
#include <vector>

namespace xlang3 {

namespace {

std::string join_path(std::string base, const std::string& name) {
  if (base.empty()) {
    return name;
  }
  const char last = base.back();
  if (last == '/' || last == '\\') {
    return base + name;
  }
  return base + "/" + name;
}

std::string dirname_of(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) {
    return "";
  }
  return path.substr(0, slash);
}

bool has_suffix(const std::string& value, const std::string& suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void add_unique_module_info(std::vector<Value>& out, std::vector<std::string>& seen, std::string name, bool is_package) {
  if (name.empty() || name == "__init__" || name == "__pycache__") {
    return;
  }
  if (std::find(seen.begin(), seen.end(), name) != seen.end()) {
    return;
  }
  seen.push_back(name);
  out.push_back(Value::tuple({Value::none(), Value::string(std::move(name)), Value::boolean(is_package)}));
}

bool collect_path_argument(Runtime& runtime, const Value* args, uint32_t argc, std::vector<std::string>& paths, std::string& error) {
  if (argc == 0 || args[0].tag == ValueTag::None) {
    for (const auto& root : runtime.import_roots()) {
      paths.push_back(root.string());
    }
    return true;
  }
  if (auto* text = value_as_string(args[0])) {
    paths.push_back(string_object_to_string(*text));
    return true;
  }
  Value iterator;
  if (!sequence_get_iter(args[0], iterator, error)) {
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    auto* text = value_as_string(item);
    if (text == nullptr) {
      error = "pkgutil path entries must be strings";
      return false;
    }
    paths.push_back(string_object_to_string(*text));
  }
}

bool pkgutil_extend_path(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "pkgutil.extend_path() expected path and name";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool pkgutil_iter_modules(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*);

bool pkgutil_walk_packages(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 3) {
    error = "pkgutil.walk_packages() expected at most 3 arguments";
    return false;
  }
  return pkgutil_iter_modules(runtime, args, argc > 2 ? 2 : argc, out, error, nullptr);
}

bool pkgutil_iter_modules(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "pkgutil.iter_modules() expected at most 2 arguments";
    return false;
  }
  std::string prefix;
  if (argc == 2) {
    auto* prefix_text = value_as_string(args[1]);
    if (prefix_text == nullptr) {
      error = "pkgutil.iter_modules() prefix must be str";
      return false;
    }
    prefix = string_object_to_string(*prefix_text);
  }
  std::vector<std::string> paths;
  if (!collect_path_argument(runtime, args, argc, paths, error)) {
    return false;
  }

  std::vector<Value> values;
  std::vector<std::string> seen;
  for (const auto& path : paths) {
    std::vector<std::string> names;
    std::string list_error;
    if (!runtime.vfs().list_dir(path, names, list_error)) {
      continue;
    }
    std::sort(names.begin(), names.end());
    for (const auto& entry : names) {
      const std::string full = join_path(path, entry);
      VfsStat stat;
      std::string stat_error;
      if (!runtime.vfs().stat(full, stat, stat_error)) {
        continue;
      }
      if (stat.kind == VfsNodeKind::File && has_suffix(entry, ".py")) {
        add_unique_module_info(values, seen, prefix + entry.substr(0, entry.size() - 3), false);
      } else if (stat.kind == VfsNodeKind::Directory) {
        VfsStat init_stat;
        std::string init_error;
        if (runtime.vfs().stat(join_path(full, "__init__.py"), init_stat, init_error) && init_stat.kind == VfsNodeKind::File) {
          add_unique_module_info(values, seen, prefix + entry, true);
        }
      }
    }
  }
  out = Value::list(std::move(values));
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

bool pkgutil_get_data(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "pkgutil.get_data() expected package and resource";
    return false;
  }
  auto* resource_text = value_as_string(args[1]);
  if (resource_text == nullptr) {
    error = "pkgutil.get_data() resource must be str";
    return false;
  }
  const std::string resource = string_object_to_string(*resource_text);
  std::vector<std::string> candidates;
  if (auto* package_text = value_as_string(args[0])) {
    Value module;
    std::string import_error;
    if (runtime.import_module(string_object_to_string(*package_text), module, import_error)) {
      Value path_attr;
      std::string attr_error;
      if (module_get_attr(module, "__path__", path_attr, attr_error)) {
        if (auto* path_text = value_as_string(path_attr)) {
          candidates.push_back(join_path(string_object_to_string(*path_text), resource));
        }
      } else if (module_get_attr(module, "__file__", path_attr, attr_error)) {
        if (auto* file_text = value_as_string(path_attr)) {
          candidates.push_back(join_path(dirname_of(string_object_to_string(*file_text)), resource));
        }
      }
    }
  }
  candidates.push_back(resource);
  for (const auto& candidate : candidates) {
    std::vector<uint8_t> bytes;
    std::string read_error;
    if (runtime.vfs().read_file(candidate, bytes, read_error)) {
      out = Value::bytes(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
      return true;
    }
  }
  value_set_none(out);
  return true;
}

bool pkgutil_resolve_name(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "pkgutil.resolve_name() expected name";
    return false;
  }
  auto* name_text = value_as_string(args[0]);
  if (name_text == nullptr) {
    error = "pkgutil.resolve_name() name must be str";
    return false;
  }
  std::string name = string_object_to_string(*name_text);
  const size_t colon = name.find(':');
  std::string module_name = colon == std::string::npos ? name : name.substr(0, colon);
  std::string attr_path = colon == std::string::npos ? "" : name.substr(colon + 1);
  if (colon == std::string::npos) {
    size_t dot = name.size();
    while (dot != std::string::npos) {
      Value module;
      std::string import_error;
      if (runtime.import_module(name.substr(0, dot), module, import_error)) {
        module_name = name.substr(0, dot);
        attr_path = dot < name.size() ? name.substr(dot + 1) : "";
        out = module;
        break;
      }
      if (dot == 0) {
        break;
      }
      dot = name.rfind('.', dot - 1);
    }
  } else if (!runtime.import_module(module_name, out, error)) {
    return false;
  }
  if (out.tag == ValueTag::Invalid || out.tag == ValueTag::None) {
    if (!runtime.import_module(module_name, out, error)) {
      return false;
    }
  }
  while (!attr_path.empty()) {
    const size_t dot = attr_path.find('.');
    const std::string attr = dot == std::string::npos ? attr_path : attr_path.substr(0, dot);
    Value next;
    if (value_as_module(out) != nullptr) {
      if (!module_get_attr(out, attr, next, error)) {
        return false;
      }
    } else {
      if (!object_get_attr(out, attr, next, error)) {
        return false;
      }
    }
    out = next;
    attr_path = dot == std::string::npos ? "" : attr_path.substr(dot + 1);
  }
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
      .function("read_code", pkgutil_read_code)
      .function("get_data", pkgutil_get_data)
      .function("resolve_name", pkgutil_resolve_name);
  runtime.register_module("pkgutil", builder.finish());
}

} // namespace xlang3
