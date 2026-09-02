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
#include "xlang3/builtins.h"

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <deque>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#endif

namespace xlang3 {

namespace {

constexpr const char* kOverlappedNativeType = "_overlapped.Overlapped";

struct OverlappedState {
  int64_t event = 0;
  int64_t address = 0;
  int64_t port = 0;
  int64_t completion_error = 0;
  int64_t completion_transferred = 0;
  int64_t completion_key = 0;
  bool pending = false;
  bool completed = false;
  bool completion_delivered = false;
  Value result;
  Value error;
};

struct IocpCompletion {
  int64_t error = 0;
  int64_t transferred = 0;
  int64_t key = 0;
  int64_t address = 0;
};

struct IocpState {
  std::deque<IocpCompletion> completions;
};

OverlappedState* overlapped_state(const Value& self, std::string& error);

std::mutex g_iocp_mutex;
std::unordered_map<int64_t, IocpState> g_iocp_ports;
std::unordered_map<int64_t, int64_t> g_iocp_handle_ports;
std::unordered_map<int64_t, OverlappedState*> g_overlapped_by_address;

int64_t next_overlapped_address() {
  static int64_t next = 0x10000;
  return ++next;
}

bool value_to_i64(const Value& value, int64_t& out) {
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
    return true;
  }
  return false;
}

int64_t overlapped_address_from_value(const Value& value) {
  int64_t address = 0;
  if (value_to_i64(value, address)) {
    return address;
  }
  std::string ignored;
  if (auto* state = overlapped_state(value, ignored)) {
    return state->address;
  }
  return 0;
}

void post_iocp_completion(int64_t port, int64_t transferred, int64_t key, int64_t address, int64_t error_code = 0) {
  if (port == 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_iocp_mutex);
  if (auto state_it = g_overlapped_by_address.find(address); state_it != g_overlapped_by_address.end()) {
    auto* state = state_it->second;
    state->port = port;
    state->completed = true;
    state->completion_delivered = false;
    state->completion_error = error_code;
    state->completion_transferred = transferred;
    state->completion_key = key;
  }
  auto port_it = g_iocp_ports.find(port);
  if (port_it == g_iocp_ports.end()) {
    return;
  }
  port_it->second.completions.push_back(IocpCompletion{error_code, transferred, key, address});
}

int64_t unique_iocp_port_unlocked() {
  if (g_iocp_ports.size() != 1) {
    return 0;
  }
  return g_iocp_ports.begin()->first;
}

OverlappedState* overlapped_state(const Value& self, std::string& error) {
  auto* state = static_cast<OverlappedState*>(instance_get_native_data(self, kOverlappedNativeType));
  if (state == nullptr) {
    error = "invalid _overlapped.Overlapped object";
  }
  return state;
}

void overlapped_cleanup(void* data) {
  auto* state = static_cast<OverlappedState*>(data);
  if (state != nullptr) {
    std::lock_guard<std::mutex> lock(g_iocp_mutex);
    g_overlapped_by_address.erase(state->address);
  }
  delete state;
}

bool overlapped_get_attr(const Value& self, const std::string& name, Value& out, std::string& error);

bool overlapped_not_implemented(Runtime& runtime, const char* name, std::string& error) {
  error = std::string(name) + " is not implemented for this XLang3 native dependency yet";
  runtime.raise_class_error("NotImplementedError", error);
  return false;
}

bool overlapped_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "Overlapped() expected optional event handle";
    return false;
  }
  auto* state = new OverlappedState();
  state->event = argc == 2 && args[1].tag == ValueTag::Int64 ? args[1].as.i64 : 0;
  state->address = next_overlapped_address();
  state->result = Value::int64(0);
  state->error = Value::none();
  {
    std::lock_guard<std::mutex> lock(g_iocp_mutex);
    g_overlapped_by_address[state->address] = state;
  }
  if (!instance_set_native_data(args[0], kOverlappedNativeType, state, overlapped_cleanup, error)) {
    {
      std::lock_guard<std::mutex> lock(g_iocp_mutex);
      g_overlapped_by_address.erase(state->address);
    }
    delete state;
    return false;
  }
  if (!instance_set_native_attr_hooks(args[0], overlapped_get_attr, nullptr, nullptr, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool overlapped_getresult(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "Overlapped.getresult() expected optional wait flag";
    return false;
  }
  auto* state = overlapped_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->pending = false;
  value_assign_fast(out, state->result);
  return true;
}

bool overlapped_cancel(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Overlapped.cancel() expected no arguments";
    return false;
  }
  auto* state = overlapped_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->pending = false;
  post_iocp_completion(state->port, 0, 0, state->address, 995);
  out = Value::boolean(false);
  return true;
}

bool overlapped_io_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1) {
    error = "Overlapped I/O method expected self";
    return false;
  }
  auto* state = overlapped_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  int64_t handle = 0;
  if (argc >= 2) {
    (void)value_to_i64(args[1], handle);
  }
  int64_t port = 0;
  {
    std::lock_guard<std::mutex> lock(g_iocp_mutex);
    auto it = g_iocp_handle_ports.find(handle);
    if (it != g_iocp_handle_ports.end()) {
      port = it->second;
    } else {
      port = unique_iocp_port_unlocked();
    }
  }
  state->port = port;
  state->pending = true;
  state->error = Value::none();

  const char* method = static_cast<const char*>(user_data);
  if (method != nullptr &&
      (std::string_view(method) == "WSARecv" || std::string_view(method) == "WSARecvFrom" ||
       std::string_view(method) == "ReadFile")) {
    state->result = Value::bytes("");
  } else if (
      method != nullptr &&
      (std::string_view(method) == "WSARecvInto" || std::string_view(method) == "WSARecvFromInto" ||
       std::string_view(method) == "ReadFileInto")) {
    state->result = Value::int64(0);
  } else if (
      method != nullptr &&
      (std::string_view(method) == "WSASend" || std::string_view(method) == "WSASendTo" ||
       std::string_view(method) == "WriteFile")) {
    int64_t transferred = 0;
    if (argc >= 3) {
      if (auto* bytes = value_as_bytes(args[2])) {
        transferred = static_cast<int64_t>(bytes->size);
      } else if (auto* string = value_as_string(args[2])) {
        transferred = static_cast<int64_t>(string->size);
      }
    }
    state->result = Value::int64(transferred);
  } else if (argc >= 3) {
    value_assign_fast(state->result, args[2]);
  } else {
    state->result = Value::int64(0);
  }
  state->completed = true;
  state->completion_delivered = false;
  state->completion_error = 0;
  state->completion_transferred = 0;
  state->completion_key = 0;
  post_iocp_completion(port, state->completion_transferred, state->completion_key, state->address);
  out = Value::boolean(false);
  return true;
}

bool overlapped_get_attr(const Value& self, const std::string& name, Value& out, std::string& error) {
  auto* state = overlapped_state(self, error);
  if (state == nullptr) {
    return false;
  }
  if (name == "address") {
    out = Value::int64(state->address);
    return true;
  }
  if (name == "event") {
    out = Value::int64(state->event);
    return true;
  }
  if (name == "pending") {
    out = Value::boolean(state->pending);
    return true;
  }
  if (name == "error") {
    value_assign_fast(out, state->error);
    return true;
  }
  error = "Overlapped object has no attribute '" + name + "'";
  return false;
}

Value make_overlapped_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_overlapped")});
  attrs.push_back({"__init__", runtime.make_native_function("_overlapped.Overlapped.__init__", overlapped_init)});
  attrs.push_back({"getresult", runtime.make_native_function("_overlapped.Overlapped.getresult", overlapped_getresult)});
  attrs.push_back({"cancel", runtime.make_native_function("_overlapped.Overlapped.cancel", overlapped_cancel)});
  for (const char* name : {
           "AcceptEx",
           "ConnectEx",
           "ConnectNamedPipe",
           "DisconnectEx",
           "ReadFile",
           "ReadFileInto",
           "TransmitFile",
           "WSARecv",
           "WSARecvFrom",
           "WSARecvFromInto",
           "WSARecvInto",
           "WSASend",
           "WSASendTo",
           "WriteFile",
       }) {
    attrs.push_back({name, runtime.make_native_function(std::string("_overlapped.Overlapped.") + name,
                                                        overlapped_io_method,
                                                        const_cast<char*>(name))});
  }
  Value klass = Value::class_object("Overlapped", std::move(attrs));
  return klass;
}

bool create_event(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::int64(next_overlapped_address());
  return true;
}

bool set_or_reset_event(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::int64(1);
  return true;
}

bool create_iocp(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "CreateIoCompletionPort() expected handle, port, key, and concurrency";
    return false;
  }
  int64_t handle = 0;
  int64_t existing_port = 0;
  (void)value_to_i64(args[0], handle);
  (void)value_to_i64(args[1], existing_port);
  std::lock_guard<std::mutex> lock(g_iocp_mutex);
  int64_t port = existing_port;
  if (port == 0) {
    port = next_overlapped_address();
    g_iocp_ports.emplace(port, IocpState{});
  } else if (g_iocp_ports.find(port) == g_iocp_ports.end()) {
    g_iocp_ports.emplace(port, IocpState{});
  }
  if (handle != 0 && handle != -1) {
    g_iocp_handle_ports[handle] = port;
  }
  out = Value::int64(port);
  return true;
}

bool get_queued_completion_status(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "GetQueuedCompletionStatus() expected port and timeout";
    return false;
  }
  int64_t port = 0;
  if (!value_to_i64(args[0], port)) {
    error = "GetQueuedCompletionStatus() port must be an integer handle";
    return false;
  }
  std::lock_guard<std::mutex> lock(g_iocp_mutex);
  auto it = g_iocp_ports.find(port);
  if (it == g_iocp_ports.end() || it->second.completions.empty()) {
    if (it != g_iocp_ports.end()) {
      for (auto& entry : g_overlapped_by_address) {
        auto* state = entry.second;
        if (state == nullptr || state->port != port || !state->completed || state->completion_delivered) {
          continue;
        }
        state->completion_delivered = true;
        state->pending = false;
        out = Value::tuple({
            Value::int64(state->completion_error),
            Value::int64(state->completion_transferred),
            Value::int64(state->completion_key),
            Value::int64(state->address),
        });
        return true;
      }
    }
    value_set_none(out);
    return true;
  }
  const IocpCompletion completion = it->second.completions.front();
  it->second.completions.pop_front();
  if (auto state_it = g_overlapped_by_address.find(completion.address); state_it != g_overlapped_by_address.end()) {
    state_it->second->completion_delivered = true;
    state_it->second->pending = false;
  }
  out = Value::tuple({
      Value::int64(completion.error),
      Value::int64(completion.transferred),
      Value::int64(completion.key),
      Value::int64(completion.address),
  });
  return true;
}

bool post_queued_completion_status(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "PostQueuedCompletionStatus() expected port, bytes, key, and overlapped";
    return false;
  }
  int64_t port = 0;
  int64_t transferred = 0;
  int64_t key = 0;
  if (!value_to_i64(args[0], port) || !value_to_i64(args[1], transferred) || !value_to_i64(args[2], key)) {
    error = "PostQueuedCompletionStatus() expected integer port, bytes, and key";
    return false;
  }
  post_iocp_completion(port, transferred, key, overlapped_address_from_value(args[3]));
  out = Value::boolean(true);
  return true;
}

bool close_iocp(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_overlapped._CloseIoCompletionPort() expected port";
    return false;
  }
  int64_t port = 0;
  if (value_to_i64(args[0], port)) {
    std::lock_guard<std::mutex> lock(g_iocp_mutex);
    g_iocp_ports.erase(port);
    for (auto it = g_iocp_handle_ports.begin(); it != g_iocp_handle_ports.end();) {
      if (it->second == port) {
        it = g_iocp_handle_ports.erase(it);
      } else {
        ++it;
      }
    }
  }
  value_set_none(out);
  return true;
}

bool overlapped_module_not_implemented(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void* user_data) {
  return overlapped_not_implemented(runtime, static_cast<const char*>(user_data), error);
}

void add_function(NativeModuleBuilder& builder, const char* name, NativeFunctionCallback callback) {
  builder.function(name, callback);
}

void add_unimplemented_function(Runtime& runtime, NativeModuleBuilder& builder, const char* name) {
  builder.value(name, runtime.make_native_function(std::string("_overlapped.") + name,
                                                  overlapped_module_not_implemented,
                                                  const_cast<char*>(name)));
}

} // namespace

void register_overlapped_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_overlapped");
  builder.value("NULL", Value::int64(0))
      .value("INVALID_HANDLE_VALUE", Value::int64(-1))
      .value("INFINITE", Value::int64(0xFFFFFFFF))
      .value("ERROR_IO_PENDING", Value::int64(997))
      .value("ERROR_NETNAME_DELETED", Value::int64(64))
      .value("ERROR_OPERATION_ABORTED", Value::int64(995))
      .value("ERROR_PIPE_BUSY", Value::int64(231))
      .value("ERROR_PORT_UNREACHABLE", Value::int64(1234))
      .value("ERROR_SEM_TIMEOUT", Value::int64(121))
      .value("SO_UPDATE_ACCEPT_CONTEXT", Value::int64(0x700B))
      .value("SO_UPDATE_CONNECT_CONTEXT", Value::int64(0x7010))
      .value("TF_REUSE_SOCKET", Value::int64(2))
      .value("Overlapped", make_overlapped_class(runtime));

  add_function(builder, "CreateEvent", create_event);
  add_function(builder, "ResetEvent", set_or_reset_event);
  add_function(builder, "SetEvent", set_or_reset_event);
  add_function(builder, "CreateIoCompletionPort", create_iocp);
  add_function(builder, "GetQueuedCompletionStatus", get_queued_completion_status);
  add_function(builder, "PostQueuedCompletionStatus", post_queued_completion_status);
  for (const char* name : {
           "BindLocal",
           "ConnectPipe",
           "FormatMessage",
           "RegisterWaitWithQueue",
           "UnregisterWait",
           "UnregisterWaitEx",
           "WSAConnect",
       }) {
    add_unimplemented_function(runtime, builder, name);
  }

  runtime.register_module("_overlapped", builder.finish());
}

} // namespace xlang3
