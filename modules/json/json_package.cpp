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
#include "json_convert.h"

#include "xlang3/xmodule.h"

#include <fstream>
#include <sstream>
#include <string>

#if defined(_WIN32)
#define X3_JSON_EXPORT __declspec(dllexport)
#else
#define X3_JSON_EXPORT __attribute__((visibility("default")))
#endif

namespace {

bool require_string(X3Runtime* runtime, X3Value value, const char* message, std::string& out, const X3PackageHost* host, X3CallContext* context) {
  if (host->value_object_kind(value) != X3_OBJECT_KIND_STRING) {
    host->set_error(context, message);
    return false;
  }
  out = host->value_to_cstr(runtime, value);
  return true;
}

X3Status json_loads(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = static_cast<const X3PackageHost*>(user_data);
  if (argc != 1) {
    host->set_error(context, "json.loads() expected 1 argument");
    return X3_STATUS_ERROR;
  }
  std::string text;
  if (!require_string(runtime, args[0], "json.loads() argument must be a string", text, host, context)) {
    return X3_STATUS_ERROR;
  }
  try {
    *result = xlang3_json::json_to_value(host, runtime, xlang3_json::Json::parse(text));
    return X3_STATUS_OK;
  } catch (const std::exception& ex) {
    const std::string error = std::string("json.loads() parse error: ") + ex.what();
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
}

X3Status json_dumps(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = static_cast<const X3PackageHost*>(user_data);
  if (argc != 1) {
    host->set_error(context, "json.dumps() expected 1 argument");
    return X3_STATUS_ERROR;
  }
  const char* error = nullptr;
  xlang3_json::Json json;
  if (!xlang3_json::value_to_json(host, runtime, args[0], json, &error)) {
    host->set_error(context, error);
    return X3_STATUS_ERROR;
  }
  *result = host->value_string(runtime, json.dump().c_str());
  return X3_STATUS_OK;
}

X3Status json_load(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = static_cast<const X3PackageHost*>(user_data);
  if (argc != 1) {
    host->set_error(context, "json.load() expected 1 argument");
    return X3_STATUS_ERROR;
  }
  std::string path;
  if (!require_string(runtime, args[0], "json.load() path must be a string", path, host, context)) {
    return X3_STATUS_ERROR;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    const std::string error = "json.load() cannot open " + path;
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  X3Value text = host->value_string(runtime, buffer.str().c_str());
  const X3Status status = json_loads(context, runtime, user_data, &text, 1, result);
  host->value_release(text);
  return status;
}

X3Status json_save(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* result) {
  auto* host = static_cast<const X3PackageHost*>(user_data);
  if (argc != 2) {
    host->set_error(context, "json.save() expected 2 arguments");
    return X3_STATUS_ERROR;
  }
  std::string path;
  if (!require_string(runtime, args[1], "json.save() path must be a string", path, host, context)) {
    return X3_STATUS_ERROR;
  }
  X3Value text = x3_value_invalid();
  const X3Status status = json_dumps(context, runtime, user_data, args, 1, &text);
  if (status != X3_STATUS_OK) {
    return status;
  }
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    host->value_release(text);
    const std::string error = "json.save() cannot open " + path;
    host->set_error(context, error.c_str());
    return X3_STATUS_ERROR;
  }
  file << host->value_to_cstr(runtime, text);
  host->value_release(text);
  *result = x3_value_bool(1);
  return X3_STATUS_OK;
}

void add_function(const X3PackageHost* host, X3Module* module, const char* name, X3NativeFn callback) {
  X3NativeFunctionDef def{};
  def.size = sizeof(def);
  def.name = name;
  def.callback = callback;
  def.user_data = const_cast<X3PackageHost*>(host);
  host->module_add_function(module, &def);
}

} // namespace

extern "C" X3_JSON_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" X3_JSON_EXPORT X3Status Load(void* host_ptr, X3Value curModule) {
  auto* host = static_cast<X3PackageHost*>(host_ptr);
  (void)curModule;
  if (host == nullptr || host->abi_version != X3_ABI_VERSION) {
    return X3_STATUS_ERROR;
  }
  host->package_set_metadata(host, "package", "xlang_json");
  host->package_set_metadata(host, "version", "0.1.0");
  host->package_set_metadata(host, "abi", "10");
  X3Module* json = nullptr;
  if (host->add_module(host, "json", &json) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  add_function(host, json, "loads", json_loads);
  add_function(host, json, "dumps", json_dumps);
  add_function(host, json, "load", json_load);
  add_function(host, json, "save", json_save);
  return X3_STATUS_OK;
}
