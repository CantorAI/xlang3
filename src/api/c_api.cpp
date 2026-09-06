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
#include "xlang3/abi/xapi.h"
#include "xlang3/abi/xmodule.h"

#include "xlang3/attribute.h"
#include "xlang3/expression.h"
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
#include "serialize/block_stream.h"
#include "xlang3/runtime.h"
#include "xlang3/sema.h"
#include "runtime_lock.h"
#include "stream_internal.h"
#include "serialize/value_graph.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

X3Value x3_value_string_utf8(X3Runtime* runtime, const char* data, uint64_t size) {
  auto* rt = as_runtime(runtime);
  if (!rt || (!data && size)) return x3_value_invalid();
  if (size > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) -
      sizeof(xlang3::StringObject) - 1) {
    rt->set_last_error("string value is too large");
    return x3_value_invalid();
  }
  try {
    return xlang3::to_c_value(xlang3::Value::string_view(
        std::string_view(size ? data : "", static_cast<size_t>(size))));
  } catch (const std::exception& error) {
    rt->set_last_error(error.what());
    return x3_value_invalid();
  }
}

X3Status x3_value_string_data(X3Runtime* runtime, X3Value value,
    const char** data, uint64_t* size) {
  auto* rt = as_runtime(runtime);
  if (!rt || !data || !size) return fail(rt, "runtime/data/size is null");
  std::string error;
  auto internal = xlang3::from_c_value(value, error);
  if (!error.empty()) return fail(rt, error);
  auto* string = xlang3::value_as_string(internal);
  if (!string) return fail(rt, "expected a string value");
  auto view = xlang3::string_object_view(*string);
  *data = view.data();
  *size = view.size();
  return X3_STATUS_OK;
}

X3Value x3_value_bytes(X3Runtime* runtime, const void* data, uint64_t size) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || (size != 0 && data == nullptr)) {
    return x3_value_invalid();
  }
  if (size > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()) -
                 sizeof(xlang3::BytesObject) - 1) {
    rt->set_last_error("bytes value is too large");
    return x3_value_invalid();
  }
  try {
    return xlang3::to_c_value(xlang3::Value::bytes(
        std::string_view(size ? static_cast<const char*>(data) : "", static_cast<size_t>(size))));
  } catch (const std::exception& error) {
    rt->set_last_error(error.what());
    return x3_value_invalid();
  }
}

X3Value x3_value_memoryview(X3Runtime* runtime, void* data, uint64_t size,
    int32_t readonly, void* context, X3BufferCleanup cleanup) {
  struct Cleanup {
    void* context;
    X3BufferCleanup callback;
    ~Cleanup() { if (callback) callback(context); }
  } owner{context, cleanup};
  auto* rt = as_runtime(runtime);
  if (!rt || (!data && size) || size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
      size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    if (rt) rt->set_last_error("invalid native memoryview buffer");
    return x3_value_invalid();
  }
  try {
    auto storage = std::make_shared<xlang3::NativeBufferStorage>();
    storage->data = static_cast<char*>(data);
    storage->size = static_cast<size_t>(size);
    storage->context = context;
    storage->cleanup = cleanup;
    owner.callback = nullptr;
    auto value = xlang3::Value::memoryview(xlang3::Value::none(), 0, static_cast<size_t>(size), readonly != 0);
    xlang3::value_as_memoryview(value)->external = std::move(storage);
    return xlang3::to_c_value(value);
  } catch (const std::exception& error) {
    rt->set_last_error(error.what());
    return x3_value_invalid();
  }
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
    case xlang3::ObjectKind::Expression:
      return X3_OBJECT_KIND_EXPRESSION;
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
    const auto storage = xlang3::memoryview_object_view(*view);
    if (storage.data()) {
      *data = storage.data();
      *size = static_cast<uint64_t>(storage.size());
      return X3_STATUS_OK;
    }
  }
  return fail(rt, "value is not bytes-like");
}

X3Status x3_value_to_bytes(X3Runtime* runtime, X3Value value, X3Value* result) try {
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
  if (!xlang3::serialize::write_value_graph(*rt, stream, internal, error)) {
    return fail(rt, error);
  }
  std::string bytes;
  bytes.resize(static_cast<size_t>(stream.Size()));
  if (!bytes.empty() && !stream.FullCopyTo(bytes.data(), static_cast<xlang3::serialize::STREAM_SIZE>(bytes.size()))) {
    return fail(rt, "failed to copy serialized value");
  }
  *result = xlang3::to_c_value(xlang3::Value::bytes(std::move(bytes)));
  return X3_STATUS_OK;
} catch (const std::exception& e) {
  return fail(as_runtime(runtime), e.what());
}

X3Status x3_value_to_stream(X3Runtime* runtime, X3Value value, X3Stream* stream) {
  auto* rt = as_runtime(runtime);
  if (!rt || !stream || stream->runtime != runtime || stream->reading || stream->failed)
    return fail(rt, "invalid stream for serialization");
  try {
    std::string error;
    auto internal = xlang3::from_c_value(value, error);
    if (!error.empty() || !xlang3::serialize::write_value_graph(*rt, stream->storage, internal, error)) {
      stream->failed = true;
      return fail(rt, error);
    }
    stream->size = static_cast<uint64_t>(stream->storage.Size());
    stream->position = stream->size;
    return X3_STATUS_OK;
  } catch (const std::exception& e) {
    stream->failed = true;
    return fail(rt, e.what());
  }
}

X3Status x3_value_from_stream(X3Runtime* runtime, X3Stream* stream, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (!rt || !result || !stream || stream->runtime != runtime || !stream->reading || stream->failed)
    return fail(rt, "invalid stream for deserialization");
  try {
    xlang3::Value value;
    std::string error;
    if (!xlang3::serialize::read_value_graph(*rt, stream->storage, value, error)) {
      stream->failed = true;
      return fail(rt, error);
    }
    stream->position = static_cast<uint64_t>(stream->storage.CalcSize(stream->storage.GetPos()));
    *result = xlang3::to_c_value(value);
    return X3_STATUS_OK;
  } catch (const std::exception& e) {
    stream->failed = true;
    return fail(rt, e.what());
  }
}

X3Status x3_value_from_bytes(X3Runtime* runtime, X3Value bytes, X3Value* result) try {
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
  std::string error;
  xlang3::Value out;
  if (!xlang3::serialize::read_value_graph(*rt, stream, out, error)) {
    return fail(rt, error);
  }
  *result = xlang3::to_c_value(out);
  return X3_STATUS_OK;
} catch (const std::exception& e) {
  return fail(as_runtime(runtime), e.what());
}

X3Status x3_expression_compile(X3Runtime* runtime, const char* source, X3Value* result) try {
  auto* rt = as_runtime(runtime);
  if (result) *result = x3_value_invalid();
  if (!rt || !source || !result) return fail(rt, "null expression compilation argument");
  auto parsed = xlang3::parse_expression_source(source);
  if (!parsed.errors.empty()) return fail(rt, parsed.errors.front());
  if (!parsed.expression) return fail(rt, "expression is empty");
  auto expression = xlang3::capture_expression(*parsed.expression);
  const auto& root = reinterpret_cast<xlang3::ExpressionObject*>(expression.as.obj)->root;
  auto validate = [&](auto&& self, const xlang3::ExpressionNode& node) -> bool {
    if (node.op == "error") return false;
    for (const auto& child : node.children) if (!self(self, child)) return false;
    return true;
  };
  if (!validate(validate, root)) return fail(rt, "unsupported captured expression syntax");
  *result = xlang3::to_c_value(expression);
  return X3_STATUS_OK;
} catch (const std::exception& error) {
  return fail(as_runtime(runtime), error.what());
}

X3Status x3_expression_evaluate(X3Runtime* runtime, X3Value expression,
    X3Value bindings, X3Value* result, X3Value* reservations) {
  auto* rt = as_runtime(runtime);
  if (!rt || !result || !reservations) return fail(rt, "null expression evaluation argument");
  std::string error;
  auto expr = xlang3::from_c_value(expression, error);
  auto scope = xlang3::from_c_value(bindings, error);
  xlang3::Value out, pending;
  if (!error.empty() || !xlang3::evaluate_expression(expr, scope, out, pending, error)) return fail(rt, error);
  *result = xlang3::to_c_value(out);
  *reservations = xlang3::to_c_value(pending);
  return X3_STATUS_OK;
}

X3Status x3_expression_inspect(X3Runtime* runtime, X3Value expression, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (!rt || !result) return fail(rt, "null expression inspection argument");
  std::string error;
  auto expr = xlang3::from_c_value(expression, error);
  xlang3::Value out;
  if (!error.empty() || !xlang3::inspect_expression(expr, out, error)) return fail(rt, error);
  *result = xlang3::to_c_value(out);
  return X3_STATUS_OK;
}

X3Status x3_value_binary_op(
    X3Runtime* runtime,
    X3ValueBinaryOp op,
    X3Value left,
    X3Value right,
    X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr) {
    return fail(rt, "runtime/result is null");
  }
  std::string error;
  auto lhs = xlang3::from_c_value(left, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  auto rhs = xlang3::from_c_value(right, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  xlang3::Value out;
  switch (op) {
    case X3_VALUE_BINARY_ADD:
      if (!xlang3::value_add(lhs, rhs, out, error)) {
        return fail(rt, error);
      }
      break;
    default:
      return fail(rt, "unknown binary operator");
  }
  *result = xlang3::to_c_value(out);
  return X3_STATUS_OK;
}

X3Status x3_value_compare_op(
    X3Runtime* runtime,
    X3ValueCompareOp op,
    X3Value left,
    X3Value right,
    int32_t* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr) {
    return fail(rt, "runtime/result is null");
  }
  const char* op_text = nullptr;
  switch (op) {
    case X3_VALUE_COMPARE_EQ: op_text = "=="; break;
    case X3_VALUE_COMPARE_NE: op_text = "!="; break;
    case X3_VALUE_COMPARE_LT: op_text = "<"; break;
    case X3_VALUE_COMPARE_LE: op_text = "<="; break;
    case X3_VALUE_COMPARE_GT: op_text = ">"; break;
    case X3_VALUE_COMPARE_GE: op_text = ">="; break;
    default:
      return fail(rt, "unknown compare operator");
  }
  std::string error;
  auto lhs = xlang3::from_c_value(left, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  auto rhs = xlang3::from_c_value(right, error);
  if (!error.empty()) {
    return fail(rt, error);
  }
  xlang3::Value out;
  if (!xlang3::value_compare(op_text, lhs, rhs, out, error)) {
    return fail(rt, error);
  }
  if (out.tag != xlang3::ValueTag::Bool) {
    return fail(rt, "compare operator did not return bool");
  }
  *result = out.as.b ? 1 : 0;
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

X3Status x3_event_set_change_handler(X3Runtime* runtime, X3Value value,
    X3EventChanged callback, void* context, void (*cleanup)(void*)) {
  auto* rt = as_runtime(runtime);
  std::string error;
  auto event = xlang3::from_c_value(value, error);
  auto* object = xlang3::value_as_event(event);
  if (!rt || !object || !error.empty()) return fail(rt, "expected event object");
  try {
    // The deleter is armed only after registration succeeds.
    struct Context { void* data; void (*cleanup)(void*) = nullptr; ~Context() { if (cleanup) cleanup(data); } };
    auto owner = std::make_shared<Context>();
    owner->data = context;
    std::shared_ptr<std::function<bool(uint64_t)>> changed;
    if (callback) changed = std::make_shared<std::function<bool(uint64_t)>>(
        [owner, callback](uint64_t count) { return callback(owner->data, count) == X3_STATUS_OK; });
    std::lock_guard<std::recursive_mutex> lock(object->mutex);
    object->changed = std::move(changed);
    owner->cleanup = cleanup;
    return X3_STATUS_OK;
  } catch (const std::exception& exception) { return fail(rt, exception.what()); }
}

X3Status x3_runtime_import_module(X3Runtime* runtime, const char* package_name, const char* module_name, X3Value* result) {
  xlang3::XlangRuntimeExecutionGuard guard;
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
  xlang3::XlangRuntimeExecutionGuard guard;
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
  auto* instance = xlang3::value_as_instance(internal);
  auto* klass = instance ? xlang3::value_as_class(instance->klass) : nullptr;
  if (klass && (klass->has_descriptors || klass->has_getattribute_hook || klass->has_getattr_hook)) {
    const auto* getter = rt->find_builtin("getattr");
    const xlang3::Value args[] = {internal, xlang3::Value::string(name)};
    if (!getter || !xlang3::runtime_call_callable(*rt, *getter, args, 2, attr, error)) return fail(rt, error);
  } else if (!xlang3::attribute_get(internal, name, attr, error)) return fail(rt, error);
  *result = xlang3::to_c_value(attr);
  return X3_STATUS_OK;
}

X3Status x3_set_attr(X3Runtime* runtime, X3Value object, const char* name, X3Value value) {
  xlang3::XlangRuntimeExecutionGuard guard;
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
  xlang3::Value descriptor;
  if (auto* instance = xlang3::value_as_instance(internal)) {
    auto* klass = xlang3::value_as_class(instance->klass);
    if (klass && klass->has_setattr_hook) {
      const auto* setter = rt->find_builtin("setattr");
      const xlang3::Value args[] = {internal, xlang3::Value::string(name), internal_value};
      xlang3::Value ignored;
      if (!setter || !xlang3::runtime_call_callable(*rt, *setter, args, 3, ignored, error)) return fail(rt, error);
      return X3_STATUS_OK;
    }
  }
  if (xlang3::value_as_instance(internal) &&
      xlang3::object_get_class_attr_for_instance(internal, name, descriptor, error) &&
      xlang3::object_value_has_descriptor_set(descriptor)) {
    xlang3::Value setter, ignored;
    const xlang3::Value args[] = {internal, internal_value};
    if (!xlang3::attribute_get(descriptor, "__set__", setter, error) ||
        !xlang3::runtime_call_callable(*rt, setter, args, 2, ignored, error)) return fail(rt, error);
    return X3_STATUS_OK;
  }
  error.clear();
  if (!xlang3::attribute_set(internal, name, internal_value, error)) {
    return fail(rt, error);
  }
  return X3_STATUS_OK;
}

X3Status x3_call(X3Runtime* runtime, X3Value callable, const X3Value* args, uint32_t argc, X3Value* result) {
  return x3_call_kw(runtime, callable, args, argc, nullptr, 0, result);
}

X3Status x3_call_kw(X3Runtime* runtime, X3Value callable, const X3Value* args,
    uint32_t argc, const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* rt = as_runtime(runtime);
  if (rt == nullptr || result == nullptr || (argc != 0 && args == nullptr) ||
      (kwargc != 0 && kwargs == nullptr)) {
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

  std::vector<std::pair<std::string, xlang3::Value>> keywords;
  keywords.reserve(kwargc);
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr) return fail(rt, "keyword name is null");
    for (const auto& previous : keywords) {
      if (previous.first == kwargs[i].name) return fail(rt, "duplicate keyword argument");
    }
    auto value = xlang3::from_c_value(kwargs[i].value, error);
    if (!error.empty()) return fail(rt, error);
    keywords.emplace_back(kwargs[i].name, std::move(value));
  }
  xlang3::Value out;
  xlang3::XlangRuntimeExecutionGuard execution_guard;
  if (!xlang3::runtime_call_callable_kw(
          *rt,
          internal_callable,
          internal_args.empty() ? nullptr : internal_args.data(),
          static_cast<uint32_t>(internal_args.size()),
          keywords,
          out,
          error)) {
    return fail(rt, error);
  }
  *result = xlang3::to_c_value(out);
  return X3_STATUS_OK;
}

X3Status x3_len(X3Runtime* runtime, X3Value value, uint64_t* result) {
  xlang3::XlangRuntimeExecutionGuard guard;
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
  if (xlang3::value_as_instance(internal)) {
    xlang3::Value method;
    if (!xlang3::object_get_attr(internal, "__len__", method, error) ||
        !xlang3::runtime_call_callable(*rt, method, nullptr, 0, length, error)) return fail(rt, error);
    if (length.tag != xlang3::ValueTag::Int64 || length.as.i64 < 0) return fail(rt, "invalid length");
    *result = static_cast<uint64_t>(length.as.i64);
    return X3_STATUS_OK;
  }
  if (!xlang3::sequence_len(internal, length, error) || length.tag != xlang3::ValueTag::Int64) {
    return fail(rt, error.empty() ? "object has no len()" : error);
  }
  *result = static_cast<uint64_t>(length.as.i64);
  return X3_STATUS_OK;
}

X3Status x3_get_item(X3Runtime* runtime, X3Value object, X3Value key, X3Value* result) {
  xlang3::XlangRuntimeExecutionGuard guard;
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
    xlang3::Value method;
    std::string lookup;
    if (xlang3::value_as_instance(internal) &&
        xlang3::object_get_attr(internal, "__getitem__", method, lookup)) {
      error.clear();
      if (!xlang3::runtime_call_callable(*rt, method, &internal_key, 1, out, error)) return fail(rt, error);
      *result = xlang3::to_c_value(out);
      return X3_STATUS_OK;
    }
    if (error == "key not found") rt->raise_class_error("KeyError", error);
    else if (error == "index out of range") rt->raise_class_error("IndexError", error);
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

void* x3_instance_get_native_data(X3Value instance, const char* type_name) {
  if (!type_name) return nullptr;
  std::string error;
  auto value = xlang3::from_c_value(instance, error);
  return error.empty() ? xlang3::instance_get_native_data(value, type_name) : nullptr;
}

X3Status x3_instance_set_native_data(X3Value instance, const char* type_name,
    void* data, void (*cleanup)(void*)) {
  if (!type_name) return X3_STATUS_ERROR;
  std::string error;
  auto value = xlang3::from_c_value(instance, error);
  return error.empty() && xlang3::instance_set_native_data(value, type_name, data, cleanup, error)
      ? X3_STATUS_OK : X3_STATUS_ERROR;
}

X3Value x3_value_instance(X3Runtime* runtime, X3Value klass) {
  auto* rt = as_runtime(runtime);
  if (!rt) return x3_value_invalid();
  xlang3::XlangRuntimeExecutionGuard guard;
  std::string error;
  auto value = xlang3::from_c_value(klass, error);
  if (!error.empty() || !xlang3::value_as_class(value)) {
    fail(rt, error.empty() ? "object is not a class" : error);
    return x3_value_invalid();
  }
  return xlang3::to_c_value(xlang3::Value::instance(std::move(value)));
}

X3Status x3_instance_set_native_owner(X3Value instance, const char* type_name,
    void* data, void* owner, void (*cleanup)(void*)) {
  if (!type_name || !data || !owner || !cleanup) return X3_STATUS_ERROR;
  std::string error;
  auto value = xlang3::from_c_value(instance, error);
  return error.empty() && xlang3::instance_set_native_owner(value, type_name, data, owner, cleanup, error)
      ? X3_STATUS_OK : X3_STATUS_ERROR;
}

X3Status x3_instance_set_native_cast(X3Value instance, X3NativeDataCast cast) {
  std::string error;
  auto value = xlang3::from_c_value(instance, error);
  auto* object = error.empty() ? xlang3::value_as_instance(value) : nullptr;
  if (!object || !object->native_data) return X3_STATUS_ERROR;
  object->native_data_cast = cast;
  return X3_STATUS_OK;
}

} // extern "C"
