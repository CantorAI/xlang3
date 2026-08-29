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
#include "xlang3/native_package_loader.h"

#include "xlang3/attribute.h"
#include "xlang3/c_api_bridge.h"
#include "xlang3/module_object.h"
#include "xlang3/native_call_context.h"
#include "xlang3/object_model.h"
#include "xlang3/xmodule.h"

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

struct X3Module {
  xlang3::Runtime* runtime = nullptr;
  std::string name;
  xlang3::Value value;
};

struct X3Package {
  xlang3::Runtime* runtime = nullptr;
  std::string name;
  xlang3::Value root_module;
  xlang3::Value requested_module;
  std::vector<std::unique_ptr<X3Module>> modules;
  std::vector<std::pair<std::string, std::string>> metadata;
  void* cleanup_data = nullptr;
  X3PackageCleanup cleanup = nullptr;
};

namespace xlang3 {

namespace {

struct NativeThunk {
  X3NativeFn callback = nullptr;
  void* user_data = nullptr;
};

std::vector<void*>& loaded_library_handles() {
  static std::vector<void*> handles;
  return handles;
}

std::vector<std::filesystem::path> native_library_candidates(const std::filesystem::path& root, const std::string& name) {
  std::vector<std::filesystem::path> out;
#if defined(_WIN32)
  out.push_back(root / (name + ".x3pkg.dll"));
  out.push_back(root / (name + ".dll"));
#elif defined(__APPLE__)
  out.push_back(root / ("lib" + name + ".x3pkg.dylib"));
  out.push_back(root / (name + ".x3pkg.dylib"));
  out.push_back(root / ("lib" + name + ".dylib"));
#else
  out.push_back(root / ("lib" + name + ".x3pkg.so"));
  out.push_back(root / (name + ".x3pkg.so"));
  out.push_back(root / ("lib" + name + ".so"));
#endif
  return out;
}

std::string metadata_attr_name(const char* key) {
  return std::string("__xlang3_") + key + "__";
}

void apply_package_metadata(X3Package& package, X3Module& module) {
  std::string error;
  for (const auto& item : package.metadata) {
    module_set_attr(module.value, metadata_attr_name(item.first.c_str()), Value::string(item.second), error);
  }
}

std::vector<std::string> native_package_name_candidates(const std::string& name) {
  std::vector<std::string> out{name};
  if (name.rfind("xlang_", 0) != 0) {
    out.push_back("xlang_" + name);
  }
  return out;
}

void* open_library(const std::filesystem::path& path, std::string& error) {
#if defined(_WIN32)
  HMODULE handle = LoadLibraryA(path.string().c_str());
  if (handle == nullptr) {
    error = "cannot load native package " + path.string();
  }
  return reinterpret_cast<void*>(handle);
#else
  void* handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    const char* dl_error = dlerror();
    error = "cannot load native package " + path.string();
    if (dl_error != nullptr) {
      error += ": ";
      error += dl_error;
    }
  }
  return handle;
#endif
}

void* find_symbol(void* handle, const char* name) {
#if defined(_WIN32)
  return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(handle), name));
#else
  return dlsym(handle, name);
#endif
}

bool native_function_bridge(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  auto* thunk = static_cast<NativeThunk*>(user_data);
  if (thunk == nullptr || thunk->callback == nullptr) {
    error = "native function callback is missing";
    return false;
  }

  std::vector<X3Value> c_args;
  c_args.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    c_args.push_back(to_c_value(args[i]));
  }

  X3Value c_result = x3_value_invalid();
  X3CallContext context;
  context.runtime = &runtime;
  context.error = &error;
  Value exception;
  context.exception = &exception;
  context.user_data = thunk->user_data;
  const X3Status status = thunk->callback(
      &context,
      reinterpret_cast<X3Runtime*>(&runtime),
      thunk->user_data,
      c_args.data(),
      argc,
      &c_result);

  if (status == X3_STATUS_OK && c_result.tag == X3_TAG_OBJECT) {
    for (const auto& value : c_args) {
      if (value.tag == X3_TAG_OBJECT && c_result.as.obj == value.as.obj) {
        x3_value_retain(c_result);
        break;
      }
    }
  }

  for (auto& value : c_args) {
    x3_value_release(value);
  }

  if (status != X3_STATUS_OK) {
    if (exception.tag != ValueTag::Invalid) {
      runtime.set_pending_exception(std::move(exception));
    }
    if (error.empty()) {
      error = "native function failed";
    }
    return false;
  }

  out = from_c_value(c_result, error);
  x3_value_release(c_result);
  return error.empty();
}

X3Status host_add_module(X3Package* package, const char* name, X3Module** out_module) {
  if (package == nullptr || package->runtime == nullptr || name == nullptr || out_module == nullptr) {
    return X3_STATUS_ERROR;
  }
  auto module = std::make_unique<X3Module>();
  module->runtime = package->runtime;
  module->name = name;
  module->value = Value::module(name);
  apply_package_metadata(*package, *module);

  std::string error;
  module_set_attr(package->root_module, name, module->value, error);
  package->runtime->register_module(name, module->value);
  package->runtime->register_module(package->name + "." + name, module->value);
  if (module->name == package->name) {
    value_assign_fast(package->requested_module, module->value);
  }

  *out_module = module.get();
  package->modules.push_back(std::move(module));
  return X3_STATUS_OK;
}

X3Status host_module_add_value(X3Module* module, const char* name, X3Value value) {
  if (module == nullptr || name == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  auto internal = from_c_value(value, error);
  if (!error.empty()) {
    module->runtime->set_last_error(error);
    return X3_STATUS_ERROR;
  }
  if (!module_set_attr(module->value, name, internal, error)) {
    module->runtime->set_last_error(error);
    return X3_STATUS_ERROR;
  }
  return X3_STATUS_OK;
}

X3Status host_module_add_function(X3Module* module, const X3NativeFunctionDef* def) {
  if (module == nullptr || def == nullptr || def->name == nullptr || def->callback == nullptr) {
    return X3_STATUS_ERROR;
  }
  auto* thunk = new NativeThunk();
  thunk->callback = def->callback;
  thunk->user_data = def->user_data;

  auto function = module->runtime->make_native_function(
      module->name + "." + def->name,
      native_function_bridge,
      thunk,
      [](void* data) { delete static_cast<NativeThunk*>(data); });
  std::string error;
  if (!module_set_attr(module->value, def->name, function, error)) {
    module->runtime->set_last_error(error);
    return X3_STATUS_ERROR;
  }
  return X3_STATUS_OK;
}

X3Status host_set_attr(X3Runtime* runtime, X3Value object, const char* name, X3Value value) {
  if (runtime == nullptr || name == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  Value internal_object = from_c_value(object, error);
  if (!error.empty()) {
    reinterpret_cast<Runtime*>(runtime)->set_last_error(error);
    return X3_STATUS_ERROR;
  }
  Value internal_value = from_c_value(value, error);
  if (!error.empty()) {
    reinterpret_cast<Runtime*>(runtime)->set_last_error(error);
    return X3_STATUS_ERROR;
  }
  if (!attribute_set(internal_object, name, internal_value, error)) {
    reinterpret_cast<Runtime*>(runtime)->set_last_error(error);
    return X3_STATUS_ERROR;
  }
  return X3_STATUS_OK;
}

X3Status host_module_add_class(
    X3Module* module,
    const char* name,
    const X3NativeFunctionDef* methods,
    uint32_t method_count,
    X3Value* out_class) {
  if (module == nullptr || name == nullptr || (method_count != 0 && methods == nullptr)) {
    return X3_STATUS_ERROR;
  }

  std::vector<std::pair<std::string, Value>> attrs;
  attrs.reserve(method_count);
  for (uint32_t i = 0; i < method_count; ++i) {
    const auto& method = methods[i];
    if (method.name == nullptr || method.callback == nullptr) {
      return X3_STATUS_ERROR;
    }
    auto* thunk = new NativeThunk();
    thunk->callback = method.callback;
    thunk->user_data = method.user_data;
    attrs.emplace_back(
        method.name,
        module->runtime->make_native_function(
            std::string(name) + "." + method.name,
            native_function_bridge,
            thunk,
            [](void* data) { delete static_cast<NativeThunk*>(data); }));
  }

  Value klass = Value::class_object(name, std::move(attrs));
  std::string error;
  if (!module_set_attr(module->value, name, klass, error)) {
    module->runtime->set_last_error(error);
    return X3_STATUS_ERROR;
  }
  if (out_class != nullptr) {
    *out_class = to_c_value(klass);
  }
  return X3_STATUS_OK;
}

X3Status host_builtin_value(X3Package* package, const char* name, X3Value* out_value) {
  if (package == nullptr || package->runtime == nullptr || name == nullptr || out_value == nullptr) {
    return X3_STATUS_ERROR;
  }
  const Value* value = package->runtime->find_builtin(name);
  if (value == nullptr) {
    package->runtime->set_last_error(std::string("builtin '") + name + "' is not defined");
    return X3_STATUS_ERROR;
  }
  *out_value = to_c_value(*value);
  return X3_STATUS_OK;
}

X3Status host_class_set_base(X3Value klass, X3Value base) {
  std::string error;
  Value internal_class = from_c_value(klass, error);
  if (!error.empty()) {
    return X3_STATUS_ERROR;
  }
  Value internal_base = from_c_value(base, error);
  if (!error.empty()) {
    return X3_STATUS_ERROR;
  }
  return class_set_base(std::move(internal_class), std::move(internal_base), error) ? X3_STATUS_OK : X3_STATUS_ERROR;
}

X3Status host_instance_set_native_data(
    X3Value instance,
    const char* type_name,
    void* data,
    X3NativeDataCleanup cleanup) {
  if (type_name == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  auto internal = from_c_value(instance, error);
  if (!error.empty()) {
    return X3_STATUS_ERROR;
  }
  return instance_set_native_data(internal, type_name, data, cleanup, error) ? X3_STATUS_OK : X3_STATUS_ERROR;
}

void* host_instance_get_native_data(X3Value instance, const char* type_name) {
  if (type_name == nullptr) {
    return nullptr;
  }
  std::string error;
  auto internal = from_c_value(instance, error);
  if (!error.empty()) {
    return nullptr;
  }
  return instance_get_native_data(internal, type_name);
}

X3Value host_value_instance(X3Runtime* runtime, X3Value klass) {
  auto* rt = reinterpret_cast<Runtime*>(runtime);
  if (rt == nullptr) {
    return x3_value_invalid();
  }
  std::string error;
  auto internal_class = from_c_value(klass, error);
  if (!error.empty() || value_as_class(internal_class) == nullptr) {
    if (rt != nullptr) {
      rt->set_last_error(error.empty() ? "object is not a class" : error);
    }
    return x3_value_invalid();
  }
  return to_c_value(Value::instance(std::move(internal_class)));
}

X3Status host_package_set_cleanup(X3Package* package, void* data, X3PackageCleanup cleanup) {
  if (package == nullptr) {
    return X3_STATUS_ERROR;
  }
  package->cleanup_data = data;
  package->cleanup = cleanup;
  return X3_STATUS_OK;
}

X3Status host_package_set_metadata(X3Package* package, const char* key, const char* value) {
  if (package == nullptr || package->runtime == nullptr || key == nullptr || key[0] == '\0' || value == nullptr) {
    return X3_STATUS_ERROR;
  }
  package->metadata.emplace_back(key, value);
  std::string error;
  const auto attr_name = metadata_attr_name(key);
  module_set_attr(package->root_module, attr_name, Value::string(value), error);
  for (const auto& module : package->modules) {
    module_set_attr(module->value, attr_name, Value::string(value), error);
  }
  return X3_STATUS_OK;
}

X3Status host_set_error(X3CallContext* context, const char* message) {
  if (context == nullptr || context->error == nullptr) {
    return X3_STATUS_ERROR;
  }
  *context->error = message == nullptr ? std::string() : std::string(message);
  return X3_STATUS_OK;
}

X3Status host_raise_class_error(X3CallContext* context, const char* class_name, const char* message) {
  if (context == nullptr || context->runtime == nullptr || class_name == nullptr) {
    return X3_STATUS_ERROR;
  }
  const std::string text = message == nullptr ? std::string() : std::string(message);
  if (context->error != nullptr) {
    *context->error = text;
  }
  if (context->exception != nullptr) {
    *context->exception = context->runtime->make_exception(class_name, text);
  } else {
    context->runtime->raise_class_error(class_name, text);
  }
  return X3_STATUS_OK;
}

X3Status host_raise_error(X3CallContext* context, X3Value exception_class, const char* message) {
  if (context == nullptr || context->runtime == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  Value klass = from_c_value(exception_class, error);
  if (!error.empty() || value_as_class(klass) == nullptr) {
    return X3_STATUS_ERROR;
  }
  const std::string text = message == nullptr ? std::string() : std::string(message);
  if (context->error != nullptr) {
    *context->error = text;
  }
  if (context->exception != nullptr) {
    *context->exception = context->runtime->make_exception_from_class(std::move(klass), text);
  } else {
    context->runtime->set_pending_exception(context->runtime->make_exception_from_class(std::move(klass), text));
  }
  return X3_STATUS_OK;
}

const X3PackageHost kPackageHost = {
    X3_ABI_VERSION,
    sizeof(X3PackageHost),
    host_add_module,
    host_module_add_value,
    host_module_add_function,
    host_set_error,
    host_raise_class_error,
    host_raise_error,
    x3_runtime_last_error,
    x3_value_release,
    x3_value_string,
    x3_value_list,
    x3_value_dict,
    x3_value_to_cstr,
    x3_value_object_kind,
    x3_len,
    x3_get_item,
    host_set_attr,
    x3_list_append,
    x3_dict_set_item,
    x3_dict_get_entry,
    host_module_add_class,
    host_builtin_value,
    host_class_set_base,
    host_instance_set_native_data,
    host_instance_get_native_data,
    host_value_instance,
    host_package_set_cleanup,
    host_package_set_metadata,
};

std::vector<std::filesystem::path> collect_native_library_candidates(
    Runtime& runtime,
    const std::string& name,
    NativePackageLookupMode mode) {
  std::vector<std::filesystem::path> out;
  out.reserve(runtime.import_roots().size() * 6);
  for (const auto& root : runtime.import_roots()) {
    std::vector<std::string> package_names;
    if (mode == NativePackageLookupMode::ExactNameOnly) {
      package_names.push_back(name);
    } else {
      package_names = native_package_name_candidates(name);
    }
    for (const auto& package_name : package_names) {
      auto candidates = native_library_candidates(root, package_name);
      out.insert(out.end(), candidates.begin(), candidates.end());
    }
  }
  return out;
}

bool find_native_library(const std::vector<std::filesystem::path>& candidates, std::filesystem::path& out) {
  std::error_code ec;
  for (const auto& candidate : candidates) {
    if (std::filesystem::is_regular_file(candidate, ec)) {
      out = candidate;
      return true;
    }
  }
  return false;
}

std::string format_native_not_found(const std::string& name, const std::vector<std::filesystem::path>& candidates) {
  std::ostringstream os;
  os << "module '" << name << "' not found; native package candidates tried:";
  if (candidates.empty()) {
    os << " <none>";
    return os.str();
  }
  for (const auto& candidate : candidates) {
    os << "\n  " << candidate.string();
  }
  return os.str();
}

} // namespace

bool import_native_package(
    Runtime& runtime,
    const std::string& package_name,
    NativePackageLookupMode mode,
    Value& out,
    std::string& error) {
  std::filesystem::path library_path;
  auto candidates = collect_native_library_candidates(runtime, package_name, mode);
  if (!find_native_library(candidates, library_path)) {
    error = format_native_not_found(package_name, candidates);
    return false;
  }

  void* handle = open_library(library_path, error);
  if (handle == nullptr) {
    return false;
  }

  auto* init = reinterpret_cast<X3PackageInitFn>(find_symbol(handle, "x3_package_init"));
  if (init == nullptr) {
    error = "native package " + library_path.string() + " does not export x3_package_init";
    return false;
  }

  X3Package package;
  package.runtime = &runtime;
  package.name = package_name;
  package.root_module = Value::module(package_name);
  package.requested_module = Value::invalid();
  module_set_attr(package.root_module, "__name__", Value::string(package_name), error);
  module_set_attr(package.root_module, "__file__", Value::string(library_path.string()), error);
  module_set_attr(package.root_module, "__xlang3_file__", Value::string(library_path.string()), error);

  if (init(&kPackageHost, &package) != X3_STATUS_OK) {
    if (package.cleanup != nullptr) {
      package.cleanup(package.cleanup_data);
      package.cleanup = nullptr;
      package.cleanup_data = nullptr;
    }
    error = runtime.last_error().empty() ? "native package init failed" : runtime.last_error();
    return false;
  }

  if (package.cleanup != nullptr) {
    runtime.register_native_package_cleanup(package.cleanup_data, package.cleanup);
  }
  loaded_library_handles().push_back(handle);
  if (package.requested_module.tag != ValueTag::Invalid) {
    runtime.register_module(package_name, package.requested_module);
    out = std::move(package.requested_module);
  } else {
    runtime.register_module(package_name, package.root_module);
    out = std::move(package.root_module);
  }
  return true;
}

} // namespace xlang3
