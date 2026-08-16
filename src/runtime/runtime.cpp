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
#include "xlang3/runtime.h"

#include "xlang3/builtins.h"
#if !defined(XLANG3_EMBEDDED)
#include "xlang3/import_loader.h"
#include "xlang3/native_package_loader.h"
#endif
#include "xlang3/module_object.h"

#include <algorithm>
#include <utility>

#if !defined(XLANG3_EMBEDDED)
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <dlfcn.h>
#endif
#endif

namespace xlang3 {

namespace {

#if !defined(XLANG3_EMBEDDED)
void runtime_module_anchor() {}

std::filesystem::path runtime_library_dir() {
#if defined(_WIN32)
  HMODULE module = nullptr;
  if (GetModuleHandleExA(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCSTR>(&runtime_module_anchor),
          &module) != 0) {
    char path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
      return std::filesystem::path(path).parent_path();
    }
  }
#elif defined(__APPLE__) || defined(__linux__)
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&runtime_module_anchor), &info) != 0 && info.dli_fname != nullptr) {
    return std::filesystem::path(info.dli_fname).parent_path();
  }
#endif
  return {};
}

void add_default_import_layout(Runtime& runtime, const std::filesystem::path& base) {
  if (base.empty()) {
    return;
  }
  runtime.add_import_root(base / "lib");
  runtime.add_import_root(base / "modules");
  runtime.add_import_root(base / "site-packages");
  runtime.add_import_root(base);
}

std::filesystem::path normalize_import_root(std::filesystem::path root) {
  if (root.empty()) {
    root = std::filesystem::current_path();
  }
  std::error_code ec;
  auto absolute = std::filesystem::absolute(root, ec);
  if (!ec) {
    root = std::move(absolute);
  }
  auto normalized = root.lexically_normal();
  return normalized.empty() ? root : normalized;
}

bool has_import_root(const std::vector<std::filesystem::path>& roots, const std::filesystem::path& root) {
  return std::find(roots.begin(), roots.end(), root) != roots.end();
}
#endif

} // namespace

Runtime::Runtime(std::ostream& out) : out_(out) {
  register_core_builtins(*this);
#if !defined(XLANG3_EMBEDDED)
  add_default_import_layout(*this, runtime_library_dir());
#endif
}

Runtime::~Runtime() {
  modules_.clear();
  builtins_.clear();
  value_set_invalid(pending_exception_);
  for (auto it = native_package_cleanups_.rbegin(); it != native_package_cleanups_.rend(); ++it) {
    if (it->second != nullptr) {
      it->second(it->first);
    }
  }
}

void Runtime::register_builtin(std::string name, Value value) {
  builtins_[std::move(name)] = std::move(value);
}

void Runtime::register_native_builtin(std::string name, NativeFunctionCallback callback) {
  auto function_value = make_native_function(name, callback);
  register_builtin(std::move(name), std::move(function_value));
}

const Value* Runtime::find_builtin(const std::string& name) const {
  auto it = builtins_.find(name);
  if (it == builtins_.end()) {
    return nullptr;
  }
  return &it->second;
}

Value Runtime::make_native_function(
    std::string name,
    NativeFunctionCallback callback,
    void* user_data,
    void (*user_data_cleanup)(void*)) {
  const uint32_t native_id = next_native_id_++;
  return Value::native_function(native_id, std::move(name), callback, user_data, user_data_cleanup);
}

void Runtime::register_module(std::string name, Value module) {
  modules_[std::move(name)] = std::move(module);
}

void Runtime::unregister_module(const std::string& name) {
  modules_.erase(name);
}

void Runtime::register_native_package_cleanup(void* data, void (*cleanup)(void*)) {
  if (cleanup == nullptr) {
    return;
  }
  native_package_cleanups_.push_back(std::make_pair(data, cleanup));
}

void Runtime::register_raw_block_handler(std::string language, std::string provider, RawBlockHandler handler) {
  raw_block_handlers_[std::move(language) + "\n" + std::move(provider)] = handler;
}

bool Runtime::execute_raw_block(
    RawBlockContext& context,
    const std::string& language,
    const std::string& provider,
    const std::string& body,
    std::string& error) {
  auto it = raw_block_handlers_.find(language + "\n" + provider);
  if (it == raw_block_handlers_.end()) {
    it = raw_block_handlers_.find(language + "\n");
  }
  if (it == raw_block_handlers_.end() || it->second == nullptr) {
    error = "no raw block provider registered for '" + language + " " + provider + "'";
    return false;
  }
  return it->second(*this, context, language, provider, body, error);
}

bool Runtime::import_module(const std::string& name, Value& out, std::string& error) {
  auto it = modules_.find(name);
  if (it == modules_.end()) {
#if !defined(XLANG3_EMBEDDED)
    std::string python_error;
    if (import_python_module(*this, name, out, python_error)) {
      return true;
    }
    std::string native_error;
    if (import_native_package(*this, name, out, native_error)) {
      return true;
    }
    error = native_error.empty() ? python_error : native_error;
#else
    error = "module '" + name + "' not found in embedded runtime";
#endif
    return false;
  }
  value_assign_fast(out, it->second);
  return true;
}

bool Runtime::import_from(const std::string& module_name, const std::string& attr_name, Value& out, std::string& error) {
  Value module;
  if (!import_module(module_name, module, error)) {
    return false;
  }
  if (module_get_attr(module, attr_name, out, error)) {
    return true;
  }

  std::string submodule_error;
  if (import_module(module_name + "." + attr_name, out, submodule_error)) {
    return true;
  }
  return false;
}

#if !defined(XLANG3_EMBEDDED)
void Runtime::add_import_root(std::filesystem::path root) {
  root = normalize_import_root(std::move(root));
  if (has_import_root(import_roots_, root)) {
    return;
  }
  import_roots_.push_back(std::move(root));
}

void Runtime::prepend_import_root(std::filesystem::path root) {
  root = normalize_import_root(std::move(root));
  if (has_import_root(import_roots_, root)) {
    return;
  }
  import_roots_.insert(import_roots_.begin(), std::move(root));
}
#endif

} // namespace xlang3
