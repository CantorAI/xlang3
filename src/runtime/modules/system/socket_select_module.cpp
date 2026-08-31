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

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace xlang3 {

namespace {

constexpr int64_t kAfUnspec = 0;
constexpr int64_t kAfInet = 2;
constexpr int64_t kAfInet6 = 23;
constexpr int64_t kSockStream = 1;
constexpr int64_t kSockDgram = 2;

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

struct SocketState {
  int64_t family = kAfInet;
  int64_t type = kSockStream;
  int64_t proto = 0;
  bool closed = false;
  std::string host = "127.0.0.1";
  int64_t port = 0;
  Value timeout;
  NativeSocket fd = kInvalidSocket;
};

void close_native_socket(NativeSocket fd) {
  if (fd == kInvalidSocket) {
    return;
  }
#ifdef _WIN32
  closesocket(fd);
#else
  close(fd);
#endif
}

bool ensure_socket_runtime(std::string& error) {
#ifdef _WIN32
  static bool initialized = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  if (!initialized) {
    error = "socket runtime initialization failed";
    return false;
  }
#else
  (void)error;
#endif
  return true;
}

int to_native_family(int64_t family) {
  return family == kAfUnspec ? AF_UNSPEC : AF_INET;
}

int to_native_type(int64_t type) {
  return type == kSockDgram ? SOCK_DGRAM : SOCK_STREAM;
}

std::string socket_last_error_text(const char* operation) {
#ifdef _WIN32
  return std::string(operation) + " failed with WSA error " + std::to_string(WSAGetLastError());
#else
  return std::string(operation) + " failed: " + std::strerror(errno);
#endif
}

NativeSocket make_native_socket(SocketState& state, std::string& error) {
  if (!ensure_socket_runtime(error)) {
    return kInvalidSocket;
  }
  if (state.fd != kInvalidSocket) {
    return state.fd;
  }
  NativeSocket fd = ::socket(to_native_family(state.family), to_native_type(state.type), static_cast<int>(state.proto));
  if (fd == kInvalidSocket) {
    error = socket_last_error_text("socket");
    return kInvalidSocket;
  }
  state.fd = fd;
  state.closed = false;
  return fd;
}

bool value_to_host_port(const Value& value, std::string& host, int64_t& port, std::string& error) {
  auto* address = value_as_tuple(value);
  if (address == nullptr || address->items.size() < 2) {
    error = "socket address must be a (host, port) tuple";
    return false;
  }
  auto* host_value = value_as_string(address->items[0]);
  if (host_value == nullptr || address->items[1].tag != ValueTag::Int64) {
    error = "socket address must be a (host, port) tuple";
    return false;
  }
  host = string_object_to_string(*host_value);
  port = address->items[1].as.i64;
  return true;
}

bool fill_ipv4_address(const std::string& host, int64_t port, sockaddr_in& address, std::string& error) {
  std::memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<uint16_t>(port));
  const std::string bind_host = host.empty() ? "127.0.0.1" : host;
  if (bind_host == "localhost") {
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return true;
  }
  if (bind_host == "0.0.0.0") {
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    return true;
  }
  if (inet_pton(AF_INET, bind_host.c_str(), &address.sin_addr) == 1) {
    return true;
  }
  error = "only IPv4 socket addresses are supported";
  return false;
}

void update_socket_address_from_fd(SocketState& state) {
  if (state.fd == kInvalidSocket) {
    return;
  }
  sockaddr_in address;
  std::memset(&address, 0, sizeof(address));
#ifdef _WIN32
  int length = sizeof(address);
#else
  socklen_t length = sizeof(address);
#endif
  if (getsockname(state.fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
    return;
  }
  char host[INET_ADDRSTRLEN] = {};
  if (inet_ntop(AF_INET, &address.sin_addr, host, sizeof(host)) != nullptr) {
    state.host = host;
  }
  state.port = ntohs(address.sin_port);
}

void socket_cleanup(void* data) {
  auto* state = static_cast<SocketState*>(data);
  if (state != nullptr) {
    close_native_socket(state->fd);
  }
  delete state;
}

SocketState* socket_state(const Value& self, std::string& error) {
  auto* state = static_cast<SocketState*>(instance_get_native_data(self, "_socket.socket"));
  if (state == nullptr) {
    error = "invalid socket object";
  }
  return state;
}

bool socket_int_arg(const Value& value, int64_t& out) {
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64;
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    out = value.as.b ? 1 : 0;
    return true;
  }
  Value attr;
  std::string ignored;
  if ((object_get_attr(value, "__xlang3_int_value__", attr, ignored) ||
       object_get_attr(value, "_value_", attr, ignored)) &&
      attr.tag == ValueTag::Int64) {
    out = attr.as.i64;
    return true;
  }
  return false;
}

void socket_set_instance_attr(const Value& self, const std::string& name, const Value& value) {
  auto* instance = value_as_instance(self);
  if (instance == nullptr) {
    return;
  }
  for (auto& attr : instance->attrs) {
    if (attr.first == name) {
      value_assign_fast(attr.second, value);
      return;
    }
  }
  instance->attrs.push_back({name, value});
}

bool socket_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 5) {
    error = "socket.__init__() expected optional family, type, proto, fileno";
    return false;
  }
  auto* state = new SocketState();
  int64_t int_value = 0;
  if (argc >= 2 && socket_int_arg(args[1], int_value)) {
    state->family = int_value;
  }
  if (argc >= 3 && socket_int_arg(args[2], int_value)) {
    state->type = int_value;
  }
  if (argc >= 4 && socket_int_arg(args[3], int_value)) {
    state->proto = int_value;
  }
  if (argc >= 5 && args[4].tag != ValueTag::None && socket_int_arg(args[4], int_value)) {
    state->fd = static_cast<NativeSocket>(int_value);
    state->closed = state->fd == kInvalidSocket;
  }
  value_set_none(state->timeout);
  if (!instance_set_native_data(args[0], "_socket.socket", state, socket_cleanup, error)) {
    delete state;
    return false;
  }
  socket_set_instance_attr(args[0], "family", Value::int64(state->family));
  socket_set_instance_attr(args[0], "type", Value::int64(state->type));
  socket_set_instance_attr(args[0], "proto", Value::int64(state->proto));
  socket_set_instance_attr(args[0], "__xlang3_string_value__", Value::string("<socket.socket fd=-1>"));
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
  close_native_socket(state->fd);
  state->fd = kInvalidSocket;
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
  value_set_int64(out, state->fd == kInvalidSocket ? -1 : static_cast<int64_t>(state->fd));
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

bool socket_setsockopt(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 4 || argc > 5) {
    error = "socket.setsockopt() expected level, optname, value";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
  if (args[1].tag == ValueTag::Int64 && args[2].tag == ValueTag::Int64 && args[3].tag == ValueTag::Int64) {
    int value = static_cast<int>(args[3].as.i64);
    setsockopt(
        fd,
        static_cast<int>(args[1].as.i64),
        static_cast<int>(args[2].as.i64),
        reinterpret_cast<const char*>(&value),
        sizeof(value));
  }
  value_set_none(out);
  return true;
}

bool socket_bind(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "socket.bind() expected address";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string host;
  int64_t port = 0;
  if (!value_to_host_port(args[1], host, port, error)) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
  sockaddr_in address;
  if (!fill_ipv4_address(host, port, address, error)) {
    return false;
  }
  if (::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    error = socket_last_error_text("bind");
    return false;
  }
  update_socket_address_from_fd(*state);
  value_set_none(out);
  return true;
}

bool socket_listen(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "socket.listen() expected optional backlog";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
  const int backlog = argc == 2 && args[1].tag == ValueTag::Int64 ? static_cast<int>(args[1].as.i64) : 128;
  if (::listen(fd, backlog) != 0) {
    error = socket_last_error_text("listen");
    return false;
  }
  update_socket_address_from_fd(*state);
  value_set_none(out);
  return true;
}

bool socket_getsockname(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.getsockname() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = Value::tuple({Value::string(state->host), Value::int64(state->port)});
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

bool socket_accept(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.accept() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
  sockaddr_in peer;
  std::memset(&peer, 0, sizeof(peer));
#ifdef _WIN32
  int peer_length = sizeof(peer);
#else
  socklen_t peer_length = sizeof(peer);
#endif
  NativeSocket accepted = ::accept(fd, reinterpret_cast<sockaddr*>(&peer), &peer_length);
  if (accepted == kInvalidSocket) {
    error = socket_last_error_text("accept");
    return false;
  }

  auto* instance = value_as_instance(args[0]);
  if (instance == nullptr) {
    close_native_socket(accepted);
    error = "socket.accept() target is not a socket instance";
    return false;
  }
  Value accepted_socket = Value::instance(instance->klass);
  auto* accepted_state = new SocketState();
  accepted_state->family = state->family;
  accepted_state->type = state->type;
  accepted_state->proto = state->proto;
  accepted_state->fd = accepted;
  value_set_none(accepted_state->timeout);
  update_socket_address_from_fd(*accepted_state);
  if (!instance_set_native_data(accepted_socket, "_socket.socket", accepted_state, socket_cleanup, error)) {
    close_native_socket(accepted);
    delete accepted_state;
    return false;
  }

  char peer_host[INET_ADDRSTRLEN] = {};
  std::string peer_host_text = "127.0.0.1";
  if (inet_ntop(AF_INET, &peer.sin_addr, peer_host, sizeof(peer_host)) != nullptr) {
    peer_host_text = peer_host;
  }
  Value peer_address = Value::tuple({Value::string(peer_host_text), Value::int64(ntohs(peer.sin_port))});
  out = Value::tuple({accepted_socket, peer_address});
  return true;
}

bool socket_connect(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "socket.connect() expected address";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string host;
  int64_t port = 0;
  if (!value_to_host_port(args[1], host, port, error)) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
  sockaddr_in address;
  if (!fill_ipv4_address(host, port, address, error)) {
    return false;
  }
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    error = socket_last_error_text("connect");
    return false;
  }
  state->host = host.empty() ? "127.0.0.1" : host;
  state->port = port;
  value_set_none(out);
  return true;
}

bool socket_send_impl(const Value* args, uint32_t argc, Value& out, std::string& error, bool send_all) {
  if (argc != 2) {
    error = send_all ? "socket.sendall() expected data" : "socket.send() expected data";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }

  std::string data;
  if (auto* bytes = value_as_bytes(args[1])) {
    data = bytes_object_to_string(*bytes);
  } else if (auto* text = value_as_string(args[1])) {
    data = string_object_to_string(*text);
  } else {
    error = send_all ? "socket.sendall() data must be bytes-like" : "socket.send() data must be bytes-like";
    return false;
  }

  size_t total = 0;
  while (total < data.size()) {
    const int chunk = static_cast<int>(std::min<size_t>(data.size() - total, 65536));
    const int sent = ::send(fd, data.data() + total, chunk, 0);
    if (sent <= 0) {
      error = socket_last_error_text(send_all ? "sendall" : "send");
      return false;
    }
    total += static_cast<size_t>(sent);
    if (!send_all) {
      break;
    }
  }
  if (send_all) {
    value_set_none(out);
  } else {
    value_set_int64(out, static_cast<int64_t>(total));
  }
  return true;
}

bool socket_send(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return socket_send_impl(args, argc, out, error, false);
}

bool socket_sendall(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return socket_send_impl(args, argc, out, error, true);
}

bool socket_recv(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "socket.recv() expected size";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
  const int size = static_cast<int>(std::max<int64_t>(0, args[1].as.i64));
  std::string data(static_cast<size_t>(size), '\0');
  const int received = ::recv(fd, data.data(), size, 0);
  if (received < 0) {
    error = socket_last_error_text("recv");
    return false;
  }
  data.resize(static_cast<size_t>(received));
  out = Value::bytes(std::move(data));
  return true;
}

bool socket_makefile(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "socket.makefile() expected optional mode and buffering";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (make_native_socket(*state, error) == kInvalidSocket) {
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool socket_read(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "socket file read() expected size";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
  const int size = static_cast<int>(std::max<int64_t>(0, args[1].as.i64));
  std::string data(static_cast<size_t>(size), '\0');
  const int received = ::recv(fd, data.data(), size, 0);
  if (received < 0) {
    error = socket_last_error_text("read");
    return false;
  }
  data.resize(static_cast<size_t>(received));
  out = Value::bytes(std::move(data));
  return true;
}

bool socket_readline(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket file readline() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
  std::string line;
  char ch = 0;
  while (true) {
    const int received = ::recv(fd, &ch, 1, 0);
    if (received <= 0) {
      break;
    }
    line.push_back(ch);
    if (ch == '\n') {
      break;
    }
  }
  out = Value::bytes(std::move(line));
  return true;
}

bool socket_write(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!socket_send_impl(args, argc, out, error, true)) {
    return false;
  }
  if (auto* bytes = value_as_bytes(args[1])) {
    value_set_int64(out, static_cast<int64_t>(bytes->size));
  } else if (auto* text = value_as_string(args[1])) {
    value_set_int64(out, static_cast<int64_t>(text->size));
  }
  return true;
}

bool socket_flush(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket file flush() expected no arguments";
    return false;
  }
  value_set_none(out);
  return true;
}

bool socket_shutdown(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "socket.shutdown() expected how";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->fd != kInvalidSocket) {
    ::shutdown(state->fd, static_cast<int>(args[1].as.i64));
  }
  value_set_none(out);
  return true;
}

bool socket_unsupported(Runtime&, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "socket operation is not supported";
  return false;
}

bool socket_gethostname(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "socket.gethostname() expected no arguments";
    return false;
  }
  out = Value::string("localhost");
  return true;
}

bool socket_getaddrinfo(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "socket.getaddrinfo() expected host and port";
    return false;
  }
  out = Value::list({});
  return true;
}

Value make_socket_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function("_socket.socket.__init__", socket_init)});
  attrs.push_back({"close", runtime.make_native_function("_socket.socket.close", socket_close)});
  attrs.push_back({"fileno", runtime.make_native_function("_socket.socket.fileno", socket_fileno)});
  attrs.push_back({"settimeout", runtime.make_native_function("_socket.socket.settimeout", socket_settimeout)});
  attrs.push_back({"gettimeout", runtime.make_native_function("_socket.socket.gettimeout", socket_gettimeout)});
  attrs.push_back({"setsockopt", runtime.make_native_function("_socket.socket.setsockopt", socket_setsockopt)});
  attrs.push_back({"bind", runtime.make_native_function("_socket.socket.bind", socket_bind)});
  attrs.push_back({"listen", runtime.make_native_function("_socket.socket.listen", socket_listen)});
  attrs.push_back({"getsockname", runtime.make_native_function("_socket.socket.getsockname", socket_getsockname)});
  attrs.push_back({"accept", runtime.make_native_function("_socket.socket.accept", socket_accept)});
  attrs.push_back({"connect", runtime.make_native_function("_socket.socket.connect", socket_connect)});
  attrs.push_back({"send", runtime.make_native_function("_socket.socket.send", socket_send)});
  attrs.push_back({"sendall", runtime.make_native_function("_socket.socket.sendall", socket_sendall)});
  attrs.push_back({"recv", runtime.make_native_function("_socket.socket.recv", socket_recv)});
  attrs.push_back({"makefile", runtime.make_native_function("_socket.socket.makefile", socket_makefile)});
  attrs.push_back({"read", runtime.make_native_function("_socket.socket.read", socket_read)});
  attrs.push_back({"readline", runtime.make_native_function("_socket.socket.readline", socket_readline)});
  attrs.push_back({"write", runtime.make_native_function("_socket.socket.write", socket_write)});
  attrs.push_back({"flush", runtime.make_native_function("_socket.socket.flush", socket_flush)});
  attrs.push_back({"shutdown", runtime.make_native_function("_socket.socket.shutdown", socket_shutdown)});
  return Value::class_object("socket", std::move(attrs));
}

void add_socket_exports(Runtime& runtime, NativeModuleBuilder& builder, const Value& socket_class) {
  Value socket_error = Value::string("socket.error");
  if (auto* os_error = runtime.find_builtin("OSError")) {
    value_assign_fast(socket_error, *os_error);
  }
  builder.value("AF_UNSPEC", Value::int64(kAfUnspec))
      .value("AF_INET", Value::int64(kAfInet))
      .value("AF_INET6", Value::int64(kAfInet6))
      .value("SOCK_STREAM", Value::int64(kSockStream))
      .value("SOCK_DGRAM", Value::int64(kSockDgram))
      .value("IPPROTO_TCP", Value::int64(IPPROTO_TCP))
#ifdef TCP_KEEPIDLE
      .value("TCP_KEEPIDLE", Value::int64(TCP_KEEPIDLE))
#else
      .value("TCP_KEEPIDLE", Value::int64(3))
#endif
#ifdef TCP_KEEPINTVL
      .value("TCP_KEEPINTVL", Value::int64(TCP_KEEPINTVL))
#else
      .value("TCP_KEEPINTVL", Value::int64(17))
#endif
#ifdef TCP_KEEPCNT
      .value("TCP_KEEPCNT", Value::int64(TCP_KEEPCNT))
#else
      .value("TCP_KEEPCNT", Value::int64(16))
#endif
      .value("SO_KEEPALIVE", Value::int64(SO_KEEPALIVE))
      .value("SOL_SOCKET", Value::int64(1))
      .value("SO_REUSEADDR", Value::int64(2))
      .value("SO_EXCLUSIVEADDRUSE", Value::int64(-5))
      .value("SOMAXCONN", Value::int64(128))
      .value("SHUT_RD", Value::int64(0))
      .value("SHUT_WR", Value::int64(1))
      .value("SHUT_RDWR", Value::int64(2))
      .value("timeout", Value::string("socket.timeout"))
      .value("error", socket_error)
      .value("socket", socket_class)
      .function("gethostname", socket_gethostname)
      .function("getaddrinfo", socket_getaddrinfo);
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
  add_socket_exports(runtime, low_level, socket_class);
  runtime.register_module("_socket", low_level.finish());

  NativeModuleBuilder select(runtime, "select");
  select.function("select", select_select)
      .value("error", Value::string("select.error"));
  runtime.register_module("select", select.finish());
}

} // namespace xlang3
