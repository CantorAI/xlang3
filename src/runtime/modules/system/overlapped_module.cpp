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

#include <cstdint>
#include <string>
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
  bool pending = false;
  Value result;
  Value error;
};

int64_t next_overlapped_address() {
  static int64_t next = 0x10000;
  return ++next;
}

OverlappedState* overlapped_state(const Value& self, std::string& error) {
  auto* state = static_cast<OverlappedState*>(instance_get_native_data(self, kOverlappedNativeType));
  if (state == nullptr) {
    error = "invalid _overlapped.Overlapped object";
  }
  return state;
}

void overlapped_cleanup(void* data) {
  delete static_cast<OverlappedState*>(data);
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
  if (!instance_set_native_data(args[0], kOverlappedNativeType, state, overlapped_cleanup, error)) {
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
  out = Value::boolean(false);
  return true;
}

bool overlapped_io_method(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void* user_data) {
  return overlapped_not_implemented(runtime, static_cast<const char*>(user_data), error);
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

bool create_iocp(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "CreateIoCompletionPort() expected handle, port, key, and concurrency";
    return false;
  }
  out = Value::int64(next_overlapped_address());
  return true;
}

bool get_queued_completion_status(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "GetQueuedCompletionStatus() expected port and timeout";
    return false;
  }
  value_set_none(out);
  return true;
}

bool post_queued_completion_status(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "PostQueuedCompletionStatus() expected port, bytes, key, and overlapped";
    return false;
  }
  out = Value::boolean(true);
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
