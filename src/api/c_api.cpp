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
#include "xlang3/xapi.h"
#include "xlang3/xmodule.h"

#include "xlang3/attribute.h"
#include "xlang3/c_api_bridge.h"
#include "xlang3/interpreter.h"
#include "xlang3/ir.h"
#include "xlang3/module_object.h"
#include "xlang3/native_call_context.h"
#include "xlang3/parser.h"
#include "xlang3/runtime.h"
#include "xlang3/sema.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

xlang3::Runtime* as_runtime(X3Runtime* runtime) {
  return reinterpret_cast<xlang3::Runtime*>(runtime);
}

X3Runtime* as_c_runtime(xlang3::Runtime* runtime) {
  return reinterpret_cast<X3Runtime*>(runtime);
}

X3Status fail(xlang3::Runtime* runtime, std::string error) {
  if (runtime != nullptr) {
    runtime->set_last_error(std::move(error));
  }
  return X3_STATUS_ERROR;
}

bool read_file(const char* path, std::string& source, std::string& error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = std::string("cannot open ") + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  source = buffer.str();
  return true;
}

} // namespace

extern "C" {

X3Runtime* x3_runtime_create(void) {
  return as_c_runtime(new xlang3::Runtime(std::cout));
}

void x3_runtime_destroy(X3Runtime* runtime) {
  delete as_runtime(runtime);
}

const char* x3_runtime_last_error(X3Runtime* runtime) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr) {
    return "runtime is null";
  }
  return rt->last_error().c_str();
}

X3Status x3_runtime_add_import_root(X3Runtime* runtime, const char* path) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || path == nullptr) {
    return fail(rt, "runtime/path is null");
  }
  rt->add_import_root(std::filesystem::path(path));
  return X3_STATUS_OK;
}

X3Status x3_runtime_eval_file(X3Runtime* runtime, const char* path, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || path == nullptr || result == nullptr) {
    return fail(rt, "runtime/path/result is null");
  }

  std::string source;
  std::string error;
  if (!read_file(path, source, error)) {
    return fail(rt, error);
  }

  auto parsed = xlang3::parse_source(source);
  if (!parsed.errors.empty()) {
    return fail(rt, parsed.errors.front());
  }
  auto lowered = xlang3::lower_to_ir(parsed.module);
  if (!lowered.errors.empty()) {
    return fail(rt, lowered.errors.front());
  }

  rt->add_import_root(std::filesystem::path(path).parent_path());
  xlang3::Interpreter interpreter(*rt);
  auto exec_result = interpreter.run(lowered.module);
  if (!exec_result.errors.empty()) {
    return fail(rt, exec_result.errors.front());
  }
  *result = xlang3::to_c_value(exec_result.value);
  return X3_STATUS_OK;
}

void x3_value_retain(X3Value value) {
  if (value.tag == X3_TAG_OBJECT && value.as.obj != nullptr) {
    xlang3::Value internal;
    internal.tag = xlang3::ValueTag::Object;
    internal.as.obj = reinterpret_cast<xlang3::Object*>(value.as.obj);
    xlang3::retain(internal);
    internal.tag = xlang3::ValueTag::Invalid;
  }
}

void x3_value_release(X3Value value) {
  if (value.tag == X3_TAG_OBJECT && value.as.obj != nullptr) {
    xlang3::Value internal;
    internal.tag = xlang3::ValueTag::Object;
    internal.as.obj = reinterpret_cast<xlang3::Object*>(value.as.obj);
    xlang3::release(internal);
    internal.tag = xlang3::ValueTag::Invalid;
  }
}

X3Value x3_value_string(X3Runtime* runtime, const char* value) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || value == nullptr) {
    return x3_value_invalid();
  }
  return xlang3::to_c_value(xlang3::Value::string(value));
}

const char* x3_value_to_cstr(X3Runtime* runtime, X3Value value) {
  thread_local std::string rendered;
  auto* rt = as_runtime(runtime);
  std::string error;
  auto internal = xlang3::from_c_value(value, error);
  if (!error.empty()) {
    if (rt != nullptr) {
      rt->set_last_error(error);
    }
    return nullptr;
  }
  rendered = xlang3::value_to_string(internal);
  return rendered.c_str();
}

X3Status x3_runtime_import_module(X3Runtime* runtime, const char* package_name, const char* module_name, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr) {
    return fail(rt, "runtime/result is null");
  }
  const char* import_name = package_name != nullptr && package_name[0] != '\0' ? package_name : module_name;
  if (import_name == nullptr || import_name[0] == '\0') {
    return fail(rt, "module name is empty");
  }

  xlang3::Value module;
  std::string error;
  if (!rt->import_module(import_name, module, error)) {
    return fail(rt, error);
  }

  if (module_name != nullptr && module_name[0] != '\0' && std::string(module_name) != import_name) {
    xlang3::Value child;
    if (!xlang3::module_get_attr(module, module_name, child, error)) {
      return fail(rt, error);
    }
    *result = xlang3::to_c_value(child);
    return X3_STATUS_OK;
  }

  *result = xlang3::to_c_value(module);
  return X3_STATUS_OK;
}

X3Status x3_get_attr(X3Runtime* runtime, X3Value object, const char* name, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || name == nullptr || result == nullptr) {
    return fail(rt, "runtime/name/result is null");
  }
  std::string error;
  auto internal = xlang3::from_c_value(object, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  xlang3::Value attr;
  if (!xlang3::attribute_get(internal, name, attr, error)) {
    return fail(rt, error);
  }
  *result = xlang3::to_c_value(attr);
  return X3_STATUS_OK;
}

X3Status x3_set_attr(X3Runtime* runtime, X3Value object, const char* name, X3Value value) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || name == nullptr) {
    return fail(rt, "runtime/name is null");
  }
  std::string error;
  auto internal = xlang3::from_c_value(object, error);
  auto internal_value = xlang3::from_c_value(value, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  if (!xlang3::module_set_attr(internal, name, internal_value, error)) {
    return fail(rt, error);
  }
  return X3_STATUS_OK;
}

X3Status x3_call(X3Runtime* runtime, X3Value callable, const X3Value* args, uint32_t argc, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr || (argc != 0 && args == nullptr)) {
    return fail(rt, "runtime/args/result is null");
  }
  std::string error;
  auto internal_callable = xlang3::from_c_value(callable, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  auto* native = xlang3::value_as_native_function(internal_callable);
  if (native == nullptr || native->callback == nullptr) {
    return fail(rt, "only native function calls are supported by the C API in this phase");
  }

  std::vector<xlang3::Value> internal_args;
  internal_args.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    internal_args.push_back(xlang3::from_c_value(args[i], error));
    if (!error.empty()) {
      return fail(rt, error);
    }
  }

  xlang3::Value out;
  if (!native->callback(*rt, internal_args.data(), argc, out, error, native->user_data)) {
    return fail(rt, error.empty() ? "native function failed" : error);
  }
  *result = xlang3::to_c_value(out);
  return X3_STATUS_OK;
}

void* x3_call_context_user_data(X3CallContext* context) {
  if (context == nullptr) {
    return nullptr;
  }
  return context->user_data;
}

X3Runtime* x3_call_context_runtime(X3CallContext* context) {
  if (context == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<X3Runtime*>(context->runtime);
}

} // extern "C"
