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
#include "xlang3/mapping.h"

#include <algorithm>
#include <cstring>
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
void ostream_output_write(void* context, const char* data, std::size_t size) {
  auto* out = static_cast<std::ostream*>(context);
  out->write(data, static_cast<std::streamsize>(size));
}

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

void Runtime::initialize() {
  modules_dict_ = Value::dict({});
  register_core_builtins(*this);
#if !defined(XLANG3_EMBEDDED)
  add_default_import_layout(*this, runtime_library_dir());
#endif
}

#if !defined(XLANG3_EMBEDDED)
Runtime::Runtime(std::ostream& out)
    : output_{&out, ostream_output_write},
      vfs_(std::make_unique<Vfs>()) {
  initialize();
}
#endif

Runtime::Runtime(OutputSink output)
    : output_(output),
      vfs_(std::make_unique<Vfs>()) {
  initialize();
}

Runtime::~Runtime() {
  std::string ignored;
  run_exit_functions(ignored);
  value_set_invalid(pending_exception_);
  value_set_invalid(active_exception_);
  value_set_invalid(current_globals_module_);
  for (auto it = native_package_cleanups_.rbegin(); it != native_package_cleanups_.rend(); ++it) {
    if (it->second != nullptr) {
      it->second(it->first);
    }
  }
}

void Runtime::write_output(const char* data, std::size_t size) {
  if (data == nullptr || size == 0 || output_.write == nullptr) {
    return;
  }
  output_.write(output_.context, data, size);
}

void Runtime::write_output(const char* text) {
  if (text == nullptr) {
    return;
  }
  write_output(text, std::strlen(text));
}

void Runtime::register_builtin(std::string name, Value value) {
  builtins_[std::move(name)] = std::move(value);
}

void Runtime::register_native_builtin(
    std::string name,
    NativeFunctionCallback callback,
    NativeFastCallCallback fast_callback,
    bool fast_releases_vm_lock,
    NativeKeywordFunctionCallback keyword_callback) {
  auto function_value = make_native_function(
      name,
      callback,
      nullptr,
      nullptr,
      fast_callback,
      fast_releases_vm_lock,
      keyword_callback);
  register_builtin(std::move(name), std::move(function_value));
}

const Value* Runtime::find_builtin(const std::string& name) const {
  auto it = builtins_.find(name);
  if (it == builtins_.end()) {
    return nullptr;
  }
  return &it->second;
}

void Runtime::set_current_globals_module(const Value& globals_module) {
  value_assign_fast(current_globals_module_, globals_module);
}

void Runtime::set_current_frame_locals(const std::vector<std::string>* names, const Value* values, size_t count) {
  current_local_names_ = names;
  current_local_values_ = values;
  current_local_count_ = count;
}

void Runtime::clear_current_frame_locals() {
  current_local_names_ = nullptr;
  current_local_values_ = nullptr;
  current_local_count_ = 0;
}

Value Runtime::current_locals_snapshot() const {
  if (current_local_names_ == nullptr || current_local_values_ == nullptr) {
    return Value::dict({});
  }
  const size_t count =
      current_local_count_ < current_local_names_->size() ? current_local_count_ : current_local_names_->size();
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    const auto& name = (*current_local_names_)[i];
    if (name.empty() || name[0] == '#') {
      continue;
    }
    if (current_local_values_[i].tag == ValueTag::Invalid) {
      continue;
    }
    entries.push_back({Value::string(name), current_local_values_[i]});
  }
  return Value::dict(std::move(entries));
}

void Runtime::register_exit_function(Value callable, std::vector<Value> args) {
  exit_functions_.push_back(ExitFunction{std::move(callable), std::move(args)});
}

void Runtime::unregister_exit_function(const Value& callable) {
  exit_functions_.erase(
      std::remove_if(
          exit_functions_.begin(),
          exit_functions_.end(),
          [&](const ExitFunction& entry) {
            if (entry.callable.tag != callable.tag) {
              return false;
            }
            if (entry.callable.tag == ValueTag::Object) {
              return entry.callable.as.obj == callable.as.obj;
            }
            if (entry.callable.tag == ValueTag::Int64) {
              return entry.callable.as.i64 == callable.as.i64;
            }
            return false;
          }),
      exit_functions_.end());
}

Value Runtime::make_native_function(
    std::string name,
    NativeFunctionCallback callback,
    void* user_data,
    void (*user_data_cleanup)(void*),
    NativeFastCallCallback fast_callback,
    bool fast_releases_vm_lock,
    NativeKeywordFunctionCallback keyword_callback) {
  const uint32_t native_id = next_native_id_++;
  return Value::native_function(
      native_id,
      std::move(name),
      callback,
      user_data,
      user_data_cleanup,
      fast_callback,
      fast_releases_vm_lock,
      keyword_callback);
}

void Runtime::register_module(std::string name, Value module) {
  std::string key = name;
  modules_[std::move(name)] = std::move(module);
  auto it = modules_.find(key);
  if (it != modules_.end() && modules_dict_.tag != ValueTag::Invalid) {
    std::string ignored;
    mapping_set_item(modules_dict_, Value::string(key), it->second, ignored);
  }
}

void Runtime::unregister_module(const std::string& name) {
  modules_.erase(name);
  if (modules_dict_.tag != ValueTag::Invalid) {
    std::string ignored;
    mapping_delete_item(modules_dict_, Value::string(name), ignored);
  }
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

bool Runtime::has_registered_module(const std::string& name) const {
  return modules_.find(name) != modules_.end();
}

bool Runtime::import_from(const std::string& module_name, const std::string& attr_name, Value& out, std::string& error) {
  std::string resolved_module = module_name;
  while (!resolved_module.empty() && resolved_module.front() == '.') {
    resolved_module.erase(resolved_module.begin());
  }
  Value module;
  if (!import_module(resolved_module, module, error)) {
    return false;
  }
  if (module_get_attr(module, attr_name, out, error)) {
    return true;
  }

  std::string submodule_error;
  if (import_module(resolved_module.empty() ? attr_name : resolved_module + "." + attr_name, out, submodule_error)) {
    return true;
  }
  return false;
}

bool Runtime::import_star(const std::string& module_name, Value& target_module, std::string& error) {
  std::string resolved_module = module_name;
  while (!resolved_module.empty() && resolved_module.front() == '.') {
    resolved_module.erase(resolved_module.begin());
  }
  Value module;
  if (!import_module(resolved_module, module, error)) {
    return false;
  }
  auto* source = value_as_module(module);
  if (source == nullptr) {
    error = "star import source is not a module";
    return false;
  }
  auto* target = value_as_module(target_module);
  if (target == nullptr) {
    error = "star import target is not a module";
    return false;
  }
  for (const auto& item : source->name_to_slot) {
    const std::string& name = item.first;
    const uint32_t slot = item.second;
    if (name.empty() || name[0] == '_' || slot >= source->slots.size()) {
      continue;
    }
    if (!module_set_attr(target_module, name, source->slots[slot], error)) {
      return false;
    }
  }
  return true;
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
