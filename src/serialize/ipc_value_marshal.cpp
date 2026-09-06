/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "serialize/ipc_value_marshal.h"

#include "xlang3/mapping.h"
#include "xlang3/expression.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cstring>

namespace xlang3::serialize {

namespace {

template <typename T>
bool fetch_pod(XLangStream& stream, T& value) {
  return stream.CopyTo(reinterpret_cast<char*>(&value), static_cast<STREAM_SIZE>(sizeof(value)));
}

constexpr uint32_t kMarshalContainerByRefThreshold = 1000;

bool should_always_pass_by_reference(ObjectKind kind) {
  switch (kind) {
    case ObjectKind::Module:
    case ObjectKind::Cell:
    case ObjectKind::Function:
    case ObjectKind::NativeFunction:
    case ObjectKind::Class:
    case ObjectKind::Instance:
    case ObjectKind::BoundMethod:
    case ObjectKind::StaticMethod:
    case ObjectKind::ClassMethod:
    case ObjectKind::Super:
    case ObjectKind::SlotDescriptor:
    case ObjectKind::Property:
    case ObjectKind::Event:
    case ObjectKind::Code:
    case ObjectKind::Frame:
    case ObjectKind::Traceback:
    case ObjectKind::File:
    case ObjectKind::GenericAlias:
    case ObjectKind::TypeParam:
    case ObjectKind::Generator:
    case ObjectKind::AsyncGeneratorAwaitable:
    case ObjectKind::CallableIterator:
    case ObjectKind::ProtocolIterator:
      return true;
    default:
      return false;
  }
}

bool write_object_ref(XLangStream& stream, IpcMarshalContext* context, const Value& value, std::string& error) {
  if (context == nullptr) {
    error = "standalone serialization does not support this object kind; function transfer is not implemented";
    return false;
  }
  RemoteObjectId id;
  if (!context->make_object_ref(value, id, error)) {
    return false;
  }
  const auto kind = ipc_arguments_by_value(value) ? IpcWireValueKind::ValueCallRef :
      expression_capture_enabled(value) ? IpcWireValueKind::ExpressionDecoratorRef : IpcWireValueKind::ObjectRef;
  stream << kind << id.node_id << id.session_id << id.object_id << id.generation;
  if (kind == IpcWireValueKind::ValueCallRef)
    stream << static_cast<uint8_t>(expression_capture_enabled(value));
  return true;
}

} // namespace

bool ipc_arguments_by_value(const Value& callable) {
  const Value* function = &callable;
  Value method;
  if (value_as_instance(callable)) {
    std::string error;
    if (!object_get_class_attr_for_instance(callable, "__call__", method, error)) return false;
    function = &method;
  }
  if (auto* bound = value_as_bound_method(*function)) function = &bound->function;
  if (auto* native = value_as_native_function(*function)) return native->ipc_args_by_value;
  return false;
}

bool XLangStream::MarshalToBytes(const Value& value, const std::string& callable_name, std::string& error) {
  if (io_failed_ || marshal_depth_ >= 256) {
    error = "serialization failed or nesting limit exceeded";
    return false;
  }
  struct DepthGuard { unsigned& depth; ~DepthGuard() { --depth; } } guard{marshal_depth_};
  ++marshal_depth_;
  const bool ok = MarshalToBytesImpl(value, callable_name, error);
  if (io_failed_ && error.empty()) error = "stream write failed";
  return ok && !io_failed_;
}

bool XLangStream::MarshalToBytesImpl(const Value& value, const std::string& callable_name, std::string& error) {
  if (!callable_name.empty()) {
    const auto kind = IpcWireValueKind::Callable;
    (*this) << kind << callable_name;
    return true;
  }
  IpcWireValueKind kind = IpcWireValueKind::Invalid;
  switch (value.tag) {
    case ValueTag::None:
      kind = IpcWireValueKind::None;
      (*this) << kind;
      return true;
    case ValueTag::Bool:
      kind = IpcWireValueKind::Bool;
      (*this) << kind << value.as.b;
      return true;
    case ValueTag::Int64:
      kind = IpcWireValueKind::Int64;
      (*this) << kind << value.as.i64;
      return true;
    case ValueTag::Double:
      kind = IpcWireValueKind::Double;
      (*this) << kind << value.as.f64;
      return true;
    case ValueTag::Object:
      if (value.as.obj == nullptr) {
        kind = IpcWireValueKind::None;
        (*this) << kind;
        return true;
      }
      if (value.as.obj->kind == ObjectKind::Expression) {
        std::string bytes;
        if (!encode_expression(value, bytes, error)) return false;
        (*this) << IpcWireValueKind::Expression << bytes;
        return true;
      }
      if (auto* string = value_as_string(value)) {
        kind = IpcWireValueKind::String;
        (*this) << kind << string_object_view(*string);
        return true;
      }
      if (auto* bytes = value_as_bytes(value)) {
        kind = IpcWireValueKind::Bytes;
        (*this) << kind << bytes_object_view(*bytes);
        return true;
      }
      if (auto* bytearray = value_as_bytearray(value)) {
        kind = IpcWireValueKind::Bytes;
        (*this) << kind << std::string_view(bytearray->value.data(), bytearray->value.size());
        return true;
      }
      if (auto* view = value_as_memoryview(value)) {
        if (view->released) {
          error = "cannot marshal released memoryview";
          return false;
        }
        const auto storage = memoryview_object_view(*view);
        if (storage.data()) {
          kind = IpcWireValueKind::Bytes;
          (*this) << kind << storage;
          return true;
        }
        error = "memoryview owner cannot be marshaled as bytes";
        return false;
      }
      if (should_always_pass_by_reference(value.as.obj->kind)) {
        return write_object_ref(*this, MarshalContext(), value, error);
      }
      if (auto* tuple = value_as_tuple(value)) {
        if (MarshalContext() && tuple->items.size() > kMarshalContainerByRefThreshold) {
          return write_object_ref(*this, MarshalContext(), value, error);
        }
        kind = IpcWireValueKind::Tuple;
        (*this) << kind << static_cast<uint32_t>(tuple->items.size());
        for (const auto& item : tuple->items) {
          if (!MarshalToBytes(item, {}, error)) return false;
        }
        return true;
      }
      if (auto* list = value_as_list(value)) {
        if (MarshalContext() && list->items.size() > kMarshalContainerByRefThreshold) {
          return write_object_ref(*this, MarshalContext(), value, error);
        }
        kind = IpcWireValueKind::List;
        (*this) << kind << static_cast<uint32_t>(list->items.size());
        for (const auto& item : list->items) {
          if (!MarshalToBytes(item, {}, error)) return false;
        }
        return true;
      }
      if (auto* dict = value_as_dict(value)) {
        if (MarshalContext() && dict->entries.size() > kMarshalContainerByRefThreshold) {
          return write_object_ref(*this, MarshalContext(), value, error);
        }
        kind = IpcWireValueKind::Dict;
        (*this) << kind << static_cast<uint32_t>(dict->entries.size());
        for (const auto& entry : dict->entries) {
          if (!MarshalToBytes(entry.first, {}, error) || !MarshalToBytes(entry.second, {}, error)) return false;
        }
        return true;
      }
      return write_object_ref(*this, MarshalContext(), value, error);
    default:
      break;
  }
  error = "XLangStream IPC marshal does not support this value yet";
  return false;
}

bool XLangStream::MarshalFromBytes(IpcWireValue& value, std::string& error) {
  if (io_failed_ || marshal_depth_ >= 256) {
    error = "deserialization failed or nesting limit exceeded";
    return false;
  }
  struct DepthGuard { unsigned& depth; ~DepthGuard() { --depth; } } guard{marshal_depth_};
  ++marshal_depth_;
  const bool ok = MarshalFromBytesImpl(value, error);
  if ((!ok || io_failed_) && error.empty()) error = "truncated or invalid serialized value";
  return ok && !io_failed_;
}

bool XLangStream::MarshalFromBytesImpl(IpcWireValue& value, std::string& error) {
  if (!fetch_pod(*this, value.kind)) {
    error = "truncated IPC value";
    return false;
  }
  switch (value.kind) {
    case IpcWireValueKind::Invalid:
    case IpcWireValueKind::None:
      return true;
    case IpcWireValueKind::Bool:
      return fetch_pod(*this, value.bool_value);
    case IpcWireValueKind::Int64:
      return fetch_pod(*this, value.int_value);
    case IpcWireValueKind::Double:
      return fetch_pod(*this, value.double_value);
    case IpcWireValueKind::String:
    case IpcWireValueKind::Bytes:
    case IpcWireValueKind::Callable:
    case IpcWireValueKind::Error:
      (*this) >> value.bytes;
      return true;
    case IpcWireValueKind::Expression: {
      uint32_t size = 0;
      if (!fetch_pod(*this, size) || size > 16 * 1024 * 1024 || !fetch_bytes(value.bytes, size)) {
        error = "invalid or truncated expression payload";
        return false;
      }
      return true;
    }
    case IpcWireValueKind::Tuple:
    case IpcWireValueKind::List: {
      uint32_t count = 0;
      (*this) >> count;
      if (io_failed_ || !CanRead(count)) return false;
      value.items.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        IpcWireValue item;
        if (!MarshalFromBytes(item, error)) return false;
        value.items.push_back(std::move(item));
      }
      return true;
    }
    case IpcWireValueKind::Dict: {
      uint32_t count = 0;
      (*this) >> count;
      if (io_failed_ || !CanRead(static_cast<STREAM_SIZE>(count) * 2)) return false;
      value.entries.reserve(count);
      for (uint32_t i = 0; i < count; ++i) {
        IpcWireValue key;
        IpcWireValue item;
        if (!MarshalFromBytes(key, error) || !MarshalFromBytes(item, error)) return false;
        value.entries.push_back({std::move(key), std::move(item)});
      }
      return true;
    }
    case IpcWireValueKind::ObjectRef:
    case IpcWireValueKind::ExpressionDecoratorRef:
    case IpcWireValueKind::ValueCallRef:
      (*this) >> value.object_id.node_id >> value.object_id.session_id >> value.object_id.object_id >> value.object_id.generation;
      if (value.kind == IpcWireValueKind::ValueCallRef) {
        uint8_t capture = 0;
        (*this) >> capture;
        if (capture > 1) { error = "invalid IPC expression capture flag"; return false; }
        value.bool_value = capture != 0;
      }
      return true;
  }
  error = "unknown IPC value kind";
  return false;
}

void XLangStream::MarshalError(const std::string& message) {
  const auto kind = IpcWireValueKind::Error;
  (*this) << kind << message;
}

} // namespace xlang3::serialize
