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
#include "xlang3/functional_iterators.h"
#include "xlang3/interpreter.h"
#include "xlang3/ir.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/native_call_context.h"
#include "xlang3/object_model.h"
#include "xlang3/parser.h"
#include "xlang3/sequence.h"
#include "xlang3/serialize/block_stream.h"
#include "xlang3/serialize/ipc_value_marshal.h"
#include "xlang3/runtime.h"
#include "xlang3/sema.h"
#include "runtime_lock.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
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

bool materialize_wire_value(
    const xlang3::serialize::IpcWireValue& wire,
    xlang3::Value& out,
    std::string& error) {
  switch (wire.kind) {
    case xlang3::serialize::IpcWireValueKind::Invalid:
      out = xlang3::Value::invalid();
      return true;
    case xlang3::serialize::IpcWireValueKind::None:
      out = xlang3::Value::none();
      return true;
    case xlang3::serialize::IpcWireValueKind::Bool:
      out = xlang3::Value::boolean(wire.bool_value);
      return true;
    case xlang3::serialize::IpcWireValueKind::Int64:
      out = xlang3::Value::int64(wire.int_value);
      return true;
    case xlang3::serialize::IpcWireValueKind::Double:
      out = xlang3::Value::number(wire.double_value);
      return true;
    case xlang3::serialize::IpcWireValueKind::String:
      out = xlang3::Value::string(wire.bytes);
      return true;
    case xlang3::serialize::IpcWireValueKind::Bytes:
      out = xlang3::Value::bytes(wire.bytes);
      return true;
    case xlang3::serialize::IpcWireValueKind::Tuple: {
      std::vector<xlang3::Value> items;
      items.reserve(wire.items.size());
      for (const auto& item_wire : wire.items) {
        xlang3::Value item;
        if (!materialize_wire_value(item_wire, item, error)) return false;
        items.push_back(std::move(item));
      }
      out = xlang3::Value::tuple(std::move(items));
      return true;
    }
    case xlang3::serialize::IpcWireValueKind::List: {
      std::vector<xlang3::Value> items;
      items.reserve(wire.items.size());
      for (const auto& item_wire : wire.items) {
        xlang3::Value item;
        if (!materialize_wire_value(item_wire, item, error)) return false;
        items.push_back(std::move(item));
      }
      out = xlang3::Value::list(std::move(items));
      return true;
    }
    case xlang3::serialize::IpcWireValueKind::Dict: {
      std::vector<std::pair<xlang3::Value, xlang3::Value>> entries;
      entries.reserve(wire.entries.size());
      for (const auto& entry_wire : wire.entries) {
        xlang3::Value key;
        xlang3::Value value;
        if (!materialize_wire_value(entry_wire.first, key, error) ||
            !materialize_wire_value(entry_wire.second, value, error)) {
          return false;
        }
        entries.push_back({std::move(key), std::move(value)});
      }
      out = xlang3::Value::dict(std::move(entries));
      return true;
    }
    case xlang3::serialize::IpcWireValueKind::ObjectRef:
    case xlang3::serialize::IpcWireValueKind::Callable:
      error = "serialized remote object references need an IPC endpoint";
      return false;
    case xlang3::serialize::IpcWireValueKind::Error:
      error = wire.bytes;
      return false;
  }
  error = "unknown serialized value kind";
  return false;
}

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
  auto module = std::make_shared<xlang3::ir::Module>(std::move(lowered.module));
  auto exec_result = interpreter.run(std::move(module));
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

X3Value x3_value_bytes(X3Runtime* runtime, const void* data, uint64_t size) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || (size != 0 && data == nullptr)) {
    return x3_value_invalid();
  }
  if (size > static_cast<uint64_t>(std::string().max_size())) {
    rt->set_last_error("bytes value is too large");
    return x3_value_invalid();
  }
  return xlang3::to_c_value(xlang3::Value::bytes(
      std::string(static_cast<const char*>(data), static_cast<size_t>(size))));
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
    case xlang3::ObjectKind::Bytes:
      return X3_OBJECT_KIND_BYTES;
    case xlang3::ObjectKind::ByteArray:
      return X3_OBJECT_KIND_BYTEARRAY;
    case xlang3::ObjectKind::MemoryView:
      return X3_OBJECT_KIND_MEMORYVIEW;
    case xlang3::ObjectKind::Event:
      return X3_OBJECT_KIND_EVENT;
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

X3Status x3_value_bytes_data(X3Runtime* runtime, X3Value value, const void** data, uint64_t* size) {
  auto* rt = as_runtime(runtime);
  if (data == nullptr || size == nullptr) {
    return fail(rt, "data/size is null");
  }
  std::string error;
  auto internal = xlang3::from_c_value(value, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  if (auto* bytes = xlang3::value_as_bytes(internal)) {
    const auto view = xlang3::bytes_object_view(*bytes);
    *data = view.data();
    *size = static_cast<uint64_t>(view.size());
    return X3_STATUS_OK;
  }
  if (auto* bytearray = xlang3::value_as_bytearray(internal)) {
    *data = bytearray->value.data();
    *size = static_cast<uint64_t>(bytearray->value.size());
    return X3_STATUS_OK;
  }
  if (auto* view = xlang3::value_as_memoryview(internal)) {
    if (view->released) {
      return fail(rt, "memoryview is released");
    }
    if (auto* owner_bytes = xlang3::value_as_bytes(view->owner)) {
      const auto owner = xlang3::bytes_object_view(*owner_bytes);
      if (view->offset <= owner.size() && owner.size() - view->offset >= view->size) {
        *data = owner.data() + view->offset;
        *size = static_cast<uint64_t>(view->size);
        return X3_STATUS_OK;
      }
    }
    if (auto* owner_array = xlang3::value_as_bytearray(view->owner);
        owner_array != nullptr && view->offset <= owner_array->value.size() &&
        owner_array->value.size() - view->offset >= view->size) {
      *data = owner_array->value.data() + view->offset;
      *size = static_cast<uint64_t>(view->size);
      return X3_STATUS_OK;
    }
  }
  return fail(rt, "value is not bytes-like");
}

X3Status x3_value_to_bytes(X3Runtime* runtime, X3Value value, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr) {
    return fail(rt, "runtime/result is null");
  }
  std::string error;
  auto internal = xlang3::from_c_value(value, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  xlang3::serialize::BlockStream stream;
  if (!stream.MarshalToBytes(internal, {}, error)) {
    return fail(rt, error);
  }
  std::string bytes;
  bytes.resize(static_cast<size_t>(stream.Size()));
  if (!bytes.empty() && !stream.FullCopyTo(bytes.data(), static_cast<xlang3::serialize::STREAM_SIZE>(bytes.size()))) {
    return fail(rt, "failed to copy serialized value");
  }
  *result = xlang3::to_c_value(xlang3::Value::bytes(std::move(bytes)));
  return X3_STATUS_OK;
}

X3Status x3_value_from_bytes(X3Runtime* runtime, X3Value bytes, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr) {
    return fail(rt, "runtime/result is null");
  }
  const void* data = nullptr;
  uint64_t size = 0;
  if (x3_value_bytes_data(runtime, bytes, &data, &size) != X3_STATUS_OK) {
    return X3_STATUS_ERROR;
  }
  xlang3::serialize::BlockStream stream(static_cast<char*>(const_cast<void*>(data)), static_cast<xlang3::serialize::STREAM_SIZE>(size), false);
  xlang3::serialize::IpcWireValue wire;
  std::string error;
  if (!stream.MarshalFromBytes(wire, error)) {
    return fail(rt, error);
  }
  xlang3::Value out;
  if (!materialize_wire_value(wire, out, error)) {
    return fail(rt, error);
  }
  *result = xlang3::to_c_value(out);
  return X3_STATUS_OK;
}

X3Status x3_event_create(X3Runtime* runtime, const char* name, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr) {
    return fail(rt, "runtime/result is null");
  }
  *result = xlang3::to_c_value(xlang3::Value::event(name == nullptr ? std::string() : std::string(name)));
  return X3_STATUS_OK;
}

X3Status x3_event_subscribe(X3Runtime* runtime, X3Value event, X3Value callable, uint64_t* cookie) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || cookie == nullptr) {
    return fail(rt, "runtime/cookie is null");
  }
  std::string error;
  auto internal_event = xlang3::from_c_value(event, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  auto internal_callable = xlang3::from_c_value(callable, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  if (!xlang3::event_subscribe(std::move(internal_event), std::move(internal_callable), *cookie, error)) {
    return fail(rt, error);
  }
  return X3_STATUS_OK;
}

X3Status x3_event_unsubscribe(X3Runtime* runtime, X3Value event, uint64_t cookie) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr) {
    return fail(rt, "runtime is null");
  }
  std::string error;
  auto internal_event = xlang3::from_c_value(event, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  if (!xlang3::event_unsubscribe(std::move(internal_event), cookie, error)) {
    return fail(rt, error);
  }
  return X3_STATUS_OK;
}

X3Status x3_event_fire(X3Runtime* runtime, X3Value event, const X3Value* args, uint32_t argc, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr || (argc != 0 && args == nullptr)) {
    return fail(rt, "runtime/args/result is null");
  }
  std::string error;
  auto internal_event = xlang3::from_c_value(event, error);
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
  if (!xlang3::event_fire(*rt, std::move(internal_event), internal_args.data(), argc, out, error)) {
    return fail(rt, error);
  }
  *result = xlang3::to_c_value(out);
  return X3_STATUS_OK;
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
  xlang3::XlangRuntimeExecutionGuard execution_guard;
  if (!xlang3::runtime_call_callable(
          *rt,
          internal_callable,
          internal_args.empty() ? nullptr : internal_args.data(),
          static_cast<uint32_t>(internal_args.size()),
          out,
          error)) {
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
