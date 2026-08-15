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
#include "xlang3/import_loader.h"
#include "xlang3/module_object.h"
#include "xlang3/native_package_loader.h"

namespace xlang3 {

Runtime::Runtime(std::ostream& out) : out_(out) {
  register_core_builtins(*this);
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

bool Runtime::import_module(const std::string& name, Value& out, std::string& error) {
  auto it = modules_.find(name);
  if (it == modules_.end()) {
    std::string python_error;
    if (import_python_module(*this, name, out, python_error)) {
      return true;
    }
    std::string native_error;
    if (import_native_package(*this, name, out, native_error)) {
      return true;
    }
    error = native_error.empty() ? python_error : native_error;
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

void Runtime::add_import_root(std::filesystem::path root) {
  import_roots_.push_back(std::move(root));
}

} // namespace xlang3
