/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "ipc_runtime.h"
#include "xlang3/expression.h"

#include "shared_memory_transport.h"
#include "shared_memory_transport_internal.h"
#include "serialize/ipc_value_marshal.h"
#include "serialize/xlang_stream.h"
#include "serialize/value_graph.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/value.h"
#include "runtime_lock.h"

#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
  bool args_by_value = false;
};

std::mutex g_registry_mutex;
std::unordered_map<std::string, RegisteredObject> g_registered_objects;
std::unordered_map<uint64_t, RegisteredObject> g_exported_objects;
std::unordered_map<Object*, uint64_t> g_exported_object_ids;
uint64_t g_next_object_id = 1;
std::atomic<uint64_t> g_session_id{0};
std::recursive_mutex g_listener_mutex;
struct ListenerContext {
  Runtime* runtime;
  std::mutex mutex;
  std::condition_variable drained;
  bool closing = false;
  size_t active = 0;
  explicit ListenerContext(Runtime* value) : runtime(value) {}
};
std::shared_ptr<ListenerContext> g_listener_context;

uint64_t current_node_id() {
#if defined(_WIN32)
  return static_cast<uint64_t>(_getpid());
#else
  return static_cast<uint64_t>(getpid());
#endif
}

class IpcObjectMarshalContext final : public serialize::IpcMarshalContext {
public:
  explicit IpcObjectMarshalContext(Runtime& runtime) : runtime_(runtime) {}
  bool make_object_ref(const Value& value, serialize::RemoteObjectId& out, std::string& error) override {
    if (auto* proxy = static_cast<RemoteProxyState*>(instance_get_native_data(value, kRemoteProxyNativeType));
        proxy != nullptr && proxy->has_object_id) {
      out = proxy->object_id;
      return true;
    }
    if (value.tag != ValueTag::Object || value.as.obj == nullptr) {
      error = "cannot export a non-object as an IPC object reference";
      return false;
    }
    if (g_session_id.load() == 0) {
      std::lock_guard<std::recursive_mutex> listener_lock(g_listener_mutex);
      if (g_session_id.load() == 0) {
        // Callback endpoints are shared-memory names, not network ports.
        Value ignored;
        const int64_t callback_port = (int64_t{1} << 32) + current_node_id();
        if (!ipc_lrpc_listen(runtime_, callback_port, false, ignored, error)) return false;
      }
    }
    {
      std::lock_guard<std::recursive_mutex> lock(g_listener_mutex);
      if (!g_listener_context || g_listener_context->runtime != &runtime_) {
        error = "lrpc exports must belong to the process listener runtime";
        return false;
      }
      std::lock_guard<std::mutex> state_lock(g_listener_context->mutex);
      if (g_listener_context->closing) { error = "lrpc runtime is closing"; return false; }
    }
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    auto it = g_exported_object_ids.find(value.as.obj);
    if (it == g_exported_object_ids.end()) {
      const uint64_t id = g_next_object_id++;
      g_exported_object_ids.emplace(value.as.obj, id);
      g_exported_objects.emplace(id, RegisteredObject{&runtime_, value});
      it = g_exported_object_ids.find(value.as.obj);
    }
    out.node_id = current_node_id();
    out.session_id = g_session_id.load();
    out.object_id = it->second;
    out.generation = 1;
    return true;
  }
private:
  Runtime& runtime_;
};

bool value_to_string(const Value& value, std::string& out) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    return true;
  }
  return false;
}

bool resolve_exported_object(const serialize::RemoteObjectId& id, Value& out) {
  if (id.node_id != current_node_id() || id.session_id != g_session_id.load() || id.generation != 1) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  auto it = g_exported_objects.find(id.object_id);
  if (it == g_exported_objects.end()) {
    return false;
  }
  value_assign_fast(out, it->second.object);
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
    case serialize::IpcWireValueKind::ExpressionDecoratorRef:
    case serialize::IpcWireValueKind::ValueCallRef:
      if (resolve_exported_object(wire.object_id, out)) {
        return true;
      }
      if (wire.object_id.node_id == current_node_id() || wire.object_id.session_id == 0) {
        error = "remote object reference is stale or has no callback endpoint";
        return false;
      }
      out = make_remote_proxy(module, endpoint, &wire.object_id);
      if (wire.kind == serialize::IpcWireValueKind::ValueCallRef)
        static_cast<RemoteProxyState*>(instance_get_native_data(out, kRemoteProxyNativeType))->args_by_value = true;
      if (wire.kind == serialize::IpcWireValueKind::ExpressionDecoratorRef ||
          (wire.kind == serialize::IpcWireValueKind::ValueCallRef && wire.bool_value)) {
        if (auto* proxy = value_as_instance(out))
          proxy->attrs.emplace_back("__xlang3_capture_expressions__", Value::boolean(true));
      }
      return true;
    case serialize::IpcWireValueKind::Expression:
      return decode_expression(wire.bytes, out, error);
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
        IpcObjectMarshalContext context(runtime);
        stream.SetMarshalContext(&context);
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

bool write_proxy_call_request(Runtime& runtime, RemoteProxyState* state, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, serialize::XLangStream& stream, std::string& error) {
  IpcObjectMarshalContext context(runtime);
  stream.SetMarshalContext(&context);
  const uint32_t first_arg = proxy_self_arg_offset(state, args, argc);
  const uint32_t remote_argc = argc - first_arg;
  if (state->args_by_value) {
    std::vector<Value> values;
    values.reserve(remote_argc);
    for (uint32_t i = first_arg; i < argc; ++i) values.push_back(args[i]);
    std::vector<Value> keywords;
    keywords.reserve(kwargc);
    for (uint32_t i = 0; i < kwargc; ++i) {
      if (!kwargs || !kwargs[i].name || !kwargs[i].name[0] || !kwargs[i].value) {
        error = "invalid IPC keyword argument";
        return false;
      }
      keywords.push_back(Value::tuple({Value::string(kwargs[i].name), *kwargs[i].value}));
    }
    stream << std::string_view("CALL_ID_VALUE") << state->object_id.node_id
           << state->object_id.session_id << state->object_id.object_id << state->object_id.generation;
    return serialize::write_value_graph(runtime, stream,
        Value::tuple({Value::tuple(std::move(values)), Value::tuple(std::move(keywords))}), error);
  }
  if (state->has_object_id) {
    stream << std::string_view(kwargc ? "CALL_ID_KW" : "CALL_ID")
           << state->object_id.node_id
           << state->object_id.session_id
           << state->object_id.object_id
           << state->object_id.generation
           << remote_argc;
  } else {
    stream << std::string_view(kwargc ? "CALL_KW" : "CALL")
           << std::string_view(state->module)
           << std::string_view("")
           << remote_argc;
  }
  for (uint32_t i = first_arg; i < argc; ++i) {
    if (!stream.MarshalToBytes(args[i], {}, error)) {
      return false;
    }
  }
  if (kwargc) {
    stream << kwargc;
    for (uint32_t i = 0; i < kwargc; ++i) {
      if (!kwargs || !kwargs[i].name || !kwargs[i].name[0] || !kwargs[i].value) {
        error = "invalid IPC keyword argument";
        return false;
      }
      stream << std::string_view(kwargs[i].name);
      if (!stream.MarshalToBytes(*kwargs[i].value, {}, error)) return false;
    }
  }
  return true;
}

bool remote_proxy_call_invoke_kw(Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out, std::string& error, void* user_data) {
  auto* state = static_cast<RemoteProxyState*>(user_data);
  if (state == nullptr) {
    error = "invalid remote proxy";
    return false;
  }
  (void)runtime;
  return ipc::lrpc_shared_memory_request(
      state->endpoint,
      [&](serialize::XLangStream& stream, std::string& write_error) {
        return write_proxy_call_request(runtime, state, args, argc, kwargs, kwargc, stream, write_error);
      },
      [&](serialize::XLangStream& stream, std::string& read_error) {
        return deserialize_value(state->module, state->endpoint, stream, out, read_error);
      },
      error);
}

bool remote_proxy_call_invoke(Runtime& runtime, const Value* args, uint32_t argc,
    Value& out, std::string& error, void* user_data) {
  return remote_proxy_call_invoke_kw(runtime, args, argc, nullptr, 0, out, error, user_data);
}

bool remote_proxy_get_attr(const Value& self, const std::string& name, Value& out, std::string& error) {
  auto* state = static_cast<RemoteProxyState*>(instance_get_native_data(self, kRemoteProxyNativeType));
  if (state == nullptr) {
    error = "invalid remote proxy";
    return false;
  }
  if (name == "__call__") {
    Value function = Value::native_function(0, "xlang3.ipc.RemoteProxy.__call__",
        remote_proxy_call_invoke, state, nullptr);
    value_as_native_function(function)->keyword_callback = remote_proxy_call_invoke_kw;
    out = Value::bound_method(self, function);
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

bool write_response_value(Runtime& runtime, const Value& value, serialize::XLangStream& response_stream, std::string& error) {
  IpcObjectMarshalContext context(runtime);
  response_stream.SetMarshalContext(&context);
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
  const bool has_keywords = op == "CALL_KW" || op == "CALL_ID_KW";
  if (op == "GETATTR" || op == "CALL" || op == "CALL_KW") {
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
  } else if (op == "GETATTR_ID" || op == "CALL_ID" || op == "CALL_ID_KW" || op == "CALL_ID_VALUE") {
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
    const auto* getter = runtime.find_builtin("getattr");
    Value attr_args[] = {target, Value::string(member)};
    if (getter == nullptr || !runtime_call_callable(runtime, *getter, attr_args, 2, callable, error)) {
      return false;
    }
  } else {
    value_assign_fast(callable, target);
  }
  if (op == "GETATTR" || op == "GETATTR_ID") {
    return write_response_value(runtime, callable, response, error);
  }
  if (op == "CALL_ID_VALUE") {
    if (!serialize::ipc_arguments_by_value(callable)) {
      error = "callable does not accept by-value IPC arguments";
      return false;
    }
    Value graph;
    if (!serialize::read_value_graph(runtime, stream, graph, error)) return false;
    auto* envelope = value_as_tuple(graph);
    if (!envelope || envelope->items.size() != 2) { error = "invalid IPC argument graph"; return false; }
    auto* positional = value_as_tuple(envelope->items[0]);
    auto* keyword_items = value_as_tuple(envelope->items[1]);
    if (!positional || !keyword_items || positional->items.size() > UINT32_MAX) {
      error = "invalid IPC argument graph"; return false;
    }
    std::vector<std::pair<std::string, Value>> keywords;
    std::unordered_set<std::string> names;
    for (const auto& entry : keyword_items->items) {
      auto* pair = value_as_tuple(entry);
      std::string name;
      if (!pair || pair->items.size() != 2 || !value_to_string(pair->items[0], name) ||
          name.empty() || !names.insert(name).second) {
        error = "invalid IPC keyword graph"; return false;
      }
      keywords.emplace_back(std::move(name), pair->items[1]);
    }
    Value result;
    std::vector<Value> arguments(positional->items.begin(), positional->items.end());
    if (!runtime_call_callable_kw(runtime, callable, arguments.data(),
        static_cast<uint32_t>(arguments.size()), keywords, result, error)) return false;
    return write_response_value(runtime, result, response, error);
  }
  uint32_t argc = 0;
  stream >> argc;
  if (!stream.CanRead(argc)) {
    error = "bad lrpc argument count";
    return false;
  }
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
  std::vector<std::pair<std::string, Value>> kwargs;
  if (has_keywords) {
    uint32_t kwargc = 0;
    if (!stream.CanRead(sizeof(kwargc))) {
      error = "missing lrpc keyword count";
      return false;
    }
    stream >> kwargc;
    if (!stream.CanRead(kwargc)) {
      error = "bad lrpc keyword count";
      return false;
    }
    std::unordered_set<std::string> names;
    for (uint32_t i = 0; i < kwargc; ++i) {
      std::string name;
      stream >> name;
      if (name.empty() || !names.insert(name).second) {
        error = "empty or duplicate lrpc keyword name";
        return false;
      }
      serialize::IpcWireValue wire;
      Value arg;
      if (!stream.MarshalFromBytes(wire, error) ||
          !materialize_wire_value(module, {}, wire, arg, error)) return false;
      kwargs.emplace_back(std::move(name), std::move(arg));
    }
  }
  Value result;
  if (!runtime_call_callable_kw(runtime, callable, args.empty() ? nullptr : args.data(), argc, kwargs, result, error)) {
    return false;
  }
  return write_response_value(runtime, result, response, error);
}

bool builtin_register_remote_object(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "register_remote_object expects name and object";
    return false;
  }
  std::string name;
  if (!value_to_string(args[0], name)) {
    error = "register_remote_object name must be a string";
    return false;
  }
  if (!ipc_register_remote_object(runtime, name, args[1], error)) return false;
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
    state->endpoint = "lrpc:" + std::to_string(object_id->session_id);
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
  std::lock_guard<std::recursive_mutex> listener_lock(g_listener_mutex);
  if (g_listener_context && g_listener_context->runtime != &runtime) {
    error = "lrpc registration must use the process listener runtime";
    return false;
  }
  std::lock_guard<std::mutex> lock(g_registry_mutex);
  g_registered_objects[name] = RegisteredObject{&runtime, object};
  return true;
}

bool ipc_lrpc_listen(Runtime& runtime, int64_t port, bool wait, Value& out, std::string& error) {
  std::unique_lock<std::recursive_mutex> listener_lock(g_listener_mutex);
  if (port <= 0) { error = "lrpc port must be positive"; return false; }
  const auto current_port = g_session_id.load();
  if (current_port != 0 && current_port != static_cast<uint64_t>(port)) {
    error = "this process already has an lrpc listener on " + std::to_string(current_port);
    return false;
  }
  if (current_port != 0) {
    if (!g_listener_context || g_listener_context->runtime != &runtime) {
      error = "this process already has an lrpc listener owned by another runtime";
      return false;
    }
    out = Value::none();
    listener_lock.unlock();
    if (wait) ipc::lrpc_wait_forever_platform();
    return true;
  }
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    for (auto& item : g_registered_objects) {
      if (item.second.runtime && item.second.runtime != &runtime) {
        error = "registered lrpc objects belong to another runtime";
        return false;
      }
      if (item.second.runtime == nullptr) {
        item.second.runtime = &runtime;
      }
    }
  }
  auto context = std::make_shared<ListenerContext>(&runtime);
  bool ok = ipc::lrpc_listen_shared_memory(
      port,
      false,
      [context](serialize::XLangStream& request, serialize::XLangStream& response, std::string& dispatch_error) {
        {
          std::lock_guard<std::mutex> lock(context->mutex);
          if (context->closing) { dispatch_error = "lrpc runtime is closing"; return false; }
          ++context->active;
        }
        struct Lease {
          ListenerContext& context;
          ~Lease() {
            std::lock_guard<std::mutex> lock(context.mutex);
            if (!--context.active) context.drained.notify_all();
          }
        } lease{*context};
        XlangRuntimeExecutionGuard execution;
        return server_dispatch(*context->runtime, request, response, dispatch_error);
      },
      error);
  if (ok) {
    g_listener_context = context;
    g_session_id.store(static_cast<uint64_t>(port));
  }
  listener_lock.unlock();
  if (ok && wait) ipc::lrpc_wait_forever_platform();
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

void ipc_detach_runtime(Runtime& runtime) {
  XlangRuntimeExecutionSuspension suspension;
  std::unique_lock<std::recursive_mutex> listener_lock(g_listener_mutex);
  auto context = g_listener_context;
  if (context && context->runtime == &runtime) {
    {
      std::unique_lock<std::mutex> lock(context->mutex);
      context->closing = true;
      // Existing callbacks can still need the listener mutex to finish exports.
      listener_lock.unlock();
      context->drained.wait(lock, [&] { return context->active == 0; });
    }
    listener_lock.lock();
    ipc::lrpc_stop_shared_memory_server_platform();
    ipc::g_dispatch = {};
    ipc::g_server_started.store(false);
    g_session_id.store(0);
    g_listener_context.reset();
  }
  std::vector<Value> released;
  {
    std::lock_guard<std::mutex> lock(g_registry_mutex);
    for (auto it = g_registered_objects.begin(); it != g_registered_objects.end();) {
      if (it->second.runtime == &runtime) {
        released.push_back(std::move(it->second.object));
        it = g_registered_objects.erase(it);
      } else ++it;
    }
    for (auto it = g_exported_objects.begin(); it != g_exported_objects.end();) {
      if (it->second.runtime == &runtime) {
        g_exported_object_ids.erase(it->second.object.as.obj);
        released.push_back(std::move(it->second.object));
        it = g_exported_objects.erase(it);
      } else ++it;
    }
  }
  listener_lock.unlock();
  // Native destructors can acquire the GIL or re-enter other runtimes.
  released.clear();
}

} // namespace xlang3
