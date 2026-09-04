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
#include "xlang3/xmodule.h"

#include <string>

#if defined(_WIN32)
#define X3_CURMODULE_PROBE_EXPORT __declspec(dllexport)
#else
#define X3_CURMODULE_PROBE_EXPORT __attribute__((visibility("default")))
#endif

namespace {

struct ProbeState {
  X3PackageHost* host = nullptr;
  std::string module_name;
  std::string module_file;
};

void cleanup_probe_state(void* data) {
  delete static_cast<ProbeState*>(data);
}

std::string read_module_attr(X3PackageHost* host, X3Value module, const char* name) {
  if (host == nullptr || host->runtime == nullptr || module.tag == X3_TAG_INVALID) {
    return {};
  }
  X3Value value = x3_value_invalid();
  if (host->get_attr(host->runtime, module, name, &value) != X3_STATUS_OK) {
    return {};
  }
  const char* text = host->value_to_cstr(host->runtime, value);
  std::string out = text == nullptr ? std::string() : std::string(text);
  host->value_release(value);
  return out;
}

X3Status probe_curmodule_name(
    X3CallContext*,
    X3Runtime* runtime,
    void* user_data,
    const X3Value*,
    uint32_t,
    X3Value* result) {
  auto* state = static_cast<ProbeState*>(user_data);
  *result = state->host->value_string(runtime, state->module_name.c_str());
  return X3_STATUS_OK;
}

X3Status probe_curmodule_file(
    X3CallContext*,
    X3Runtime* runtime,
    void* user_data,
    const X3Value*,
    uint32_t,
    X3Value* result) {
  auto* state = static_cast<ProbeState*>(user_data);
  *result = state->host->value_string(runtime, state->module_file.c_str());
  return X3_STATUS_OK;
}

void add_function(X3PackageHost* host, X3Module* module, const char* name, X3NativeFn callback, ProbeState* state) {
  X3NativeFunctionDef def{};
  def.size = sizeof(def);
  def.name = name;
  def.callback = callback;
  def.user_data = state;
  host->module_add_function(module, &def);
}

} // namespace

extern "C" X3_CURMODULE_PROBE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" X3_CURMODULE_PROBE_EXPORT X3Status Load(void* host_ptr, X3Value curModule) {
  auto* host = static_cast<X3PackageHost*>(host_ptr);
  if (host == nullptr || host->abi_version != X3_ABI_VERSION) {
    return X3_STATUS_ERROR;
  }

  auto* state = new ProbeState();
  state->host = host;
  state->module_name = read_module_attr(host, curModule, "__name__");
  state->module_file = read_module_attr(host, curModule, "__file__");
  host->package_set_cleanup(host, state, cleanup_probe_state);
  host->package_set_metadata(host, "package", "xlang_curmodule_probe");
  host->package_set_metadata(host, "abi", "10");

  X3Module* module = nullptr;
  if (host->add_module(host, "xlang_curmodule_probe", &module) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  add_function(host, module, "curmodule_name", probe_curmodule_name, state);
  add_function(host, module, "curmodule_file", probe_curmodule_file, state);
  return X3_STATUS_OK;
}
