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
#include <utility>
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
  std::vector<std::pair<void*, X3PackageCleanup>> cleanups;
};

namespace xlang3 {

namespace {

struct NativeThunk {
  X3NativeFn callback = nullptr;
  void* user_data = nullptr;
};

struct NativePackageCleanupState {
  X3PackageHost* host = nullptr;
  std::string package_name;
  std::string library_path;
  std::vector<std::pair<void*, X3PackageCleanup>> cleanups;
};

void cleanup_native_package_state(void* data) {
  auto* state = static_cast<NativePackageCleanupState*>(data);
  if (state == nullptr) {
    return;
  }
  for (auto it = state->cleanups.rbegin(); it != state->cleanups.rend(); ++it) {
    if (it->second != nullptr) {
      it->second(it->first);
    }
  }
  delete state->host;
  delete state;
}

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

bool is_python_source_root(const std::filesystem::path& root) {
  const auto leaf = root.filename().string();
  if (leaf == "Lib" || leaf == "lib" || leaf == "site-packages") {
    return true;
  }
  const auto parent = root.parent_path().filename().string();
  return parent == "site-packages";
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
  if (name == "_sqlite3") {
    out.push_back("xlang_sqlite3");
  }
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

X3Package* package_from_host(X3PackageHost* host) {
  return host == nullptr ? nullptr : static_cast<X3Package*>(host->package_context);
}

X3Status host_add_module(X3PackageHost* host, const char* name, X3Module** out_module) {
  X3Package* package = package_from_host(host);
  if (package == nullptr || package->runtime == nullptr || name == nullptr || out_module == nullptr) {
    return X3_STATUS_ERROR;
  }
  auto module = std::make_unique<X3Module>();
  module->runtime = package->runtime;
  module->name = name;
  module->value = Value::module(name);
  apply_package_metadata(*package, *module);

  std::string error;
  const std::string module_name(name);
  const std::string package_prefix = package->name + ".";
  if (module_name.rfind(package_prefix, 0) == 0) {
    const std::string attr_name = module_name.substr(package_prefix.size());
    const auto dot = attr_name.find('.');
    module_set_attr(package->root_module, dot == std::string::npos ? attr_name : attr_name.substr(0, dot), module->value, error);
    package->runtime->register_module(module_name, module->value);
  } else {
    module_set_attr(package->root_module, module_name, module->value, error);
    package->runtime->register_module(module_name, module->value);
    package->runtime->register_module(package->name + "." + module_name, module->value);
  }
  if (module->name == package->name || (package->name == "_sqlite3" && module->name == "sqlite3")) {
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

X3Status host_module_get_value(X3Module* module, X3Value* out_value) {
  if (module == nullptr || out_value == nullptr) {
    return X3_STATUS_ERROR;
  }
  *out_value = to_c_value(module->value);
  return X3_STATUS_OK;
}

X3Status host_module_get_attr(X3Module* module, const char* name, X3Value* out_value) {
  if (module == nullptr || name == nullptr || out_value == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  Value out;
  if (!module_get_attr(module->value, name, out, error)) {
    if (module->runtime != nullptr) {
      module->runtime->set_last_error(error);
    }
    return X3_STATUS_ERROR;
  }
  *out_value = to_c_value(out);
  return X3_STATUS_OK;
}

X3Status host_module_set_attr(X3Module* module, const char* name, X3Value value) {
  if (module == nullptr || name == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  Value internal = from_c_value(value, error);
  if (!error.empty() || !module_set_attr(module->value, name, internal, error)) {
    if (module->runtime != nullptr) {
      module->runtime->set_last_error(error);
    }
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

X3Status host_builtin_value(X3PackageHost* host, const char* name, X3Value* out_value) {
  X3Package* package = package_from_host(host);
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

X3Status host_package_set_cleanup(X3PackageHost* host, void* data, X3PackageCleanup cleanup) {
  X3Package* package = package_from_host(host);
  if (package == nullptr) {
    return X3_STATUS_ERROR;
  }
  if (cleanup != nullptr) {
    package->cleanups.emplace_back(data, cleanup);
  }
  return X3_STATUS_OK;
}

X3Status host_class_add_value(X3Value klass, const char* name, X3Value value) {
  if (name == nullptr) {
    return X3_STATUS_ERROR;
  }
  std::string error;
  Value internal_class = from_c_value(klass, error);
  if (!error.empty() || value_as_class(internal_class) == nullptr) {
    return X3_STATUS_ERROR;
  }
  Value internal_value = from_c_value(value, error);
  if (!error.empty()) {
    return X3_STATUS_ERROR;
  }
  return attribute_set(internal_class, name, internal_value, error) ? X3_STATUS_OK : X3_STATUS_ERROR;
}

X3Status host_property_create(
    X3Runtime* runtime,
    const char* name,
    X3NativeFn getter,
    X3NativeFn setter,
    void* user_data,
    X3Value* result) {
  auto* rt = reinterpret_cast<Runtime*>(runtime);
  if (rt == nullptr || name == nullptr || getter == nullptr || result == nullptr) {
    return X3_STATUS_ERROR;
  }
  auto* get_thunk = new NativeThunk();
  get_thunk->callback = getter;
  get_thunk->user_data = user_data;
  Value fget = rt->make_native_function(
      std::string(name) + ".get",
      native_function_bridge,
      get_thunk,
      [](void* data) { delete static_cast<NativeThunk*>(data); });

  Value fset = Value::none();
  if (setter != nullptr) {
    auto* set_thunk = new NativeThunk();
    set_thunk->callback = setter;
    set_thunk->user_data = user_data;
    fset = rt->make_native_function(
        std::string(name) + ".set",
        native_function_bridge,
        set_thunk,
        [](void* data) { delete static_cast<NativeThunk*>(data); });
  }

  *result = to_c_value(Value::property(std::move(fget), std::move(fset), Value::none(), Value::none()));
  return X3_STATUS_OK;
}

X3Status host_package_set_metadata(X3PackageHost* host, const char* key, const char* value) {
  X3Package* package = package_from_host(host);
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

const X3PackageHost kPackageHostTemplate = {
    X3_ABI_VERSION,
    sizeof(X3PackageHost),
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    host_add_module,
    host_module_add_value,
    host_module_add_function,
    host_set_error,
    host_raise_class_error,
    host_raise_error,
    x3_runtime_last_error,
    x3_value_retain,
    x3_value_release,
    x3_value_string,
    x3_value_bytes,
    x3_value_list,
    x3_value_dict,
    x3_value_to_cstr,
    x3_value_object_kind,
    x3_value_bytes_data,
    x3_value_to_bytes,
    x3_value_from_bytes,
    x3_event_create,
    x3_event_subscribe,
    x3_event_unsubscribe,
    x3_event_fire,
    x3_call,
    x3_len,
    x3_get_attr,
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
    host_class_add_value,
    host_property_create,
    host_package_set_metadata,
    host_module_get_value,
    host_module_get_attr,
    host_module_set_attr,
};

std::vector<std::filesystem::path> collect_native_library_candidates(
    Runtime& runtime,
    const std::string& name,
    NativePackageLookupMode mode) {
  std::vector<std::filesystem::path> out;
  out.reserve(runtime.import_roots().size() * 6);
  for (const auto& root : runtime.import_roots()) {
    if (is_python_source_root(root)) {
      continue;
    }
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

void apply_sqlite3_compat_attrs(const std::string& package_name, Value& module) {
  if (package_name != "_sqlite3" && package_name != "sqlite3") {
    return;
  }
  std::string ignored;
  module_set_attr(module, "sqlite_version", Value::string("3.0.0"), ignored);
  module_set_attr(module, "version", Value::string("2.6.0"), ignored);
  module_set_attr(module, "paramstyle", Value::string("qmark"), ignored);
  module_set_attr(module, "apilevel", Value::string("2.0"), ignored);
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

  auto* init = reinterpret_cast<X3PackageInitFn>(find_symbol(handle, "Load"));
  if (init == nullptr) {
    error = "native package " + library_path.string() + " does not export Load";
    return false;
  }

  auto* abi_version = static_cast<const uint32_t*>(find_symbol(handle, "xlang3_package_abi_version"));
  if (abi_version == nullptr) {
    error = "native package " + library_path.string() + " does not export xlang3_package_abi_version";
    return false;
  }
  if (*abi_version != X3_ABI_VERSION) {
    error = "native package " + library_path.string() + " has xlang3 ABI " + std::to_string(*abi_version) +
            ", runtime expects " + std::to_string(X3_ABI_VERSION);
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

  auto* host = new X3PackageHost(kPackageHostTemplate);
  host->package_context = &package;
  host->runtime = reinterpret_cast<X3Runtime*>(&runtime);
  host->package_name = package.name.c_str();
  const std::string library_path_text = library_path.string();
  host->library_path = library_path_text.c_str();

  X3Value cur_module = to_c_value(runtime.current_globals_module());
  const X3Status init_status = init(host, cur_module);
  x3_value_release(cur_module);
  if (init_status != X3_STATUS_OK) {
    for (auto it = package.cleanups.rbegin(); it != package.cleanups.rend(); ++it) {
      if (it->second != nullptr) {
        it->second(it->first);
      }
    }
    package.cleanups.clear();
    delete host;
    error = runtime.last_error().empty() ? "native package init failed" : runtime.last_error();
    return false;
  }

  auto* cleanup_state = new NativePackageCleanupState();
  cleanup_state->host = host;
  cleanup_state->package_name = package.name;
  cleanup_state->library_path = library_path_text;
  cleanup_state->cleanups = std::move(package.cleanups);
  host->package_name = cleanup_state->package_name.c_str();
  host->library_path = cleanup_state->library_path.c_str();
  host->package_context = nullptr;
  runtime.register_native_package_cleanup(cleanup_state, cleanup_native_package_state);
  loaded_library_handles().push_back(handle);
  if (package.requested_module.tag != ValueTag::Invalid) {
    apply_sqlite3_compat_attrs(package_name, package.requested_module);
    runtime.register_module(package_name, package.requested_module);
    out = std::move(package.requested_module);
  } else {
    apply_sqlite3_compat_attrs(package_name, package.root_module);
    runtime.register_module(package_name, package.root_module);
    out = std::move(package.root_module);
  }
  return true;
}

} // namespace xlang3
