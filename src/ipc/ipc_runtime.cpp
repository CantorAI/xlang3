/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "ipc_runtime.h"

#include "shared_memory_transport.h"
#include "serialize/ipc_value_marshal.h"
#include "serialize/xlang_stream.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/value.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace xlang3 {

namespace {

constexpr const char* kRemoteProxyNativeType = "xlang3.ipc.RemoteProxy";

struct RegisteredObject {
  Runtime* runtime = nullptr;
  Value object;
};

struct RemoteProxyState {
  std::string module;
  std::string endpoint;
  serialize::RemoteObjectId object_id;
  bool has_object_id = false;
};

std::mutex g_registry_mutex;
std::unordered_map<std::string, RegisteredObject> g_registered_objects;
std::unordered_map<uint64_t, Value> g_exported_objects;
std::unordered_map<Object*, uint64_t> g_exported_object_ids;
uint64_t g_next_object_id = 1;
uint64_t g_session_id = 0;

uint64_t current_node_id() {
#if defined(_WIN32)
  return static_cast<uint64_t>(_getpid());
#else
  return static_cast<uint64_t>(getpid());
#endif
}

class IpcObjectMarshalContext final : public serialize::IpcMarshalContext {
public:
  bool make_object_ref(const Value& value, serialize::RemoteObjectId& out, std::string& error) override {
    if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
      error = "cannot export a non-object as an IPC object reference";
      return false;
    }
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = g_exported_object_ids.find(value.as.obj);
    if (it == g_exported_object_ids.end()) {
      const uint64_t id = g_next_object_id++;
      g_exported_object_ids.emplace(value.as.obj, id);
      g_exported_objects.emplace(id, value);
      it = g_exported_object_ids.find(value.as.obj);
    }
    out.node_id = current_node_id();
    out.session_id = g_session_id;
    out.object_id = it->second;
    out.generation = 1;
    return true;
  }
};

IpcObjectMarshalContext g_marshal_context;

bool value_to_string(const Value& value, std::string& out) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    return true;
  }
  return false;
}

bool resolve_exported_object(const serialize::RemoteObjectId& id, Value& out) {
  if (id.node_id != current_node_id() || id.generation == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  auto it = g_exported_objects.find(id.object_id);
  if (it == g_exported_objects.end()) {
    return false;
  }
  value_assign_fast(out, it->second);
  return true;
}

Value make_remote_proxy(
    const std::string& module,
    const std::string& endpoint,
    const serialize::RemoteObjectId* object_id);

bool materialize_wire_value(
    const std::string& module,
    const std::string& endpoint,
    const serialize::IpcWireValue& wire,
    Value& out,
    std::string& error) {
  switch (wire.kind) {
    case serialize::IpcWireValueKind::None:
      out = Value::none();
      return true;
    case serialize::IpcWireValueKind::Bool:
      out = Value::boolean(wire.bool_value);
      return true;
    case serialize::IpcWireValueKind::Int64:
      out = Value::int64(wire.int_value);
      return true;
    case serialize::IpcWireValueKind::Double:
      out = Value::number(wire.double_value);
      return true;
    case serialize::IpcWireValueKind::String:
      out = Value::string(wire.bytes);
      return true;
    case serialize::IpcWireValueKind::Bytes:
      out = Value::bytes(wire.bytes);
      return true;
    case serialize::IpcWireValueKind::Tuple: {
      std::vector<Value> items;
      items.reserve(wire.items.size());
      for (const auto& wire_item : wire.items) {
        Value item;
        if (!materialize_wire_value(module, endpoint, wire_item, item, error)) return false;
        items.push_back(std::move(item));
      }
      out = Value::tuple(std::move(items));
      return true;
    }
    case serialize::IpcWireValueKind::List: {
      std::vector<Value> items;
      items.reserve(wire.items.size());
      for (const auto& wire_item : wire.items) {
        Value item;
        if (!materialize_wire_value(module, endpoint, wire_item, item, error)) return false;
        items.push_back(std::move(item));
      }
      out = Value::list(std::move(items));
      return true;
    }
    case serialize::IpcWireValueKind::Dict: {
      std::vector<std::pair<Value, Value>> entries;
      entries.reserve(wire.entries.size());
      for (const auto& wire_entry : wire.entries) {
        Value key;
        Value value;
        if (!materialize_wire_value(module, endpoint, wire_entry.first, key, error) ||
            !materialize_wire_value(module, endpoint, wire_entry.second, value, error)) {
          return false;
        }
        entries.push_back({std::move(key), std::move(value)});
      }
      out = Value::dict(std::move(entries));
      return true;
    }
    case serialize::IpcWireValueKind::ObjectRef:
      if (resolve_exported_object(wire.object_id, out)) {
        return true;
      }
      out = make_remote_proxy(module, endpoint, &wire.object_id);
      return true;
    case serialize::IpcWireValueKind::Callable:
      out = make_remote_proxy(module, endpoint, nullptr);
      return true;
    case serialize::IpcWireValueKind::Error:
      error = wire.bytes;
      return false;
  }
  error = "unknown IPC value kind";
  return false;
}

bool deserialize_value(const std::string& module, const std::string& endpoint, serialize::XLangStream& stream, Value& out, std::string& error) {
  stream.SetPos({0, 0});
  serialize::IpcWireValue wire;
  if (!stream.MarshalFromBytes(wire, error)) {
    return false;
  }
  return materialize_wire_value(module, endpoint, wire, out, error);
}

bool remote_call(
    Runtime& runtime,
    const std::string& module,
    const std::string& endpoint,
    const std::string& member,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error) {
  bool ok = ipc::lrpc_shared_memory_request(
      endpoint,
      [&](serialize::XLangStream& stream, std::string& write_error) {
        stream.SetMarshalContext(&g_marshal_context);
        stream << std::string_view("CALL") << std::string_view(module) << std::string_view(member) << argc;
        for (uint32_t i = 0; i < argc; ++i) {
          if (!stream.MarshalToBytes(args[i], {}, write_error)) {
            return false;
          }
        }
        return true;
      },
      [&](serialize::XLangStream& stream, std::string& read_error) {
        return deserialize_value(module, endpoint, stream, out, read_error);
      },
      error);
  if (!ok) {
    return false;
  }
  (void)runtime;
  return true;
}

uint32_t proxy_self_arg_offset(const RemoteProxyState* state, const Value* args, uint32_t argc) {
  return (argc > 0 && value_as_instance(args[0]) != nullptr &&
          instance_get_native_data(args[0], kRemoteProxyNativeType) == state)
             ? 1
             : 0;
}

bool write_proxy_call_request(RemoteProxyState* state, const Value* args, uint32_t argc, serialize::XLangStream& stream, std::string& error) {
  stream.SetMarshalContext(&g_marshal_context);
  const uint32_t first_arg = proxy_self_arg_offset(state, args, argc);
  const uint32_t remote_argc = argc - first_arg;
  if (state->has_object_id) {
    stream << std::string_view("CALL_ID")
           << state->object_id.node_id
           << state->object_id.session_id
           << state->object_id.object_id
           << state->object_id.generation
           << remote_argc;
  } else {
    stream << std::string_view("CALL")
           << std::string_view(state->module)
           << std::string_view("")
           << remote_argc;
  }
  for (uint32_t i = first_arg; i < argc; ++i) {
    if (!stream.MarshalToBytes(args[i], {}, error)) {
      return false;
    }
  }
  return true;
}

bool remote_proxy_call_invoke(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<RemoteProxyState*>(user_data);
  if (state == nullptr) {
    error = "invalid remote proxy";
    return false;
  }
  (void)runtime;
  return ipc::lrpc_shared_memory_request(
      state->endpoint,
      [&](serialize::XLangStream& stream, std::string& write_error) {
        return write_proxy_call_request(state, args, argc, stream, write_error);
      },
      [&](serialize::XLangStream& stream, std::string& read_error) {
        return deserialize_value(state->module, state->endpoint, stream, out, read_error);
      },
      error);
}

bool remote_proxy_get_attr(const Value& self, const std::string& name, Value& out, std::string& error) {
  auto* state = static_cast<RemoteProxyState*>(instance_get_native_data(self, kRemoteProxyNativeType));
  if (state == nullptr) {
    error = "invalid remote proxy";
    return false;
  }
  if (name == "__call__") {
    out = Value::bound_method(
        self,
        Value::native_function(
            0,
            "xlang3.ipc.RemoteProxy.__call__",
            remote_proxy_call_invoke,
            state,
            nullptr));
    return true;
  }
  return ipc::lrpc_shared_memory_request(
      state->endpoint,
      [&](serialize::XLangStream& stream, std::string&) {
        if (state->has_object_id) {
          stream << std::string_view("GETATTR_ID")
                 << state->object_id.node_id
                 << state->object_id.session_id
                 << state->object_id.object_id
                 << state->object_id.generation
                 << std::string_view(name);
        } else {
          stream << std::string_view("GETATTR") << std::string_view(state->module) << std::string_view(name);
        }
        return true;
      },
      [&](serialize::XLangStream& stream, std::string& read_error) {
        return deserialize_value(state->module, state->endpoint, stream, out, read_error);
      },
      error);
}

void remote_proxy_cleanup(void* data) {
  delete static_cast<RemoteProxyState*>(data);
}

bool read_object_id(serialize::XLangStream& stream, serialize::RemoteObjectId& id) {
  stream >> id.node_id >> id.session_id >> id.object_id >> id.generation;
  return true;
}

bool write_response_value(const Value& value, serialize::XLangStream& response_stream, std::string& error) {
  response_stream.SetMarshalContext(&g_marshal_context);
  return response_stream.MarshalToBytes(value, {}, error);
}

bool server_dispatch(Runtime& runtime, serialize::XLangStream& stream, serialize::XLangStream& response, std::string& error) {
  stream.SetPos({0, 0});
  std::string op;
  stream >> op;
  if (op.empty()) {
    error = "bad lrpc request";
    return false;
  }
  std::string module;
  std::string member;
  Value target;
  if (op == "GETATTR" || op == "CALL") {
    stream >> module >> member;
    if (module.empty() || (op == "GETATTR" && member.empty())) {
      error = "bad lrpc request";
      return false;
    }
    RegisteredObject registered;
    {
      std::lock_guard<std::mutex> lock(g_registry_mutex);
      auto it = g_registered_objects.find(module);
      if (it == g_registered_objects.end()) {
        error = "remote object '" + module + "' is not registered";
        return false;
      }
      registered = it->second;
    }
    value_assign_fast(target, registered.object);
  } else if (op == "GETATTR_ID" || op == "CALL_ID") {
    serialize::RemoteObjectId id;
    read_object_id(stream, id);
    if (!resolve_exported_object(id, target)) {
      error = "remote object reference is stale or not owned by this process";
      return false;
    }
    if (op == "GETATTR_ID") {
      stream >> member;
      if (member.empty()) {
        error = "bad lrpc getattr request";
        return false;
      }
    }
  } else {
    error = "unknown lrpc operation";
    return false;
  }
  Value callable;
  if (!member.empty()) {
    if (!object_get_attr(target, member, callable, error)) {
      return false;
    }
  } else {
    value_assign_fast(callable, target);
  }
  if (op == "GETATTR" || op == "GETATTR_ID") {
    return write_response_value(callable, response, error);
  }
  uint32_t argc = 0;
  stream >> argc;
  std::vector<Value> args;
  args.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) {
    serialize::IpcWireValue wire;
    Value arg;
    if (!stream.MarshalFromBytes(wire, error) ||
        !materialize_wire_value(module, {}, wire, arg, error)) {
      return false;
    }
    args.push_back(std::move(arg));
  }
  Value result;
  if (!runtime_call_callable(runtime, callable, args.empty() ? nullptr : args.data(), argc, result, error)) {
    return false;
  }
  return write_response_value(result, response, error);
}

bool builtin_register_remote_object(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "register_remote_object expects name and object";
    return false;
  }
  std::string name;
  if (!value_to_string(args[0], name)) {
    error = "register_remote_object name must be a string";
    return false;
  }
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  g_registered_objects[name] = RegisteredObject{nullptr, args[1]};
  out = Value::none();
  return true;
}

bool builtin_lrpc_listen(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2 || args[0].tag != ValueTag::Int64) {
    error = "lrpc_listen expects port and optional wait flag";
    return false;
  }
  bool wait = true;
  if (argc == 2 && args[1].tag == ValueTag::Bool) {
    wait = args[1].as.b;
  }
  return ipc_lrpc_listen(runtime, args[0].as.i64, wait, out, error);
}

Value make_remote_proxy(
    const std::string& module,
    const std::string& endpoint,
    const serialize::RemoteObjectId* object_id) {
  std::string error;
  Value klass = Value::class_object("RemoteObject", {});
  Value proxy = Value::instance(klass);
  auto* state = new RemoteProxyState{module, endpoint};
  if (object_id != nullptr) {
    state->object_id = *object_id;
    state->has_object_id = true;
  }
  if (!instance_set_native_data(proxy, kRemoteProxyNativeType, state, remote_proxy_cleanup, error) ||
      !instance_set_native_attr_hooks(proxy, remote_proxy_get_attr, nullptr, nullptr, error)) {
    delete state;
    return Value::invalid();
  }
  return proxy;
}

} // namespace

bool ipc_register_remote_object(Runtime& runtime, const std::string& name, const Value& object, std::string& error) {
  (void)error;
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  g_registered_objects[name] = RegisteredObject{&runtime, object};
  return true;
}

bool ipc_lrpc_listen(Runtime& runtime, int64_t port, bool wait, Value& out, std::string& error) {
  g_session_id = static_cast<uint64_t>(port);
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    for (auto& item : g_registered_objects) {
      if (item.second.runtime == nullptr) {
        item.second.runtime = &runtime;
      }
    }
  }
  bool ok = ipc::lrpc_listen_shared_memory(
      port,
      wait,
      [&runtime](serialize::XLangStream& request, serialize::XLangStream& response, std::string& dispatch_error) {
        return server_dispatch(runtime, request, response, dispatch_error);
      },
      error);
  out = Value::none();
  return ok;
}

bool ipc_import_thru(Runtime& runtime, const std::string& name, const Value& endpoint, Value& out, std::string& error) {
  std::string endpoint_text;
  if (!value_to_string(endpoint, endpoint_text)) {
    error = "thru endpoint must be a string";
    return false;
  }
  if (endpoint_text.rfind("lrpc:", 0) != 0) {
    error = "unsupported thru endpoint '" + endpoint_text + "'";
    return false;
  }
  out = make_remote_proxy(name, endpoint_text, nullptr);
  if (out.tag == ValueTag::Invalid) {
    error = "failed to create remote proxy";
    return false;
  }
  (void)runtime;
  return true;
}

void register_ipc_builtins(Runtime& runtime) {
  runtime.register_native_builtin("register_remote_object", builtin_register_remote_object);
  runtime.register_native_builtin("lrpc_listen", builtin_lrpc_listen);
}

} // namespace xlang3
