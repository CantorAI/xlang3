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
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/native_call_context.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sequence.h"
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

bool call_native_function(
    xlang3::Runtime& runtime,
    xlang3::NativeFunctionObject* native,
    const std::vector<xlang3::Value>& args,
    xlang3::Value& out,
    std::string& error) {
  if (native == nullptr || native->callback == nullptr) {
    error = "native function callback is missing";
    return false;
  }
  if (!native->callback(
          runtime,
          args.empty() ? nullptr : args.data(),
          static_cast<uint32_t>(args.size()),
          out,
          error,
          native->user_data)) {
    xlang3::Value pending;
    if (runtime.take_pending_exception(pending)) {
      error = xlang3::value_to_string(pending);
    }
    if (error.empty()) {
      error = "native function failed";
    }
    return false;
  }
  return true;
}

bool call_function_object(
    xlang3::Runtime& runtime,
    xlang3::FunctionObject* function,
    const std::vector<xlang3::Value>& args,
    xlang3::Value& out,
    std::string& error) {
  xlang3::CallArgsView view;
  view.leading = args.empty() ? nullptr : args.data();
  view.leading_count = static_cast<uint32_t>(args.size());
  xlang3::Interpreter interpreter(runtime);
  auto result = interpreter.run_function_value(function, view);
  if (!result.errors.empty()) {
    error = result.errors.front();
    return false;
  }
  xlang3::value_assign_fast(out, result.value);
  return true;
}

bool call_value(
    xlang3::Runtime& runtime,
    const xlang3::Value& callable,
    const std::vector<xlang3::Value>& args,
    xlang3::Value& out,
    std::string& error);

bool construct_class(
    xlang3::Runtime& runtime,
    const xlang3::Value& klass_value,
    const std::vector<xlang3::Value>& args,
    xlang3::Value& out,
    std::string& error) {
  auto* klass = xlang3::value_as_class(klass_value);
  if (klass == nullptr) {
    error = "object is not a class";
    return false;
  }
  xlang3::Value instance = xlang3::Value::instance(klass_value);
  xlang3::Value init;
  if (xlang3::object_get_attr(klass_value, "__init__", init, error)) {
    std::vector<xlang3::Value> init_args;
    init_args.reserve(args.size() + 1);
    init_args.push_back(instance);
    for (const auto& arg : args) {
      init_args.push_back(arg);
    }
    xlang3::Value ignored;
    if (!call_value(runtime, init, init_args, ignored, error)) {
      return false;
    }
  } else {
    error.clear();
    if (!args.empty()) {
      error = "class construction expected no arguments";
      return false;
    }
  }
  xlang3::value_assign_fast(out, instance);
  return true;
}

bool call_value(
    xlang3::Runtime& runtime,
    const xlang3::Value& callable,
    const std::vector<xlang3::Value>& args,
    xlang3::Value& out,
    std::string& error) {
  if (auto* native = xlang3::value_as_native_function(callable)) {
    return call_native_function(runtime, native, args, out, error);
  }
  if (auto* function = xlang3::value_as_function(callable)) {
    return call_function_object(runtime, function, args, out, error);
  }
  if (auto* bound = xlang3::value_as_bound_method(callable)) {
    std::vector<xlang3::Value> bound_args;
    bound_args.reserve(args.size() + 1);
    bound_args.push_back(bound->self);
    for (const auto& arg : args) {
      bound_args.push_back(arg);
    }
    return call_value(runtime, bound->function, bound_args, out, error);
  }
  if (xlang3::value_as_class(callable) != nullptr) {
    return construct_class(runtime, callable, args, out, error);
  }
  error = "object is not callable";
  return false;
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

  rt->prepend_import_root(std::filesystem::path(path).parent_path());
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

X3Value x3_value_list(X3Runtime* runtime) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr) {
    return x3_value_invalid();
  }
  return xlang3::to_c_value(xlang3::Value::list({}));
}

X3Value x3_value_dict(X3Runtime* runtime) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr) {
    return x3_value_invalid();
  }
  return xlang3::to_c_value(xlang3::Value::dict({}));
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

X3ObjectKind x3_value_object_kind(X3Value value) {
  if (value.tag != X3_TAG_OBJECT || value.as.obj == nullptr) {
    return X3_OBJECT_KIND_UNKNOWN;
  }
  auto* object = reinterpret_cast<xlang3::Object*>(value.as.obj);
  switch (object->kind) {
    case xlang3::ObjectKind::String:
      return X3_OBJECT_KIND_STRING;
    case xlang3::ObjectKind::Tuple:
      return X3_OBJECT_KIND_TUPLE;
    case xlang3::ObjectKind::List:
      return X3_OBJECT_KIND_LIST;
    case xlang3::ObjectKind::Dict:
      return X3_OBJECT_KIND_DICT;
    case xlang3::ObjectKind::Instance:
      return X3_OBJECT_KIND_INSTANCE;
    default:
      return X3_OBJECT_KIND_UNKNOWN;
  }
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

  std::vector<xlang3::Value> internal_args;
  internal_args.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    internal_args.push_back(xlang3::from_c_value(args[i], error));
    if (!error.empty()) {
      return fail(rt, error);
    }
  }

  xlang3::Value out;
  if (!call_value(*rt, internal_callable, internal_args, out, error)) {
    return fail(rt, error);
  }
  *result = xlang3::to_c_value(out);
  return X3_STATUS_OK;
}

X3Status x3_len(X3Runtime* runtime, X3Value value, uint64_t* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr) {
    return fail(rt, "runtime/result is null");
  }
  std::string error;
  auto internal = xlang3::from_c_value(value, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  xlang3::Value length;
  if (!xlang3::sequence_len(internal, length, error) || length.tag != xlang3::ValueTag::Int64) {
    return fail(rt, error.empty() ? "object has no len()" : error);
  }
  *result = static_cast<uint64_t>(length.as.i64);
  return X3_STATUS_OK;
}

X3Status x3_get_item(X3Runtime* runtime, X3Value object, X3Value key, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr) {
    return fail(rt, "runtime/result is null");
  }
  std::string error;
  auto internal = xlang3::from_c_value(object, error);
  auto internal_key = xlang3::from_c_value(key, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  xlang3::Value out;
  if (!xlang3::sequence_get_item(internal, internal_key, out, error)) {
    return fail(rt, error);
  }
  *result = xlang3::to_c_value(out);
  return X3_STATUS_OK;
}

X3Status x3_list_append(X3Runtime* runtime, X3Value list, X3Value item) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr) {
    return fail(rt, "runtime is null");
  }
  std::string error;
  auto internal = xlang3::from_c_value(list, error);
  auto internal_item = xlang3::from_c_value(item, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  if (!xlang3::sequence_list_append(internal, internal_item, error)) {
    return fail(rt, error);
  }
  return X3_STATUS_OK;
}

X3Status x3_dict_set_item(X3Runtime* runtime, X3Value dict, X3Value key, X3Value item) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr) {
    return fail(rt, "runtime is null");
  }
  std::string error;
  auto internal = xlang3::from_c_value(dict, error);
  auto internal_key = xlang3::from_c_value(key, error);
  auto internal_item = xlang3::from_c_value(item, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  if (!xlang3::mapping_set_item(internal, internal_key, internal_item, error)) {
    return fail(rt, error);
  }
  return X3_STATUS_OK;
}

X3Status x3_dict_get_entry(X3Runtime* runtime, X3Value dict, uint64_t index, X3Value* key, X3Value* value) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || key == nullptr || value == nullptr) {
    return fail(rt, "runtime/key/value is null");
  }
  std::string error;
  auto internal = xlang3::from_c_value(dict, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  auto* dict_object = xlang3::value_as_dict(internal);
  if (dict_object == nullptr) {
    return fail(rt, "object is not a dict");
  }
  if (index >= dict_object->entries.size()) {
    return fail(rt, "dict entry index out of range");
  }
  const auto& entry = dict_object->entries[static_cast<size_t>(index)];
  *key = xlang3::to_c_value(entry.first);
  *value = xlang3::to_c_value(entry.second);
  return X3_STATUS_OK;
}

} // extern "C"
