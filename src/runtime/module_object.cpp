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
#include "xlang3/module_object.h"

#include "xlang3/perf_counters.h"
#include "xlang3/runtime.h"
#include "xlang3/functional_iterators.h"

#include <cstdlib>
#include <iostream>

#if !defined(XLANG3_EMBEDDED)
#include <filesystem>
#endif

namespace xlang3 {

namespace {

bool missing_lookup_diagnostics_enabled() {
  static const bool enabled = std::getenv("XLANG3_DIAG_MISSING_LOOKUPS") != nullptr;
  return enabled;
}

bool is_expected_import_probe_attr(const std::string& name) {
  return name == "__path__" || name == "__file__" || name == "__annotations__";
}

template <typename T>
T* allocate_module_object(ObjectKind kind) {
  auto* obj = new T();
  obj->header.kind = kind;
  obj->header.refcnt = 1;
  xlang_perf_count_object_alloc(kind);
  return obj;
}

#if !defined(XLANG3_EMBEDDED)
std::filesystem::path module_relative_source_path(const std::string& name) {
  std::filesystem::path path;
  size_t start = 0;
  for (;;) {
    const size_t dot = name.find('.', start);
    const auto part = name.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (!part.empty()) {
      path /= part;
    }
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }
  path += ".py";
  return path;
}

std::string native_module_source_file(Runtime& runtime, const std::string& name) {
  const auto relative_file = module_relative_source_path(name);
  for (const auto& root : runtime.import_roots()) {
    const auto candidate = root / relative_file;
    std::error_code ec;
    if (std::filesystem::is_regular_file(candidate, ec)) {
      return candidate.generic_string();
    }
    auto package_path = root;
    size_t start = 0;
    for (;;) {
      const size_t dot = name.find('.', start);
      const auto part = name.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
      if (!part.empty()) {
        package_path /= part;
      }
      if (dot == std::string::npos) {
        break;
      }
      start = dot + 1;
    }
    const auto package_init = package_path / "__init__.py";
    if (std::filesystem::is_regular_file(package_init, ec)) {
      return package_init.generic_string();
    }
  }
  return {};
}
#endif

std::string module_package_name(const std::string& name) {
  const size_t dot = name.rfind('.');
  return dot == std::string::npos ? std::string() : name.substr(0, dot);
}

} // namespace

Value Value::module(std::string name) {
  Value v;
  v.tag = ValueTag::Object;
  auto* obj = allocate_module_object<ModuleObject>(ObjectKind::Module);
  obj->name = std::move(name);
  v.as.obj = &obj->header;
  return v;
}

void module_release_object(Object* object) {
  if (object->kind == ObjectKind::Module) {
    delete reinterpret_cast<ModuleObject*>(object);
  }
}

std::string module_to_string(const Value& value) {
  auto* module = value_as_module(value);
  if (module == nullptr) {
    return "<module>";
  }
  Value file;
  std::string ignored;
  if (module_get_attr(value, "__file__", file, ignored)) {
    if (auto* text = value_as_string(file)) {
      return "<module '" + module->name + "' from '" + string_object_to_string(*text) + "'>";
    }
  }
  return "<module '" + module->name + "'>";
}

bool module_get_attr(const Value& object, const std::string& name, Value& out, std::string& error) {
  auto* module = value_as_module(object);
  if (module == nullptr) {
    error = "object has no attributes";
    return false;
  }
  if (name == "__name__") {
    out = Value::string(module->name);
    return true;
  }
  if (name == "__dict__") {
    value_assign_fast(out, object);
    return true;
  }
  if (name == "__spec__") {
    auto spec_it = module->name_to_slot.find(name);
    if (spec_it != module->name_to_slot.end() && spec_it->second < module->slots.size()) {
      value_assign_fast(out, module->slots[spec_it->second]);
      return true;
    }
    value_set_none(out);
    return true;
  }
  auto it = module->name_to_slot.find(name);
  if (it == module->name_to_slot.end() || it->second >= module->slots.size()) {
    error = "module '" + module->name + "' has no attribute '" + name + "'";
    if (missing_lookup_diagnostics_enabled() && !is_expected_import_probe_attr(name)) {
      std::cerr << "XLANG3_MISSING_ATTR kind=\"module\" object=\"" << module->name
                << "\" attr=\"" << name << "\"\n";
    }
    return false;
  }
  if (auto* property = value_as_property(module->slots[it->second]); property && property->native_module_runtime) {
    const Value descriptor = module->slots[it->second];
    return runtime_call_callable(*property->native_module_runtime, property->fget, &object, 1, out, error);
  }
  value_assign_fast(out, module->slots[it->second]);
  return true;
}

bool module_set_attr(Value& object, const std::string& name, const Value& value, std::string& error) {
  auto* module = value_as_module(object);
  if (module == nullptr) {
    error = "object does not support attribute assignment";
    return false;
  }
  if (name == "__name__" && value_as_string(value) != nullptr) {
    module->name = string_object_to_string(*value_as_string(value));
  }
  auto it = module->name_to_slot.find(name);
  if (it != module->name_to_slot.end() && it->second < module->slots.size()) {
    if (auto* property = value_as_property(module->slots[it->second]); property && property->native_module_runtime) {
      const Value descriptor = module->slots[it->second];
      if (value.tag == ValueTag::Invalid) {
        error = "native module property cannot be deleted: " + name;
        return false;
      }
      if (property->fset.tag == ValueTag::None) {
        error = "module property '" + name + "' is read-only";
        return false;
      }
      const Value args[] = {object, value};
      Value result;
      return runtime_call_callable(*property->native_module_runtime, property->fset, args, 2, result, error);
    }
    if ((value.flags & kXlangValueBorrowedRefFlag) != 0) {
      value_assign_fast(module->slots[it->second], value);
    } else {
      module->slots[it->second] = value;
    }
  } else {
    const auto slot = static_cast<uint32_t>(module->slots.size());
    module->name_to_slot[name] = slot;
    if ((value.flags & kXlangValueBorrowedRefFlag) != 0) {
      Value stored;
      value_assign_fast(stored, value);
      module->slots.push_back(std::move(stored));
    } else {
      module->slots.push_back(value);
    }
  }
  ++module->version;
  return true;
}

bool module_find_attr_slot(const Value& object, const std::string& name, uint32_t& slot, std::string& error) {
  auto* module = value_as_module(object);
  if (module == nullptr) {
    error = "object has no attributes";
    return false;
  }
  auto it = module->name_to_slot.find(name);
  if (it == module->name_to_slot.end() || it->second >= module->slots.size()) {
    error = "module '" + module->name + "' has no attribute '" + name + "'";
    return false;
  }
  slot = it->second;
  return true;
}

NativeModuleBuilder::NativeModuleBuilder(Runtime& runtime, std::string name)
    : runtime_(runtime), name_(std::move(name)), module_(Value::module(name_)) {
  std::string error;
  module_set_attr(module_, "__package__", Value::string(module_package_name(name_)), error);
#if !defined(XLANG3_EMBEDDED)
  const auto source_file = native_module_source_file(runtime_, name_);
  if (!source_file.empty()) {
    module_set_attr(module_, "__file__", Value::string(source_file), error);
  }
#endif
}

NativeModuleBuilder& NativeModuleBuilder::value(std::string name, Value value) {
  std::string error;
  module_set_attr(module_, name, value, error);
  return *this;
}

NativeModuleBuilder& NativeModuleBuilder::function(
    std::string name,
    NativeFunctionCallback callback,
    NativeFastCallCallback fast_callback,
    bool fast_releases_vm_lock,
    NativeKeywordFunctionCallback keyword_callback) {
  auto full_name = name_ + "." + name;
  value(
      std::move(name),
      runtime_.make_native_function(
          std::move(full_name),
          callback,
          nullptr,
          nullptr,
          fast_callback,
          fast_releases_vm_lock,
          keyword_callback,
          false));
  return *this;
}

Value NativeModuleBuilder::finish() {
  return std::move(module_);
}

} // namespace xlang3
