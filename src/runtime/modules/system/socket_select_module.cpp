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

namespace xlang3 {

namespace {

constexpr int64_t kAfUnspec = 0;
constexpr int64_t kAfInet = 2;
constexpr int64_t kSockStream = 1;
constexpr int64_t kSockDgram = 2;

struct SocketState {
  int64_t family = kAfInet;
  int64_t type = kSockStream;
  int64_t proto = 0;
  bool closed = false;
  Value timeout;
};

void socket_cleanup(void* data) {
  delete static_cast<SocketState*>(data);
}

SocketState* socket_state(const Value& self, std::string& error) {
  auto* state = static_cast<SocketState*>(instance_get_native_data(self, "_socket.socket"));
  if (state == nullptr) {
    error = "invalid socket object";
  }
  return state;
}

bool socket_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 4) {
    error = "socket.__init__() expected optional family, type, proto";
    return false;
  }
  auto* state = new SocketState();
  if (argc >= 2 && args[1].tag == ValueTag::Int64) {
    state->family = args[1].as.i64;
  }
  if (argc >= 3 && args[2].tag == ValueTag::Int64) {
    state->type = args[2].as.i64;
  }
  if (argc >= 4 && args[3].tag == ValueTag::Int64) {
    state->proto = args[3].as.i64;
  }
  value_set_none(state->timeout);
  if (!instance_set_native_data(args[0], "_socket.socket", state, socket_cleanup, error)) {
    delete state;
    return false;
  }
  Value self = args[0];
  object_set_attr(self, "family", Value::int64(state->family), error);
  object_set_attr(self, "type", Value::int64(state->type), error);
  object_set_attr(self, "proto", Value::int64(state->proto), error);
  object_set_attr(self, "__xlang3_string_value__", Value::string("<socket.socket fd=-1>"), error);
  value_set_none(out);
  return true;
}

bool socket_close(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.close() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  state->closed = true;
  value_set_none(out);
  return true;
}

bool socket_fileno(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.fileno() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  value_set_int64(out, -1);
  return true;
}

bool socket_settimeout(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "socket.settimeout() expected timeout";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  value_assign_fast(state->timeout, args[1]);
  value_set_none(out);
  return true;
}

bool socket_gettimeout(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.gettimeout() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  value_assign_fast(out, state->timeout);
  return true;
}

bool socket_unsupported(Runtime&, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "native socket networking is not enabled in this XLang3 build";
  return false;
}

Value make_socket_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("_socket.socket.__init__", socket_init)});
  attrs.push_back({"close", runtime.make_native_function("_socket.socket.close", socket_close)});
  attrs.push_back({"fileno", runtime.make_native_function("_socket.socket.fileno", socket_fileno)});
  attrs.push_back({"settimeout", runtime.make_native_function("_socket.socket.settimeout", socket_settimeout)});
  attrs.push_back({"gettimeout", runtime.make_native_function("_socket.socket.gettimeout", socket_gettimeout)});
  attrs.push_back({"bind", runtime.make_native_function("_socket.socket.bind", socket_unsupported)});
  attrs.push_back({"listen", runtime.make_native_function("_socket.socket.listen", socket_unsupported)});
  attrs.push_back({"accept", runtime.make_native_function("_socket.socket.accept", socket_unsupported)});
  attrs.push_back({"connect", runtime.make_native_function("_socket.socket.connect", socket_unsupported)});
  attrs.push_back({"send", runtime.make_native_function("_socket.socket.send", socket_unsupported)});
  attrs.push_back({"sendall", runtime.make_native_function("_socket.socket.sendall", socket_unsupported)});
  attrs.push_back({"recv", runtime.make_native_function("_socket.socket.recv", socket_unsupported)});
  return Value::class_object("socket", std::move(attrs));
}

void add_socket_exports(NativeModuleBuilder& builder, const Value& socket_class) {
  builder.value("AF_UNSPEC", Value::int64(kAfUnspec))
      .value("AF_INET", Value::int64(kAfInet))
      .value("SOCK_STREAM", Value::int64(kSockStream))
      .value("SOCK_DGRAM", Value::int64(kSockDgram))
      .value("SOL_SOCKET", Value::int64(1))
      .value("SO_REUSEADDR", Value::int64(2))
      .value("SHUT_RD", Value::int64(0))
      .value("SHUT_WR", Value::int64(1))
      .value("SHUT_RDWR", Value::int64(2))
      .value("timeout", Value::string("socket.timeout"))
      .value("error", Value::string("socket.error"))
      .value("socket", socket_class);
}

bool select_select(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 4) {
    error = "select.select() expected rlist, wlist, xlist, optional timeout";
    return false;
  }
  out = Value::tuple({args[0], args[1], args[2]});
  return true;
}

} // namespace

void register_socket_modules(Runtime& runtime) {
  Value socket_class = make_socket_class(runtime);

  NativeModuleBuilder low_level(runtime, "_socket");
  add_socket_exports(low_level, socket_class);
  runtime.register_module("_socket", low_level.finish());

  NativeModuleBuilder high_level(runtime, "socket");
  add_socket_exports(high_level, socket_class);
  runtime.register_module("socket", high_level.finish());

  NativeModuleBuilder select(runtime, "select");
  select.function("select", select_select)
      .value("error", Value::string("select.error"));
  runtime.register_module("select", select.finish());
}

} // namespace xlang3
