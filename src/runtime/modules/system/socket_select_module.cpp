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

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
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

Value g_default_socket_timeout;
std::mutex g_socket_resource_warning_mutex;
std::vector<std::string> g_socket_resource_warnings;

struct SocketState {
  int64_t family = kAfInet;
  int64_t type = kSockStream;
  int64_t proto = 0;
  bool closed = false;
  std::string host = "127.0.0.1";
  int64_t port = 0;
  Value timeout;
  NativeSocket fd = kInvalidSocket;
  bool blocking = true;
};

struct SelectEntry {
  Value value;
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
  if (family == kAfUnspec) return AF_UNSPEC;
  if (family == kAfInet6) return AF_INET6;
  return AF_INET;
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

bool socket_last_error_would_block() {
#ifdef _WIN32
  const int code = WSAGetLastError();
  return code == WSAEWOULDBLOCK || code == WSAEINPROGRESS || code == WSAEALREADY;
#else
  return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS || errno == EALREADY;
#endif
}

bool socket_timeout_seconds(const SocketState& state, double& timeout, std::string& error) {
  if (state.timeout.tag == ValueTag::None) {
    timeout = -1.0;
    return true;
  }
  if (state.timeout.tag == ValueTag::Int64) {
    timeout = static_cast<double>(state.timeout.as.i64);
  } else if (state.timeout.tag == ValueTag::Double) {
    timeout = state.timeout.as.f64;
  } else {
    error = "socket timeout must be a number or None";
    return false;
  }
  if (timeout < 0.0) {
    error = "socket timeout must be non-negative";
    return false;
  }
  return true;
}

bool timeout_value_seconds(const Value& value, double& timeout, std::string& error) {
  if (value.tag == ValueTag::None) {
    timeout = -1.0;
    return true;
  }
  if (value.tag == ValueTag::Bool) {
    timeout = value.as.b ? 1.0 : 0.0;
  } else if (value.tag == ValueTag::Int64) {
    timeout = static_cast<double>(value.as.i64);
  } else if (value.tag == ValueTag::Double) {
    timeout = value.as.f64;
  } else {
    error = "timeout must be a number or None";
    return false;
  }
  if (timeout < 0.0) {
    error = "timeout must be non-negative";
    return false;
  }
  return true;
}

bool wait_socket_connect(Runtime& runtime, NativeSocket fd, double timeout, std::string& error) {
  if (timeout == 0.0) {
    runtime.raise_class_error("BlockingIOError", socket_last_error_text("connect"));
    return false;
  }

  fd_set write_set;
  FD_ZERO(&write_set);
  FD_SET(fd, &write_set);
  fd_set error_set;
  FD_ZERO(&error_set);
  FD_SET(fd, &error_set);
  timeval tv{};
  timeval* tv_ptr = nullptr;
  if (timeout >= 0.0) {
    tv.tv_sec = static_cast<long>(timeout);
    tv.tv_usec = static_cast<long>((timeout - static_cast<double>(tv.tv_sec)) * 1000000.0);
    tv_ptr = &tv;
  }

#ifdef _WIN32
  const int ready = ::select(0, nullptr, &write_set, &error_set, tv_ptr);
#else
  const int ready = ::select(fd + 1, nullptr, &write_set, &error_set, tv_ptr);
#endif
  if (ready == 0) {
    error = "timed out";
    runtime.raise_class_error("TimeoutError", error);
    return false;
  }
  if (ready < 0) {
    error = socket_last_error_text("select");
    return false;
  }

  int socket_error = 0;
#ifdef _WIN32
  int option_length = sizeof(socket_error);
#else
  socklen_t option_length = sizeof(socket_error);
#endif
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &option_length) != 0) {
    error = socket_last_error_text("getsockopt");
    return false;
  }
  if (socket_error != 0) {
#ifdef _WIN32
    error = "connect failed with WSA error " + std::to_string(socket_error);
#else
    error = std::string("connect failed: ") + std::strerror(socket_error);
#endif
    return false;
  }
  return true;
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
#ifdef _WIN32
  SetHandleInformation(reinterpret_cast<HANDLE>(fd), HANDLE_FLAG_INHERIT, 0);
#else
  const int descriptor_flags = fcntl(fd, F_GETFD);
  if (descriptor_flags >= 0) fcntl(fd, F_SETFD, descriptor_flags | FD_CLOEXEC);
#endif
  state.closed = false;
  return fd;
}

bool apply_socket_blocking(SocketState& state, bool blocking, std::string& error) {
  NativeSocket fd = make_native_socket(state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
#ifdef _WIN32
  u_long mode = blocking ? 0 : 1;
  if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
    error = socket_last_error_text("ioctlsocket");
    return false;
  }
#else
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    error = socket_last_error_text("fcntl");
    return false;
  }
  if (blocking) {
    flags &= ~O_NONBLOCK;
  } else {
    flags |= O_NONBLOCK;
  }
  if (fcntl(fd, F_SETFL, flags) != 0) {
    error = socket_last_error_text("fcntl");
    return false;
  }
#endif
  state.blocking = blocking;
  return true;
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

struct SocketAddress {
  sockaddr_storage storage{};
  socklen_t length = 0;
};

bool fill_socket_address(int64_t family, const std::string& host, int64_t port,
                         SocketAddress& address, std::string& error) {
  if (port < 0 || port > 65535) {
    error = "port must be 0-65535";
    return false;
  }
  std::memset(&address.storage, 0, sizeof(address.storage));
  if (family == kAfInet6) {
    auto* ipv6 = reinterpret_cast<sockaddr_in6*>(&address.storage);
    ipv6->sin6_family = AF_INET6;
    ipv6->sin6_port = htons(static_cast<uint16_t>(port));
    const std::string bind_host = host.empty() ? "::1" : host;
    if (bind_host == "localhost") {
      ipv6->sin6_addr = in6addr_loopback;
    } else if (bind_host == "::") {
      ipv6->sin6_addr = in6addr_any;
    } else if (inet_pton(AF_INET6, bind_host.c_str(), &ipv6->sin6_addr) != 1) {
      error = "invalid IPv6 socket address";
      return false;
    }
    address.length = sizeof(sockaddr_in6);
    return true;
  }

  auto* ipv4 = reinterpret_cast<sockaddr_in*>(&address.storage);
  ipv4->sin_family = AF_INET;
  ipv4->sin_port = htons(static_cast<uint16_t>(port));
  const std::string bind_host = host.empty() ? "127.0.0.1" : host;
  if (bind_host == "localhost") {
    ipv4->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  } else if (bind_host == "0.0.0.0") {
    ipv4->sin_addr.s_addr = htonl(INADDR_ANY);
  } else if (inet_pton(AF_INET, bind_host.c_str(), &ipv4->sin_addr) != 1) {
    error = "invalid IPv4 socket address";
    return false;
  }
  address.length = sizeof(sockaddr_in);
  return true;
}

Value socket_address_value(const sockaddr* address, socklen_t length) {
  if (address != nullptr && address->sa_family == AF_INET6 && length >= sizeof(sockaddr_in6)) {
    const auto* ipv6 = reinterpret_cast<const sockaddr_in6*>(address);
    char host[INET6_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET6, &ipv6->sin6_addr, host, sizeof(host)) != nullptr) {
      return Value::tuple({Value::string(host), Value::int64(ntohs(ipv6->sin6_port)),
                           Value::int64(ipv6->sin6_flowinfo), Value::int64(ipv6->sin6_scope_id)});
    }
  }
  const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
  char host[INET_ADDRSTRLEN] = {};
  std::string host_text = "127.0.0.1";
  if (ipv4 != nullptr && inet_ntop(AF_INET, &ipv4->sin_addr, host, sizeof(host)) != nullptr) host_text = host;
  return Value::tuple({Value::string(host_text), Value::int64(ipv4 != nullptr ? ntohs(ipv4->sin_port) : 0)});
}

void update_socket_address_from_fd(SocketState& state) {
  if (state.fd == kInvalidSocket) return;
  SocketAddress address;
  address.length = sizeof(address.storage);
  if (getsockname(state.fd, reinterpret_cast<sockaddr*>(&address.storage), &address.length) != 0) return;
  Value value = socket_address_value(reinterpret_cast<const sockaddr*>(&address.storage), address.length);
  auto* tuple = value_as_tuple(value);
  if (tuple == nullptr || tuple->items.size() < 2) return;
  auto* host = value_as_string(tuple->items[0]);
  if (host != nullptr) state.host = string_object_to_string(*host);
  if (tuple->items[1].tag == ValueTag::Int64) state.port = tuple->items[1].as.i64;
}

void socket_cleanup(void* data) {
  auto* state = static_cast<SocketState*>(data);
  if (state != nullptr) {
    if (!state->closed && state->fd != kInvalidSocket) {
      std::lock_guard<std::mutex> lock(g_socket_resource_warning_mutex);
      g_socket_resource_warnings.push_back(
          "unclosed <socket.socket fd=" + std::to_string(static_cast<int64_t>(state->fd)) +
          ", family=" + std::to_string(state->family) +
          ", type=" + std::to_string(state->type) +
          ", proto=" + std::to_string(state->proto) +
          ", laddr=('" + state->host + "', " + std::to_string(state->port) + ")>");
    }
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

bool socket_value_fileno(Runtime& runtime, const Value& value, NativeSocket& fd, std::string& error) {
  if (value.tag == ValueTag::Int64) {
    fd = static_cast<NativeSocket>(value.as.i64);
    return true;
  }

  if (value.tag == ValueTag::Bool) {
    fd = static_cast<NativeSocket>(value.as.b ? 1 : 0);
    return true;
  }

  std::string state_error;
  if (auto* state = socket_state(value, state_error)) {
    fd = make_native_socket(*state, error);
    return fd != kInvalidSocket;
  }

  std::string attr_error;
  Value fileno;
  if (!object_get_attr(value, "fileno", fileno, attr_error)) {
    error = "argument must be an int or have a fileno() method";
    return false;
  }

  Value result;
  if (!runtime_call_callable(runtime, fileno, nullptr, 0, result, error)) {
    return false;
  }
  if (result.tag != ValueTag::Int64) {
    error = "fileno() returned a non-integer";
    return false;
  }
  fd = static_cast<NativeSocket>(result.as.i64);
  return true;
}

bool collect_select_entries(
    Runtime& runtime,
    const Value& iterable,
    std::vector<SelectEntry>& entries,
    fd_set& set,
    NativeSocket& max_fd,
    std::string& error) {
  std::vector<Value> values;
  if (auto* list = value_as_list(iterable)) {
    values = list->items;
  } else if (auto* tuple = value_as_tuple(iterable)) {
    values = tuple->items;
  } else if (!runtime_collect_iterable(runtime, iterable, values, error)) {
    return false;
  }

  entries.reserve(values.size());
  for (const auto& value : values) {
    NativeSocket fd = kInvalidSocket;
    if (!socket_value_fileno(runtime, value, fd, error)) {
      return false;
    }
    if (fd == kInvalidSocket) {
      error = "file descriptor cannot be a negative integer (-1)";
      return false;
    }
    FD_SET(fd, &set);
#ifndef _WIN32
    if (fd > max_fd) {
      max_fd = fd;
    }
#else
    (void)max_fd;
#endif
    entries.push_back({value, fd});
  }
  return true;
}

Value select_ready_values(const std::vector<SelectEntry>& entries, fd_set& set) {
  std::vector<Value> ready;
  ready.reserve(entries.size());
  for (const auto& entry : entries) {
    if (FD_ISSET(entry.fd, &set)) {
      ready.push_back(entry.value);
    }
  }
  return Value::list(std::move(ready));
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

bool contains_utf8_surrogate(std::string_view text) {
  for (size_t index = 0; index + 2 < text.size(); ++index) {
    const auto first = static_cast<unsigned char>(text[index]);
    const auto second = static_cast<unsigned char>(text[index + 1]);
    const auto third = static_cast<unsigned char>(text[index + 2]);
    if (first == 0xedu && second >= 0xa0u && second <= 0xbfu &&
        third >= 0x80u && third <= 0xbfu) {
      return true;
    }
    if (first == 0xefu && second == 0xbfu && third == 0xbdu) {
      return true;
    }
  }
  return false;
}

bool raise_socket_code_error(Runtime& runtime, const char* operation, int code, std::string& error) {
#ifdef _WIN32
  error = std::string(operation) + " failed with WSA error " + std::to_string(code);
#else
  error = std::string(operation) + " failed: " + std::strerror(code);
#endif
  Value exception = runtime.make_exception("OSError", error);
  std::string ignored;
  object_set_attr(exception, "errno", Value::int64(code), ignored);
  object_set_attr(exception, "strerror", Value::string(error), ignored);
#ifdef _WIN32
  object_set_attr(exception, "winerror", Value::int64(code), ignored);
#endif
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool raise_socket_os_error(Runtime& runtime, const char* operation, std::string& error) {
#ifdef _WIN32
  return raise_socket_code_error(runtime, operation, WSAGetLastError(), error);
#else
  return raise_socket_code_error(runtime, operation, errno, error);
#endif
}

bool socket_dup_fd(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "dup() expected a file descriptor";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t raw_fd = -1;
  if (!socket_int_arg(args[0], raw_fd)) {
    error = "file descriptor must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#ifdef _WIN32
  if (!ensure_socket_runtime(error)) {
    runtime.raise_class_error("OSError", error);
    return false;
  }
  WSAPROTOCOL_INFOW protocol_info{};
  const NativeSocket source = static_cast<NativeSocket>(raw_fd);
  if (WSADuplicateSocketW(source, GetCurrentProcessId(), &protocol_info) == SOCKET_ERROR) {
    error = socket_last_error_text("dup");
    runtime.raise_class_error("OSError", error);
    return false;
  }
  const NativeSocket duplicate = WSASocketW(
      protocol_info.iAddressFamily,
      protocol_info.iSocketType,
      protocol_info.iProtocol,
      &protocol_info,
      0,
      WSA_FLAG_OVERLAPPED);
  if (duplicate == kInvalidSocket) {
    error = socket_last_error_text("dup");
    runtime.raise_class_error("OSError", error);
    return false;
  }
  SetHandleInformation(reinterpret_cast<HANDLE>(duplicate), HANDLE_FLAG_INHERIT, 0);
#else
  const NativeSocket duplicate = ::dup(static_cast<NativeSocket>(raw_fd));
  if (duplicate == kInvalidSocket) {
    error = socket_last_error_text("dup");
    runtime.raise_class_error("OSError", error);
    return false;
  }
#endif
  value_set_int64(out, static_cast<int64_t>(duplicate));
  return true;
}

bool socket_close_fd(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "close() expected a file descriptor";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t raw_fd = -1;
  if (!socket_int_arg(args[0], raw_fd)) {
    error = "file descriptor must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#ifdef _WIN32
  if (closesocket(static_cast<NativeSocket>(raw_fd)) == SOCKET_ERROR) {
#else
  if (::close(static_cast<NativeSocket>(raw_fd)) != 0) {
#endif
    return raise_socket_os_error(runtime, "close", error);
  }
  value_set_none(out);
  return true;
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

bool socket_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 5) {
    error = "socket.__init__() expected optional family, type, proto, fileno";
    return false;
  }
  if (!ensure_socket_runtime(error)) {
    runtime.raise_class_error("OSError", error);
    return false;
  }
  auto* state = new SocketState();
  int64_t int_value = 0;
  if (argc >= 2) {
    if (!socket_int_arg(args[1], int_value)) {
      delete state;
      error = "socket family must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state->family = int_value;
  }
  if (argc >= 3) {
    if (!socket_int_arg(args[2], int_value)) {
      delete state;
      error = "socket type must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state->type = int_value;
  }
  if (argc >= 4) {
    if (!socket_int_arg(args[3], int_value)) {
      delete state;
      error = "socket protocol must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state->proto = int_value;
  }
  if (argc >= 5 && args[4].tag != ValueTag::None) {
    if (!socket_int_arg(args[4], int_value)) {
      delete state;
      error = "socket fileno must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state->fd = static_cast<NativeSocket>(int_value);
    if (state->fd == kInvalidSocket) {
      delete state;
      error = "negative file descriptor";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    int socket_type = 0;
#ifdef _WIN32
    int option_length = sizeof(socket_type);
#else
    socklen_t option_length = sizeof(socket_type);
#endif
    if (::getsockopt(state->fd, SOL_SOCKET, SO_TYPE, reinterpret_cast<char*>(&socket_type), &option_length) != 0) {
      delete state;
      return raise_socket_os_error(runtime, "socket", error);
    }
    // A fileno-only call reaches this native constructor with CPython's
    // default IPv4/stream/zero values.  Derive those defaults from the
    // descriptor, but retain explicitly supplied non-default metadata.
    if (state->family < 0 || state->type < 0 || state->proto < 0 ||
        (state->family == kAfInet && state->type == kSockStream && state->proto == 0)) {
      SocketAddress native_address;
      native_address.length = sizeof(native_address.storage);
      if (::getsockname(state->fd, reinterpret_cast<sockaddr*>(&native_address.storage),
                        &native_address.length) == 0) {
        const auto family = reinterpret_cast<const sockaddr*>(&native_address.storage)->sa_family;
        state->family = family == AF_INET6 ? kAfInet6 : kAfInet;
      }
      state->type = socket_type == SOCK_DGRAM ? kSockDgram : kSockStream;
    }
    if (state->proto < 0) state->proto = 0;
  } else if (make_native_socket(*state, error) == kInvalidSocket) {
    delete state;
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_assign_fast(state->timeout, g_default_socket_timeout);
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

bool socket_close(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.close() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (state->closed || state->fd == kInvalidSocket) {
    value_set_none(out);
    return true;
  }
#ifdef _WIN32
  if (closesocket(state->fd) == SOCKET_ERROR) {
    return raise_socket_os_error(runtime, "close", error);
  }
#else
  if (::close(state->fd) != 0) {
    return raise_socket_os_error(runtime, "close", error);
  }
#endif
  state->fd = kInvalidSocket;
  state->closed = true;
  value_set_none(out);
  return true;
}

bool socket_reduce_ex(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "cannot pickle 'socket' object";
  runtime.raise_class_error("TypeError", error);
  return false;
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

bool socket_get_inheritable(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.get_inheritable() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr || state->fd == kInvalidSocket) {
    error = "Bad file descriptor";
    return raise_socket_code_error(runtime, "get_inheritable", WSAENOTSOCK, error);
  }
#ifdef _WIN32
  DWORD flags = 0;
  if (!GetHandleInformation(reinterpret_cast<HANDLE>(state->fd), &flags)) {
    return raise_socket_os_error(runtime, "get_inheritable", error);
  }
  value_set_bool(out, (flags & HANDLE_FLAG_INHERIT) != 0);
#else
  const int flags = fcntl(state->fd, F_GETFD);
  if (flags < 0) return raise_socket_os_error(runtime, "get_inheritable", error);
  value_set_bool(out, (flags & FD_CLOEXEC) == 0);
#endif
  return true;
}

bool socket_set_inheritable(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "socket.set_inheritable() expected inheritable flag";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr || state->fd == kInvalidSocket) {
    error = "Bad file descriptor";
    return raise_socket_code_error(runtime, "set_inheritable", WSAENOTSOCK, error);
  }
  const bool inheritable = value_truthy(args[1]);
#ifdef _WIN32
  if (!SetHandleInformation(reinterpret_cast<HANDLE>(state->fd), HANDLE_FLAG_INHERIT,
                            inheritable ? HANDLE_FLAG_INHERIT : 0)) {
    return raise_socket_os_error(runtime, "set_inheritable", error);
  }
#else
  int flags = fcntl(state->fd, F_GETFD);
  if (flags < 0 || fcntl(state->fd, F_SETFD, inheritable ? flags & ~FD_CLOEXEC : flags | FD_CLOEXEC) < 0) {
    return raise_socket_os_error(runtime, "set_inheritable", error);
  }
#endif
  value_set_none(out);
  return true;
}

bool socket_repr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.__repr__() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  out = Value::string(
      "<socket object, fd=" +
      std::to_string(state->fd == kInvalidSocket ? -1 : static_cast<int64_t>(state->fd)) +
      ", family=" + std::to_string(state->family) +
      ", type=" + std::to_string(state->type) +
      ", proto=" + std::to_string(state->proto) + ">");
  return true;
}

bool socket_detach(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.detach() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  value_set_int64(out, state->fd == kInvalidSocket ? -1 : static_cast<int64_t>(state->fd));
  state->fd = kInvalidSocket;
  state->closed = true;
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
  if (args[1].tag == ValueTag::None) {
    if (!apply_socket_blocking(*state, true, error)) {
      return false;
    }
  } else {
    if (!apply_socket_blocking(*state, false, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool socket_setblocking(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "socket.setblocking() expected flag";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  const bool blocking = value_truthy(args[1]);
  if (!apply_socket_blocking(*state, blocking, error)) {
    return false;
  }
  if (blocking) {
    value_set_none(state->timeout);
  } else {
    state->timeout = Value::number(0.0);
  }
  value_set_none(out);
  return true;
}

bool socket_getdefaulttimeout(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "getdefaulttimeout() takes no arguments";
    return false;
  }
  value_assign_fast(out, g_default_socket_timeout);
  return true;
}

bool socket_setdefaulttimeout(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "setdefaulttimeout() expected timeout";
    return false;
  }
  if (args[0].tag != ValueTag::None && args[0].tag != ValueTag::Int64 && args[0].tag != ValueTag::Double) {
    error = "setdefaulttimeout() timeout must be a number or None";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if ((args[0].tag == ValueTag::Int64 && args[0].as.i64 < 0) ||
      (args[0].tag == ValueTag::Double && args[0].as.f64 < 0.0)) {
    error = "Timeout value out of range";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  value_assign_fast(g_default_socket_timeout, args[0]);
  value_set_none(out);
  return true;
}

bool socket_setsockopt(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
  if (args[1].tag != ValueTag::Int64 || args[2].tag != ValueTag::Int64) {
    error = "socket.setsockopt() level and optname must be integers";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int level = static_cast<int>(args[1].as.i64);
  const int option = static_cast<int>(args[2].as.i64);
  if (args[3].tag == ValueTag::Int64) {
    if (argc != 4) {
      error = "socket.setsockopt() integer value does not take optlen";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    int value = static_cast<int>(args[3].as.i64);
    if (setsockopt(
        fd,
        level,
        option,
        reinterpret_cast<const char*>(&value),
        sizeof(value)) != 0) {
      return raise_socket_os_error(runtime, "setsockopt", error);
    }
  } else if (args[3].tag == ValueTag::None) {
    if (argc != 5 || args[4].tag != ValueTag::Int64 || args[4].as.i64 < 0) {
      error = "socket.setsockopt() None value requires a non-negative integer optlen";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (setsockopt(fd, level, option, nullptr, static_cast<int>(args[4].as.i64)) != 0) {
      return raise_socket_os_error(runtime, "setsockopt", error);
    }
  } else {
    if (argc != 4) {
      error = "socket.setsockopt() bytes-like value does not take optlen";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    std::string_view value;
    if (auto* bytes = value_as_bytes(args[3])) value = bytes_object_view(*bytes);
    else if (auto* bytearray = value_as_bytearray(args[3])) value = bytearray->value;
    else if (auto* view = value_as_memoryview(args[3])) value = memoryview_object_view(*view);
    else {
      error = "socket.setsockopt() value must be an integer or bytes-like object";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (!value.data() && !value.empty()) {
      error = "socket.setsockopt() value is a released memoryview";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (setsockopt(fd, level, option, value.data(), static_cast<int>(value.size())) != 0) {
      return raise_socket_os_error(runtime, "setsockopt", error);
    }
  }
  value_set_none(out);
  return true;
}

bool wait_socket_readable(Runtime& runtime, NativeSocket fd, double timeout, const char* operation, std::string& error) {
  if (timeout < 0.0) {
    return true;
  }
  if (timeout == 0.0) {
    runtime.raise_class_error("BlockingIOError", socket_last_error_text(operation));
    return false;
  }

  fd_set read_set;
  FD_ZERO(&read_set);
  FD_SET(fd, &read_set);
  fd_set error_set;
  FD_ZERO(&error_set);
  FD_SET(fd, &error_set);

  timeval tv{};
  tv.tv_sec = static_cast<long>(timeout);
  tv.tv_usec = static_cast<long>((timeout - static_cast<double>(tv.tv_sec)) * 1000000.0);

#ifdef _WIN32
  const int ready = ::select(0, &read_set, nullptr, &error_set, &tv);
#else
  const int ready = ::select(fd + 1, &read_set, nullptr, &error_set, &tv);
#endif
  if (ready == 0) {
    runtime.raise_class_error("TimeoutError", std::string(operation) + " timed out");
    return false;
  }
  if (ready < 0) {
    error = socket_last_error_text("select");
    return false;
  }
  if (FD_ISSET(fd, &error_set)) {
    error = socket_last_error_text(operation);
    return false;
  }
  return true;
}

bool prepare_socket_read(Runtime& runtime, SocketState& state, NativeSocket fd, const char* operation, std::string& error) {
  double timeout = -1.0;
  if (!socket_timeout_seconds(state, timeout, error)) {
    return false;
  }
  if (!state.blocking) {
    // A true nonblocking socket (setblocking(False), represented by a zero
    // timeout) must attempt recv immediately.  The recv call itself reports
    // WSAEWOULDBLOCK/EAGAIN when no data is available.  Preemptively raising
    // here also discarded data after select() had reported the socket ready.
    if (timeout == 0.0) {
      return true;
    }
    return wait_socket_readable(runtime, fd, timeout, operation, error);
  }
  return true;
}

bool socket_bind(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
  if (port < 0 || port > 65535) {
    error = "bind(): port must be 0-65535";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  if (state->family == kAfInet6) {
    auto* address_tuple = value_as_tuple(args[1]);
    if (address_tuple != nullptr && address_tuple->items.size() >= 3) {
      int64_t flowinfo = 0;
      if (!socket_int_arg(address_tuple->items[2], flowinfo) || flowinfo < 0 || flowinfo > 0xFFFFF) {
        error = "bind(): IPv6 flowinfo out of range";
        runtime.raise_class_error("OverflowError", error);
        return false;
      }
    }
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    return false;
  }
  SocketAddress address;
  if (!fill_socket_address(state->family, host, port, address, error)) return false;
  if (::bind(fd, reinterpret_cast<sockaddr*>(&address.storage), address.length) != 0) {
    return raise_socket_os_error(runtime, "bind", error);
  }
  update_socket_address_from_fd(*state);
  value_set_none(out);
  return true;
}

bool socket_listen(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
    return raise_socket_os_error(runtime, "listen", error);
  }
  update_socket_address_from_fd(*state);
  value_set_none(out);
  return true;
}

bool socket_getsockname(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "socket.getsockname() expected no arguments"; return false; }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) return false;
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) return false;
  SocketAddress address;
  address.length = sizeof(address.storage);
  if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address.storage), &address.length) != 0) return raise_socket_os_error(runtime, "getsockname", error);
  out = socket_address_value(reinterpret_cast<const sockaddr*>(&address.storage), address.length);
  update_socket_address_from_fd(*state);
  return true;
}

bool socket_getpeername(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "socket.getpeername() expected no arguments"; return false; }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) return false;
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) return false;
  SocketAddress address;
  address.length = sizeof(address.storage);
  if (::getpeername(fd, reinterpret_cast<sockaddr*>(&address.storage), &address.length) != 0) {
    return raise_socket_os_error(runtime, "getpeername", error);
  }
  out = socket_address_value(reinterpret_cast<const sockaddr*>(&address.storage), address.length);
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

bool socket_getblocking(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.getblocking() expected no arguments";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) return false;
  value_set_bool(out, state->blocking);
  return true;
}

bool socket_dup(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.dup() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* source = socket_state(args[0], error);
  auto* source_instance = value_as_instance(args[0]);
  if (source == nullptr || source_instance == nullptr) {
    return false;
  }
  if (source->closed || source->fd == kInvalidSocket) {
    error = "Bad file descriptor";
#ifdef _WIN32
    return raise_socket_code_error(runtime, "dup", WSAENOTSOCK, error);
#else
    return raise_socket_code_error(runtime, "dup", EBADF, error);
#endif
  }
  Value fd_argument = Value::int64(static_cast<int64_t>(source->fd));
  Value duplicated_fd;
  if (!socket_dup_fd(runtime, &fd_argument, 1, duplicated_fd, error, nullptr)) {
    return false;
  }
  auto* state = new SocketState();
  state->family = source->family;
  state->type = source->type;
  state->proto = source->proto;
  state->host = source->host;
  state->port = source->port;
  value_assign_fast(state->timeout, source->timeout);
  state->fd = static_cast<NativeSocket>(duplicated_fd.as.i64);
  state->blocking = source->blocking;
  out = Value::instance(source_instance->klass);
  if (!instance_set_native_data(out, "_socket.socket", state, socket_cleanup, error)) {
    close_native_socket(state->fd);
    delete state;
    return false;
  }
  socket_set_instance_attr(out, "family", Value::int64(state->family));
  socket_set_instance_attr(out, "type", Value::int64(state->type));
  socket_set_instance_attr(out, "proto", Value::int64(state->proto));
  socket_set_instance_attr(out, "__xlang3_string_value__", Value::string("<socket.socket fd=-1>"));
  return true;
}

bool socket_accept(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
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
  SocketAddress peer;
  peer.length = sizeof(peer.storage);
  NativeSocket accepted = ::accept(fd, reinterpret_cast<sockaddr*>(&peer.storage), &peer.length);
  if (accepted == kInvalidSocket && socket_last_error_would_block()) {
    double timeout = -1.0;
    if (!socket_timeout_seconds(*state, timeout, error) ||
        !wait_socket_readable(runtime, fd, timeout, "accept", error)) {
      return false;
    }
    accepted = ::accept(fd, reinterpret_cast<sockaddr*>(&peer.storage), &peer.length);
  }
  if (accepted == kInvalidSocket) {
    return raise_socket_os_error(runtime, "accept", error);
  }
#ifdef _WIN32
  SetHandleInformation(reinterpret_cast<HANDLE>(accepted), HANDLE_FLAG_INHERIT, 0);
#else
  const int accepted_flags = fcntl(accepted, F_GETFD);
  if (accepted_flags >= 0) fcntl(accepted, F_SETFD, accepted_flags | FD_CLOEXEC);
#endif

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

  Value peer_address = socket_address_value(reinterpret_cast<const sockaddr*>(&peer.storage), peer.length);
  out = Value::tuple({accepted_socket, peer_address});
  return true;
}

bool socket_accept_fd(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket._accept() expected no arguments";
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
  SocketAddress peer;
  peer.length = sizeof(peer.storage);
  NativeSocket accepted = ::accept(fd, reinterpret_cast<sockaddr*>(&peer.storage), &peer.length);
  if (accepted == kInvalidSocket && socket_last_error_would_block()) {
    double timeout = -1.0;
    if (!socket_timeout_seconds(*state, timeout, error) ||
        !wait_socket_readable(runtime, fd, timeout, "accept", error)) {
      return false;
    }
    accepted = ::accept(fd, reinterpret_cast<sockaddr*>(&peer.storage), &peer.length);
  }
  if (accepted == kInvalidSocket) {
    return raise_socket_os_error(runtime, "accept", error);
  }
#ifdef _WIN32
  SetHandleInformation(reinterpret_cast<HANDLE>(accepted), HANDLE_FLAG_INHERIT, 0);
#else
  const int accepted_flags = fcntl(accepted, F_GETFD);
  if (accepted_flags >= 0) fcntl(accepted, F_SETFD, accepted_flags | FD_CLOEXEC);
#endif

  Value peer_address = socket_address_value(reinterpret_cast<const sockaddr*>(&peer.storage), peer.length);
  out = Value::tuple({Value::int64(static_cast<int64_t>(accepted)), peer_address});
  return true;
}

bool socket_connect(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) { error = "socket.connect() expected address"; return false; }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) return false;
  std::string host; int64_t port = 0;
  if (!value_to_host_port(args[1], host, port, error)) return false;
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) return false;
  SocketAddress address;
  if (!fill_socket_address(state->family, host, port, address, error)) return false;
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address.storage), address.length) != 0) {
    if (!state->blocking && socket_last_error_would_block()) {
      double timeout = 0.0;
      if (!socket_timeout_seconds(*state, timeout, error) || !wait_socket_connect(runtime, fd, timeout, error)) return false;
    } else return raise_socket_os_error(runtime, "connect", error);
  }
  state->host = host.empty() ? (state->family == kAfInet6 ? "::1" : "127.0.0.1") : host;
  state->port = port;
  value_set_none(out);
  return true;
}

bool socket_connect_ex(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "socket.connect_ex() expected address";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = socket_state(args[0], error);
  std::string host;
  int64_t port = 0;
  if (state == nullptr || !value_to_host_port(args[1], host, port, error)) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  SocketAddress address;
  if (fd == kInvalidSocket || !fill_socket_address(state->family, host, port, address, error)) return false;
  if (::connect(fd, reinterpret_cast<sockaddr*>(&address.storage), address.length) == 0) {
    state->host = host.empty() ? "127.0.0.1" : host;
    state->port = port;
    value_set_int64(out, 0);
    return true;
  }
#ifdef _WIN32
  value_set_int64(out, WSAGetLastError());
#else
  value_set_int64(out, errno);
#endif
  return true;
}

bool socket_send_impl(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, bool send_all) {
  if ((argc != 2 && argc != 3) || (argc == 3 && args[2].tag != ValueTag::Int64)) {
    error = send_all ? "socket.sendall() expected data and optional flags" : "socket.send() expected data and optional flags";
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

  std::string_view data;
  if (auto* bytes = value_as_bytes(args[1])) {
    data = bytes_object_view(*bytes);
  } else if (auto* array = value_as_bytearray(args[1])) {
    data = array->value;
  } else if (auto* view = value_as_memoryview(args[1])) {
    data = memoryview_object_view(*view);
    if (!data.data()) { error = "invalid or released memoryview"; return false; }
  } else {
    error = "a bytes-like object is required, not '" + std::string(value_binary_type_name(args[1])) + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int flags = argc == 3 ? static_cast<int>(args[2].as.i64) : 0;

  size_t total = 0;
  while (total < data.size()) {
    const int chunk = static_cast<int>(std::min<size_t>(data.size() - total, 65536));
    const int sent = ::send(fd, data.data() + total, chunk, flags);
    if (sent <= 0) {
      error = socket_last_error_text(send_all ? "sendall" : "send");
      runtime.raise_class_error("OSError", error);
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

bool socket_send(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return socket_send_impl(runtime, args, argc, out, error, false);
}

bool socket_sendall(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return socket_send_impl(runtime, args, argc, out, error, true);
}

bool socket_sendto(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3 && argc != 4) { error = "sendto() takes 2 or 3 arguments (" + std::to_string(argc > 0 ? argc - 1 : 0) + " given)"; runtime.raise_class_error("TypeError", error); return false; }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) return false;
  std::string_view data;
  if (auto* bytes = value_as_bytes(args[1])) data = bytes_object_view(*bytes);
  else if (auto* array = value_as_bytearray(args[1])) data = array->value;
  else if (auto* view = value_as_memoryview(args[1])) { data = memoryview_object_view(*view); if (!data.data()) { error = "invalid or released memoryview"; return false; } }
  else { error = "a bytes-like object is required, not '" + std::string(value_binary_type_name(args[1])) + "'"; runtime.raise_class_error("TypeError", error); return false; }
  const uint32_t address_index = argc == 3 ? 2 : 3;
  if (args[address_index].tag == ValueTag::None) {
    error = "sendto(): AF_INET address must be tuple, not NoneType";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int flags = 0;
  if (argc == 4) { if (args[2].tag != ValueTag::Int64) { error = "socket.sendto() flags must be int"; runtime.raise_class_error("TypeError", error); return false; } flags = static_cast<int>(args[2].as.i64); }
  std::string host; int64_t port = 0;
  if (!value_to_host_port(args[address_index], host, port, error)) { runtime.raise_class_error("TypeError", error); return false; }
  SocketAddress address;
  if (!fill_socket_address(state->family, host, port, address, error)) return false;
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) return false;
  const int sent = ::sendto(fd, data.data(), static_cast<int>(data.size()), flags,
                            reinterpret_cast<sockaddr*>(&address.storage), address.length);
  if (sent < 0) { error = socket_last_error_text("sendto"); runtime.raise_class_error("OSError", error); return false; }
  value_set_int64(out, sent);
  return true;
}

bool socket_getsockopt(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if ((argc != 3 && argc != 4) || args[1].tag != ValueTag::Int64 ||
      args[2].tag != ValueTag::Int64 || (argc == 4 && args[3].tag != ValueTag::Int64)) {
    error = "socket.getsockopt() expected level, optname, and optional buffer length";
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
  const int level = static_cast<int>(args[1].as.i64);
  const int option = static_cast<int>(args[2].as.i64);
  if (argc == 3 || args[3].as.i64 == 0) {
    int value = 0;
#ifdef _WIN32
    int length = sizeof(value);
#else
    socklen_t length = sizeof(value);
#endif
    if (::getsockopt(fd, level, option, reinterpret_cast<char*>(&value), &length) != 0) {
      return raise_socket_os_error(runtime, "getsockopt", error);
    }
    value_set_int64(out, value);
    return true;
  }
  if (args[3].as.i64 < 0 || args[3].as.i64 > 1024) {
    error = "getsockopt buflen out of range";
    return false;
  }
  std::string buffer(static_cast<size_t>(args[3].as.i64), '\0');
#ifdef _WIN32
  int length = static_cast<int>(buffer.size());
#else
  socklen_t length = static_cast<socklen_t>(buffer.size());
#endif
  if (::getsockopt(fd, level, option, buffer.data(), &length) != 0) {
    return raise_socket_os_error(runtime, "getsockopt", error);
  }
  buffer.resize(static_cast<size_t>(length));
  out = Value::bytes(std::move(buffer));
  return true;
}

bool socket_recvfrom(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if ((argc != 2 && argc != 3) || args[1].tag != ValueTag::Int64 ||
      (argc == 3 && args[2].tag != ValueTag::Int64)) {
    error = "socket.recvfrom() expected size and optional flags";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket || !prepare_socket_read(runtime, *state, fd, "recvfrom", error)) {
    return false;
  }
  const int size = static_cast<int>(std::max<int64_t>(0, args[1].as.i64));
  const int flags = argc == 3 ? static_cast<int>(args[2].as.i64) : 0;
  std::string data(static_cast<size_t>(size), '\0');
  SocketAddress peer;
  peer.length = sizeof(peer.storage);
  const int received = ::recvfrom(
      fd,
      data.data(),
      size,
      flags,
      reinterpret_cast<sockaddr*>(&peer.storage),
      &peer.length);
  if (received < 0) {
    if (socket_last_error_would_block()) {
      runtime.raise_class_error("BlockingIOError", socket_last_error_text("recvfrom"));
      return false;
    }
    return raise_socket_os_error(runtime, "recvfrom", error);
  }
  data.resize(static_cast<size_t>(received));
  Value peer_address = socket_address_value(reinterpret_cast<const sockaddr*>(&peer.storage), peer.length);
  out = Value::tuple({
      Value::bytes(std::move(data)),
      peer_address,
  });
  return true;
}

bool socket_recv(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if ((argc != 2 && argc != 3) || args[1].tag != ValueTag::Int64 ||
      (argc == 3 && args[2].tag != ValueTag::Int64)) {
    error = "socket.recv() expected size and optional flags";
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
  if (!prepare_socket_read(runtime, *state, fd, "recv", error)) {
    return false;
  }
  const int size = static_cast<int>(std::max<int64_t>(0, args[1].as.i64));
  const int flags = argc == 3 ? static_cast<int>(args[2].as.i64) : 0;
  std::string data(static_cast<size_t>(size), '\0');
  const int received = ::recv(fd, data.data(), size, flags);
  if (received < 0) {
    if (socket_last_error_would_block()) {
      runtime.raise_class_error("BlockingIOError", socket_last_error_text("recv"));
      return false;
    }
    return raise_socket_os_error(runtime, "recv", error);
  }
  data.resize(static_cast<size_t>(received));
  out = Value::bytes(std::move(data));
  return true;
}

bool socket_recv_into(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "socket.recv_into() expected buffer, optional nbytes, and optional flags";
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
  if (!prepare_socket_read(runtime, *state, fd, "recv_into", error)) {
    return false;
  }

  char* data = nullptr;
  size_t capacity = 0;
  if (auto* array = value_as_bytearray(args[1])) {
    data = array->value.data();
    capacity = array->value.size();
  } else if (auto* view = value_as_memoryview(args[1])) {
    if (view->released) {
      error = "operation forbidden on released memoryview object";
      return false;
    }
    if (view->readonly) {
      error = "recv_into() argument must be read-write bytes-like object";
      return false;
    }
    data = memoryview_object_writable_data(*view);
    if (data == nullptr) {
      error = "recv_into() memoryview owner is not writable";
      return false;
    }
    capacity = view->size;
  } else {
    error = "recv_into() argument must be read-write bytes-like object";
    return false;
  }

  if (argc >= 3) {
    if (args[2].tag != ValueTag::Int64) {
      error = "recv_into() nbytes must be int";
      return false;
    }
    if (args[2].as.i64 >= 0) {
      capacity = std::min(capacity, static_cast<size_t>(args[2].as.i64));
    }
  }
  int flags = 0;
  if (argc == 4) {
    if (args[3].tag != ValueTag::Int64) {
      error = "recv_into() flags must be int";
      return false;
    }
    flags = static_cast<int>(args[3].as.i64);
  }
  if (capacity == 0) {
    value_set_int64(out, 0);
    return true;
  }

  const int received = ::recv(fd, data, static_cast<int>(std::min<size_t>(capacity, 65536)), flags);
  if (received < 0) {
    if (socket_last_error_would_block()) {
      runtime.raise_class_error("BlockingIOError", socket_last_error_text("recv_into"));
      return false;
    }
    return raise_socket_os_error(runtime, "recv_into", error);
  }
  value_set_int64(out, received);
  return true;
}

bool socket_recvfrom_into(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "socket.recvfrom_into() expected buffer, optional nbytes, and optional flags";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) return false;
  char* data = nullptr;
  size_t capacity = 0;
  if (auto* array = value_as_bytearray(args[1])) {
    data = array->value.data();
    capacity = array->value.size();
  } else if (auto* view = value_as_memoryview(args[1])) {
    data = memoryview_object_writable_data(*view);
    capacity = view->size;
  }
  if (data == nullptr && capacity != 0) {
    error = "recvfrom_into() argument must be a writable bytes-like object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int flags = 0;
  if (argc >= 3) {
    int64_t requested = 0;
    if (!socket_int_arg(args[2], requested) || requested < 0) {
      error = "recvfrom_into() nbytes must be a non-negative integer";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (requested != 0) capacity = std::min(capacity, static_cast<size_t>(requested));
  }
  if (argc == 4) {
    int64_t requested_flags = 0;
    if (!socket_int_arg(args[3], requested_flags)) {
      error = "recvfrom_into() flags must be an integer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    flags = static_cast<int>(requested_flags);
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket || !prepare_socket_read(runtime, *state, fd, "recvfrom_into", error)) return false;
  SocketAddress peer;
  peer.length = sizeof(peer.storage);
  const int received = ::recvfrom(
      fd,
      data,
      static_cast<int>(std::min<size_t>(capacity, 65536)),
      flags,
      reinterpret_cast<sockaddr*>(&peer.storage),
      &peer.length);
  if (received < 0) {
    error = socket_last_error_text("recvfrom_into");
    runtime.raise_class_error(socket_last_error_would_block() ? "BlockingIOError" : "OSError", error);
    return false;
  }
  Value peer_address = socket_address_value(reinterpret_cast<const sockaddr*>(&peer.storage), peer.length);
  out = Value::tuple({
      Value::int64(received),
      peer_address,
  });
  return true;
}

bool socket_shutdown(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "socket.shutdown() expected how";
    return false;
  }
  auto* state = socket_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) return false;
  if (::shutdown(fd, static_cast<int>(args[1].as.i64)) != 0) {
    return raise_socket_os_error(runtime, "shutdown", error);
  }
  value_set_none(out);
  return true;
}

bool socket_ioctl(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 3) {
    error = "socket.ioctl() expected control code and option";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = socket_state(args[0], error);
  int64_t command = 0;
  if (state == nullptr || !socket_int_arg(args[1], command)) {
    error = "socket.ioctl() control code must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#ifdef _WIN32
  if (command == -1) {
    error = "invalid ioctl command";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  NativeSocket fd = make_native_socket(*state, error);
  if (fd == kInvalidSocket) {
    runtime.raise_class_error("OSError", error);
    return false;
  }
  DWORD bytes_returned = 0;
  if (command == static_cast<int64_t>(SIO_KEEPALIVE_VALS)) {
    auto* values = value_as_tuple(args[2]);
    if (values == nullptr || values->items.size() != 3) {
      error = "SIO_KEEPALIVE_VALS option must be a 3-tuple";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    int64_t onoff = 0;
    int64_t keepalive_time = 0;
    int64_t keepalive_interval = 0;
    if (!socket_int_arg(values->items[0], onoff) ||
        !socket_int_arg(values->items[1], keepalive_time) ||
        !socket_int_arg(values->items[2], keepalive_interval)) {
      error = "SIO_KEEPALIVE_VALS option values must be integers";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    tcp_keepalive option{
        static_cast<ULONG>(onoff),
        static_cast<ULONG>(keepalive_time),
        static_cast<ULONG>(keepalive_interval)};
    if (WSAIoctl(
            fd,
            static_cast<DWORD>(command),
            &option,
            sizeof(option),
            nullptr,
            0,
            &bytes_returned,
            nullptr,
            nullptr) == SOCKET_ERROR) {
      return raise_socket_os_error(runtime, "ioctl", error);
    }
    value_set_none(out);
    return true;
  }
  int64_t input_value = 0;
  if (!socket_int_arg(args[2], input_value)) {
    error = "socket.ioctl() option must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  DWORD input = static_cast<DWORD>(input_value);
  if (WSAIoctl(
          fd,
          static_cast<DWORD>(command),
          &input,
          sizeof(input),
          nullptr,
          0,
          &bytes_returned,
          nullptr,
          nullptr) == SOCKET_ERROR) {
    return raise_socket_os_error(runtime, "ioctl", error);
  }
  value_set_none(out);
  return true;
#else
  error = "socket.ioctl() is only available on Windows";
  runtime.raise_class_error("OSError", error);
  return false;
#endif
}

bool socket_unsupported(Runtime&, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "socket operation is not supported";
  return false;
}

bool socket_gethostname(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 0) {
    error = "socket.gethostname() expected no arguments";
    return false;
  }
  if (!ensure_socket_runtime(error)) {
    runtime.raise_class_error("OSError", error);
    return false;
  }
  char name[NI_MAXHOST] = {};
  if (::gethostname(name, static_cast<int>(sizeof(name))) != 0) {
    return raise_socket_os_error(runtime, "gethostname", error);
  }
  name[sizeof(name) - 1] = '\0';
  out = Value::string(name);
  return true;
}

bool socket_gethostbyname(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.gethostbyname() expected host";
    return false;
  }
  auto* host_string = value_as_string(args[0]);
  if (host_string == nullptr) {
    error = "gethostbyname() argument must be str";
    return false;
  }
  std::string startup_error;
  if (!ensure_socket_runtime(startup_error)) {
    error = startup_error;
    return false;
  }
  const std::string host = string_object_to_string(*host_string);
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &results);
  if (rc != 0 || results == nullptr) {
    return raise_socket_code_error(runtime, "gethostbyname", rc, error);
  }
  char numeric_host[INET_ADDRSTRLEN] = {};
  bool found = false;
  for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
    if (item->ai_family == AF_INET && item->ai_addr != nullptr) {
      auto* address = reinterpret_cast<sockaddr_in*>(item->ai_addr);
      found = inet_ntop(AF_INET, &address->sin_addr, numeric_host, sizeof(numeric_host)) != nullptr;
      if (found) {
        break;
      }
    }
  }
  freeaddrinfo(results);
  if (!found) {
    error = "gethostbyname failed";
    return false;
  }
  out = Value::string(numeric_host);
  return true;
}

bool socket_gethostbyname_ex(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "gethostbyname_ex() argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string startup_error;
  if (!ensure_socket_runtime(startup_error)) {
    error = startup_error;
    return false;
  }
  const std::string host = string_object_to_string(*value_as_string(args[0]));
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_CANONNAME;
  addrinfo* results = nullptr;
  const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &results);
  if (rc != 0 || results == nullptr) return raise_socket_code_error(runtime, "gethostbyname_ex", rc, error);
  std::string canonical_name;
  std::vector<Value> addresses;
  for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
    if (canonical_name.empty() && item->ai_canonname != nullptr) canonical_name = item->ai_canonname;
    if (item->ai_family != AF_INET || item->ai_addr == nullptr) continue;
    char numeric_host[INET_ADDRSTRLEN] = {};
    auto* address = reinterpret_cast<sockaddr_in*>(item->ai_addr);
    if (inet_ntop(AF_INET, &address->sin_addr, numeric_host, sizeof(numeric_host)) == nullptr) continue;
    bool seen = false;
    for (const Value& existing : addresses) {
      const auto* text = value_as_string(existing);
      if (text != nullptr && string_object_view(*text) == numeric_host) { seen = true; break; }
    }
    if (!seen) addresses.push_back(Value::string(numeric_host));
  }
  freeaddrinfo(results);
  if (addresses.empty()) {
    error = "gethostbyname_ex failed";
    return false;
  }
  if (canonical_name.empty()) canonical_name = host;
  out = Value::tuple({Value::string(std::move(canonical_name)), Value::list({}), Value::list(std::move(addresses))});
  return true;
}

bool socket_gethostbyaddr(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "socket.gethostbyaddr() expected host";
    return false;
  }
  auto* host_string = value_as_string(args[0]);
  if (host_string == nullptr) {
    error = "gethostbyaddr() argument must be str";
    return false;
  }
  std::string startup_error;
  if (!ensure_socket_runtime(startup_error)) {
    error = startup_error;
    return false;
  }

  const std::string host = string_object_to_string(*host_string);
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &results);
  if (rc != 0 || results == nullptr) {
    return raise_socket_code_error(runtime, "gethostbyaddr", rc, error);
  }

  std::string canonical_name;
  std::vector<Value> addresses;
  for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
    if (item->ai_family != AF_INET || item->ai_addr == nullptr) continue;
    auto* address = reinterpret_cast<sockaddr_in*>(item->ai_addr);
    char numeric_host[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &address->sin_addr, numeric_host, sizeof(numeric_host)) != nullptr) {
      bool seen = false;
      for (const Value& existing : addresses) {
        auto* text = value_as_string(existing);
        if (text != nullptr && string_object_view(*text) == numeric_host) {
          seen = true;
          break;
        }
      }
      if (!seen) addresses.push_back(Value::string(numeric_host));
    }
    if (canonical_name.empty()) {
      char resolved[NI_MAXHOST] = {};
      if (::getnameinfo(
              item->ai_addr,
              static_cast<socklen_t>(item->ai_addrlen),
              resolved,
              sizeof(resolved),
              nullptr,
              0,
              0) == 0) {
        canonical_name = resolved;
      }
    }
  }
  freeaddrinfo(results);
  if (addresses.empty()) {
    error = "gethostbyaddr failed";
    return false;
  }
  if (canonical_name.empty()) canonical_name = host;
  out = Value::tuple({
      Value::string(std::move(canonical_name)),
      Value::list({}),
      Value::list(std::move(addresses)),
  });
  return true;
}

bool socket_byte_order(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    void* user_data) {
  if (argc != 1) {
    error = "byte-order function expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t value = 0;
  if (!socket_int_arg(args[0], value)) {
    if (value_as_bigint(args[0]) != nullptr) {
      bool negative = false;
      const uint32_t* limbs = nullptr;
      uint32_t count = 0;
      value_bigint_limb_view(args[0], negative, limbs, count);
      error = negative
          ? "can't convert negative Python int to unsigned"
          : "int larger than the platform field";
      runtime.raise_class_error(negative ? "ValueError" : "OverflowError", error);
      return false;
    }
    error = "an integer is required";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const intptr_t operation = reinterpret_cast<intptr_t>(user_data);
  const uint64_t maximum = operation < 2 ? UINT32_MAX : UINT16_MAX;
  if (value < 0) {
    error = "can't convert negative Python int to unsigned";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (static_cast<uint64_t>(value) > maximum) {
    error = "int larger than the platform field";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  switch (operation) {
    case 0: value_set_int64(out, static_cast<int64_t>(htonl(static_cast<u_long>(value)))); break;
    case 1: value_set_int64(out, static_cast<int64_t>(ntohl(static_cast<u_long>(value)))); break;
    case 2: value_set_int64(out, static_cast<int64_t>(htons(static_cast<u_short>(value)))); break;
    default: value_set_int64(out, static_cast<int64_t>(ntohs(static_cast<u_short>(value)))); break;
  }
  return true;
}

bool socket_inet_aton(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "inet_aton() argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string text = string_object_to_string(*value_as_string(args[0]));
  in_addr address{};
  if (inet_pton(AF_INET, text.c_str(), &address) != 1) {
    error = "illegal IP address string passed to inet_aton";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  out = Value::bytes(std::string(reinterpret_cast<const char*>(&address), sizeof(address)));
  return true;
}

bool socket_inet_ntoa(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "inet_ntoa() expected packed IP";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string_view packed;
  if (auto* bytes = value_as_bytes(args[0])) {
    packed = bytes_object_view(*bytes);
  } else if (auto* array = value_as_bytearray(args[0])) {
    packed = array->value;
  } else if (auto* view = value_as_memoryview(args[0])) {
    packed = memoryview_object_view(*view);
  } else {
    error = "inet_ntoa() argument must be a 4-byte buffer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (packed.size() != sizeof(in_addr)) {
    error = "packed IP wrong length for inet_ntoa";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  char text[INET_ADDRSTRLEN] = {};
  if (inet_ntop(AF_INET, packed.data(), text, sizeof(text)) == nullptr) {
    error = socket_last_error_text("inet_ntoa");
    runtime.raise_class_error("OSError", error);
    return false;
  }
  out = Value::string(text);
  return true;
}

bool socket_getprotobyname(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "getprotobyname() argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string startup_error;
  if (!ensure_socket_runtime(startup_error)) {
    error = startup_error;
    return false;
  }
  const std::string protocol = string_object_to_string(*value_as_string(args[0]));
  protoent* entry = ::getprotobyname(protocol.c_str());
  if (entry == nullptr) {
    error = "protocol not found";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(entry->p_proto));
  return true;
}

bool socket_getservbyname(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2 || value_as_string(args[0]) == nullptr ||
      (argc == 2 && args[1].tag != ValueTag::None && value_as_string(args[1]) == nullptr)) {
    error = "getservbyname() expected service and optional protocol strings";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!ensure_socket_runtime(error)) {
    runtime.raise_class_error("OSError", error);
    return false;
  }
  const std::string service = string_object_to_string(*value_as_string(args[0]));
  std::string protocol;
  const char* protocol_ptr = nullptr;
  if (argc == 2 && args[1].tag != ValueTag::None) {
    protocol = string_object_to_string(*value_as_string(args[1]));
    protocol_ptr = protocol.c_str();
  }
  servent* entry = ::getservbyname(service.c_str(), protocol_ptr);
  if (entry == nullptr) {
    error = "service/proto not found";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_int64(out, static_cast<int64_t>(ntohs(static_cast<u_short>(entry->s_port))));
  return true;
}

bool socket_getservbyport(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "getservbyport() expected port and optional protocol";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t port = 0;
  if (!socket_int_arg(args[0], port)) {
    error = "getservbyport() port must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (port < 0 || port > 65535) {
    error = "getservbyport(): port must be 0-65535";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  std::string protocol;
  const char* protocol_ptr = nullptr;
  if (argc == 2 && args[1].tag != ValueTag::None) {
    auto* text = value_as_string(args[1]);
    if (text == nullptr) {
      error = "getservbyport() protocol must be a string";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    protocol = string_object_to_string(*text);
    protocol_ptr = protocol.c_str();
  }
  if (!ensure_socket_runtime(error)) {
    runtime.raise_class_error("OSError", error);
    return false;
  }
  servent* entry = ::getservbyport(htons(static_cast<u_short>(port)), protocol_ptr);
  if (entry == nullptr) {
    error = "port/proto not found";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  out = Value::string(entry->s_name == nullptr ? "" : entry->s_name);
  return true;
}

bool socket_getnameinfo(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "getnameinfo() expected sockaddr and flags";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* address = value_as_tuple(args[0]);
  int64_t flags = 0;
  if (address == nullptr || address->items.size() < 2 || value_as_string(address->items[0]) == nullptr || !socket_int_arg(args[1], flags)) {
    error = "getnameinfo(): sockaddr must be a (host, port) tuple";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const bool ipv4 = address->items.size() == 2;
  const bool ipv6 = address->items.size() == 3 || address->items.size() == 4;
  if (!ipv4 && !ipv6) {
    error = "getnameinfo failed for the supplied address family";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  int64_t port = 0;
  if (!socket_int_arg(address->items[1], port)) {
    error = "getnameinfo(): port must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (port < 0 || port > 65535) {
    error = "getnameinfo(): port out of range";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  const std::string host = string_object_to_string(*value_as_string(address->items[0]));
  sockaddr_storage storage{};
  sockaddr* native = nullptr;
  socklen_t native_size = 0;
  if (ipv4) {
    auto* address4 = reinterpret_cast<sockaddr_in*>(&storage);
    address4->sin_family = AF_INET;
    address4->sin_port = htons(static_cast<u_short>(port));
    if (inet_pton(AF_INET, host.c_str(), &address4->sin_addr) != 1) {
      error = "getnameinfo only accepts numeric IP addresses";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    native = reinterpret_cast<sockaddr*>(address4);
    native_size = sizeof(*address4);
  } else {
    int64_t flowinfo = 0;
    int64_t scope_id = 0;
    if (!socket_int_arg(address->items[2], flowinfo) ||
        (address->items.size() == 4 && !socket_int_arg(address->items[3], scope_id))) {
      error = "getnameinfo(): IPv6 flowinfo and scope_id must be integers";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (flowinfo < 0 || flowinfo > 0xFFFFF || scope_id < 0) {
      error = "getnameinfo(): IPv6 flowinfo or scope_id out of range";
      runtime.raise_class_error("OverflowError", error);
      return false;
    }
    auto* address6 = reinterpret_cast<sockaddr_in6*>(&storage);
    address6->sin6_family = AF_INET6;
    address6->sin6_port = htons(static_cast<u_short>(port));
    address6->sin6_flowinfo = static_cast<u_long>(flowinfo);
    address6->sin6_scope_id = static_cast<u_long>(scope_id);
    if (inet_pton(AF_INET6, host.c_str(), &address6->sin6_addr) != 1) {
      error = "getnameinfo only accepts numeric IP addresses";
      runtime.raise_class_error("OSError", error);
      return false;
    }
    native = reinterpret_cast<sockaddr*>(address6);
    native_size = sizeof(*address6);
  }
  char result_host[NI_MAXHOST] = {};
  char result_service[NI_MAXSERV] = {};
  const int result = ::getnameinfo(
      native,
      native_size,
      result_host,
      sizeof(result_host),
      result_service,
      sizeof(result_service),
      static_cast<int>(flags));
  if (result != 0) {
    error = "getnameinfo failed";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  out = Value::tuple({Value::string(result_host), Value::string(result_service)});
  return true;
}

bool socket_inet_pton(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "socket.inet_pton() expected address family and IP string";
    return false;
  }
  int64_t family = 0;
  if (!socket_int_arg(args[0], family) || (family != kAfInet && family != kAfInet6)) {
    error = "inet_pton() supports AF_INET and AF_INET6";
    return false;
  }
  auto* address_string = value_as_string(args[1]);
  if (address_string == nullptr) {
    error = "inet_pton() argument 2 must be str";
    return false;
  }
  const int native_family = family == kAfInet6 ? AF_INET6 : AF_INET;
  std::array<unsigned char, sizeof(in6_addr)> address{};
  const std::string text = string_object_to_string(*address_string);
  if (inet_pton(native_family, text.c_str(), address.data()) != 1) {
    error = "illegal IP address string passed to inet_pton";
    runtime.raise_class_error("OSError", error);
    return false;
  }
  const size_t size = family == kAfInet6 ? sizeof(in6_addr) : sizeof(in_addr);
  out = Value::bytes(std::string(reinterpret_cast<const char*>(address.data()), size));
  return true;
}

bool socket_inet_ntop(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "socket.inet_ntop() expected address family and packed IP";
    return false;
  }
  int64_t family = 0;
  if (!socket_int_arg(args[0], family) || (family != kAfInet && family != kAfInet6)) {
    error = "inet_ntop() supports AF_INET and AF_INET6";
    return false;
  }
  std::string_view view;
  if (auto* packed = value_as_bytes(args[1])) {
    view = bytes_object_view(*packed);
  } else if (auto* packed = value_as_bytearray(args[1])) {
    view = packed->value;
  } else if (auto* packed = value_as_memoryview(args[1])) {
    view = memoryview_object_view(*packed);
  } else {
    error = "inet_ntop() argument 2 must be bytes-like";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const int native_family = family == kAfInet6 ? AF_INET6 : AF_INET;
  const size_t expected_size = family == kAfInet6 ? sizeof(in6_addr) : sizeof(in_addr);
  if (view.size() != expected_size) {
    error = "invalid length of packed IP address string";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  char numeric_host[INET6_ADDRSTRLEN] = {};
  if (inet_ntop(native_family, view.data(), numeric_host, sizeof(numeric_host)) == nullptr) {
    error = socket_last_error_text("inet_ntop");
    return false;
  }
  out = Value::string(numeric_host);
  return true;
}

bool socket_getaddrinfo(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "socket.getaddrinfo() expected host and port";
    return false;
  }

  std::string host_storage;
  const char* host = nullptr;
  if (args[0].tag != ValueTag::None) {
    auto* host_string = value_as_string(args[0]);
    if (host_string == nullptr) {
      error = "getaddrinfo() host must be string or None";
      return false;
    }
    host_storage = string_object_to_string(*host_string);
    if (contains_utf8_surrogate(host_storage)) {
      error = "host name contains a surrogate character";
      runtime.raise_class_error("UnicodeEncodeError", error);
      return false;
    }
    host = host_storage.c_str();
  }

  std::string service_storage;
  const char* service = nullptr;
  if (args[1].tag != ValueTag::None) {
    if (args[1].tag == ValueTag::Int64) {
      service_storage = std::to_string(args[1].as.i64);
    } else if (auto* service_string = value_as_string(args[1])) {
      service_storage = string_object_to_string(*service_string);
      if (contains_utf8_surrogate(service_storage)) {
        error = "service name contains a surrogate character";
        runtime.raise_class_error("UnicodeEncodeError", error);
        return false;
      }
    } else {
      error = "getaddrinfo() port must be integer, string, or None";
      return false;
    }
    service = service_storage.c_str();
  }

  int64_t family = kAfUnspec;
  int64_t type = 0;
  int64_t proto = 0;
  int64_t flags = 0;
  if (argc >= 3 && !socket_int_arg(args[2], family)) {
    error = "getaddrinfo() family must be integer";
    return false;
  }
  if (argc >= 4 && !socket_int_arg(args[3], type)) {
    error = "getaddrinfo() type must be integer";
    return false;
  }
  if (argc >= 5 && !socket_int_arg(args[4], proto)) {
    error = "getaddrinfo() proto must be integer";
    return false;
  }
  if (argc >= 6 && !socket_int_arg(args[5], flags)) {
    error = "getaddrinfo() flags must be integer";
    return false;
  }

  std::string startup_error;
  if (!ensure_socket_runtime(startup_error)) {
    error = startup_error;
    return false;
  }

  addrinfo hints{};
  hints.ai_family = family == kAfUnspec ? AF_UNSPEC : (family == kAfInet6 ? AF_INET6 : to_native_family(family));
  hints.ai_socktype = type == 0
      ? (proto == IPPROTO_TCP ? SOCK_STREAM : (proto == IPPROTO_UDP ? SOCK_DGRAM : 0))
      : to_native_type(type);
  hints.ai_protocol = static_cast<int>(proto);
  hints.ai_flags = static_cast<int>(flags);

  addrinfo* results = nullptr;
  const int rc = ::getaddrinfo(host, service, &hints, &results);
  if (rc != 0) {
    return raise_socket_code_error(runtime, "getaddrinfo", rc, error);
  }

  std::vector<Value> rows;
  for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
    if ((item->ai_family != AF_INET && item->ai_family != AF_INET6) || item->ai_addr == nullptr) {
      continue;
    }
    const bool ipv6 = item->ai_family == AF_INET6;
    char numeric_host[INET6_ADDRSTRLEN] = {};
    int64_t port = 0;
    Value sockaddr;
    if (ipv6) {
      auto* address = reinterpret_cast<sockaddr_in6*>(item->ai_addr);
      if (inet_ntop(AF_INET6, &address->sin6_addr, numeric_host, sizeof(numeric_host)) == nullptr) continue;
      port = ntohs(address->sin6_port);
      sockaddr = Value::tuple({Value::string(numeric_host), Value::int64(port),
                               Value::int64(address->sin6_flowinfo), Value::int64(address->sin6_scope_id)});
    } else {
      auto* address = reinterpret_cast<sockaddr_in*>(item->ai_addr);
      if (inet_ntop(AF_INET, &address->sin_addr, numeric_host, sizeof(numeric_host)) == nullptr) continue;
      port = ntohs(address->sin_port);
      sockaddr = Value::tuple({Value::string(numeric_host), Value::int64(port)});
    }

    const int64_t result_type = item->ai_socktype == SOCK_DGRAM ? kSockDgram : kSockStream;
    const int64_t result_proto = static_cast<int64_t>(item->ai_protocol);
    const char* canonname = item->ai_canonname == nullptr ? "" : item->ai_canonname;
    rows.push_back(Value::tuple({
        Value::int64(ipv6 ? kAfInet6 : kAfInet),
        Value::int64(result_type),
        Value::int64(result_proto),
        Value::string(canonname),
        std::move(sockaddr),
    }));
  }
  freeaddrinfo(results);

  out = Value::list(std::move(rows));
  return true;
}

Value make_socket_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_socket")});
  attrs.push_back({"__init__", runtime.make_native_function("_socket.socket.__init__", socket_init)});
  attrs.push_back({"__repr__", runtime.make_native_function("_socket.socket.__repr__", socket_repr)});
  attrs.push_back({"close", runtime.make_native_function("_socket.socket.close", socket_close)});
  attrs.push_back({"fileno", runtime.make_native_function("_socket.socket.fileno", socket_fileno)});
  attrs.push_back({"__reduce_ex__", runtime.make_native_function("_socket.socket.__reduce_ex__", socket_reduce_ex)});
  attrs.push_back({"get_inheritable", runtime.make_native_function("_socket.socket.get_inheritable", socket_get_inheritable)});
  attrs.push_back({"set_inheritable", runtime.make_native_function("_socket.socket.set_inheritable", socket_set_inheritable)});
  attrs.push_back({"detach", runtime.make_native_function("_socket.socket.detach", socket_detach)});
  attrs.push_back({"dup", runtime.make_native_function("_socket.socket.dup", socket_dup)});
  attrs.push_back({"settimeout", runtime.make_native_function("_socket.socket.settimeout", socket_settimeout)});
  attrs.push_back({"setblocking", runtime.make_native_function("_socket.socket.setblocking", socket_setblocking)});
  attrs.push_back({"gettimeout", runtime.make_native_function("_socket.socket.gettimeout", socket_gettimeout)});
  attrs.push_back({"getblocking", runtime.make_native_function("_socket.socket.getblocking", socket_getblocking)});
  attrs.push_back({"setsockopt", runtime.make_native_function("_socket.socket.setsockopt", socket_setsockopt)});
  attrs.push_back({"getsockopt", runtime.make_native_function("_socket.socket.getsockopt", socket_getsockopt)});
  attrs.push_back({"bind", runtime.make_native_function("_socket.socket.bind", socket_bind)});
  attrs.push_back({"listen", runtime.make_native_function("_socket.socket.listen", socket_listen)});
  attrs.push_back({"getsockname", runtime.make_native_function("_socket.socket.getsockname", socket_getsockname)});
  attrs.push_back({"getpeername", runtime.make_native_function("_socket.socket.getpeername", socket_getpeername)});
  attrs.push_back({"_accept", runtime.make_native_function("_socket.socket._accept", socket_accept_fd)});
  attrs.push_back({"accept", runtime.make_native_function("_socket.socket.accept", socket_accept)});
  attrs.push_back({"connect", runtime.make_native_function("_socket.socket.connect", socket_connect)});
  attrs.push_back({"connect_ex", runtime.make_native_function("_socket.socket.connect_ex", socket_connect_ex)});
  attrs.push_back({"send", runtime.make_native_function("_socket.socket.send", socket_send)});
  attrs.push_back({"sendall", runtime.make_native_function("_socket.socket.sendall", socket_sendall)});
  attrs.push_back({"sendto", runtime.make_native_function("_socket.socket.sendto", socket_sendto)});
  attrs.push_back({"recv", runtime.make_native_function("_socket.socket.recv", socket_recv)});
  attrs.push_back({"recvfrom", runtime.make_native_function("_socket.socket.recvfrom", socket_recvfrom)});
  attrs.push_back({"recv_into", runtime.make_native_function("_socket.socket.recv_into", socket_recv_into)});
  attrs.push_back({"recvfrom_into", runtime.make_native_function("_socket.socket.recvfrom_into", socket_recvfrom_into)});
  attrs.push_back({"shutdown", runtime.make_native_function("_socket.socket.shutdown", socket_shutdown)});
  attrs.push_back({"ioctl", runtime.make_native_function("_socket.socket.ioctl", socket_ioctl)});
  return Value::class_object("socket", std::move(attrs));
}

void add_socket_exports(Runtime& runtime, NativeModuleBuilder& builder, const Value& socket_class) {
  Value socket_error = Value::string("socket.error");
  if (auto* os_error = runtime.find_builtin("OSError")) {
    value_assign_fast(socket_error, *os_error);
  }
  Value socket_timeout = socket_error;
  if (auto* timeout_error = runtime.find_builtin("TimeoutError")) {
    value_assign_fast(socket_timeout, *timeout_error);
  }
  builder.value("AF_UNSPEC", Value::int64(kAfUnspec))
      .value("AF_INET", Value::int64(kAfInet))
      .value("AF_INET6", Value::int64(kAfInet6))
      .value("has_ipv6", Value::boolean(true))
      .value("SOCK_STREAM", Value::int64(kSockStream))
      .value("SOCK_DGRAM", Value::int64(kSockDgram))
      .value("SOCK_RAW", Value::int64(SOCK_RAW))
      .value("SOCK_RDM", Value::int64(SOCK_RDM))
      .value("SOCK_SEQPACKET", Value::int64(SOCK_SEQPACKET))
#ifdef MSG_PEEK
      .value("MSG_PEEK", Value::int64(MSG_PEEK))
#endif
      .value("IPPROTO_TCP", Value::int64(IPPROTO_TCP))
      .value("IPPROTO_UDP", Value::int64(IPPROTO_UDP))
      .value("IPPROTO_IPV6", Value::int64(IPPROTO_IPV6))
      .value("IPPROTO_ICLFXBM", Value::int64(78))
      .value("IPPROTO_ST", Value::int64(5))
      .value("IPPROTO_CBT", Value::int64(7))
      .value("IPPROTO_IGP", Value::int64(9))
      .value("IPPROTO_RDP", Value::int64(27))
      .value("IPPROTO_PGM", Value::int64(113))
      .value("IPPROTO_L2TP", Value::int64(115))
      .value("IPPROTO_SCTP", Value::int64(132))
      .value("SOL_TCP", Value::int64(IPPROTO_TCP))
#ifdef TCP_NODELAY
      .value("TCP_NODELAY", Value::int64(TCP_NODELAY))
#else
      .value("TCP_NODELAY", Value::int64(1))
#endif
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
      .value("SOL_SOCKET", Value::int64(SOL_SOCKET))
      .value("SO_REUSEADDR", Value::int64(SO_REUSEADDR))
      .value("SO_ERROR", Value::int64(SO_ERROR))
      .value("SO_TYPE", Value::int64(SO_TYPE))
      .value("SO_EXCLUSIVEADDRUSE", Value::int64(-5))
      .value("SOMAXCONN", Value::int64(128))
      .value("SHUT_RD", Value::int64(0))
      .value("SHUT_WR", Value::int64(1))
      .value("SHUT_RDWR", Value::int64(2))
      .value("NI_NUMERICHOST", Value::int64(NI_NUMERICHOST))
      .value("NI_NUMERICSERV", Value::int64(NI_NUMERICSERV))
      .value("NI_NOFQDN", Value::int64(NI_NOFQDN))
      .value("NI_NAMEREQD", Value::int64(NI_NAMEREQD))
      .value("NI_DGRAM", Value::int64(NI_DGRAM))
#ifdef _WIN32
      .value("SIO_RCVALL", Value::int64(static_cast<int64_t>(SIO_RCVALL)))
      .value("RCVALL_ON", Value::int64(static_cast<int64_t>(RCVALL_ON)))
      .value("RCVALL_OFF", Value::int64(static_cast<int64_t>(RCVALL_OFF)))
      .value("SIO_KEEPALIVE_VALS", Value::int64(static_cast<int64_t>(SIO_KEEPALIVE_VALS)))
#endif
#ifdef AI_PASSIVE
      .value("AI_PASSIVE", Value::int64(AI_PASSIVE))
#else
      .value("AI_PASSIVE", Value::int64(1))
#endif
#ifdef AI_CANONNAME
      .value("AI_CANONNAME", Value::int64(AI_CANONNAME))
#else
      .value("AI_CANONNAME", Value::int64(2))
#endif
#ifdef AI_NUMERICHOST
      .value("AI_NUMERICHOST", Value::int64(AI_NUMERICHOST))
#else
      .value("AI_NUMERICHOST", Value::int64(4))
#endif
#ifdef AI_NUMERICSERV
      .value("AI_NUMERICSERV", Value::int64(AI_NUMERICSERV))
#else
      .value("AI_NUMERICSERV", Value::int64(8))
#endif
#ifdef AI_ALL
      .value("AI_ALL", Value::int64(AI_ALL))
#else
      .value("AI_ALL", Value::int64(256))
#endif
#ifdef AI_ADDRCONFIG
      .value("AI_ADDRCONFIG", Value::int64(AI_ADDRCONFIG))
#else
      .value("AI_ADDRCONFIG", Value::int64(1024))
#endif
#ifdef AI_V4MAPPED
      .value("AI_V4MAPPED", Value::int64(AI_V4MAPPED))
#else
      .value("AI_V4MAPPED", Value::int64(2048))
#endif
      .value("timeout", socket_timeout)
      .value("error", socket_error)
      .value("herror", socket_error)
      .value("gaierror", socket_error)
      .value("socket", socket_class)
      .value("SocketType", socket_class)
      .function("getdefaulttimeout", socket_getdefaulttimeout)
      .function("setdefaulttimeout", socket_setdefaulttimeout)
      .function("dup", socket_dup_fd)
      .function("close", socket_close_fd)
      .function("gethostname", socket_gethostname)
      .function("gethostbyname", socket_gethostbyname)
      .function("gethostbyname_ex", socket_gethostbyname_ex)
      .function("gethostbyaddr", socket_gethostbyaddr)
      .function("getprotobyname", socket_getprotobyname)
      .function("getservbyname", socket_getservbyname)
      .function("getservbyport", socket_getservbyport)
      .function("getnameinfo", socket_getnameinfo)
      .value("htonl", runtime.make_native_function("_socket.htonl", socket_byte_order, reinterpret_cast<void*>(0)))
      .value("ntohl", runtime.make_native_function("_socket.ntohl", socket_byte_order, reinterpret_cast<void*>(1)))
      .value("htons", runtime.make_native_function("_socket.htons", socket_byte_order, reinterpret_cast<void*>(2)))
      .value("ntohs", runtime.make_native_function("_socket.ntohs", socket_byte_order, reinterpret_cast<void*>(3)))
      .function("inet_aton", socket_inet_aton)
      .function("inet_ntoa", socket_inet_ntoa)
      .function("inet_pton", socket_inet_pton)
      .function("inet_ntop", socket_inet_ntop)
      .function("getaddrinfo", socket_getaddrinfo);
}

bool select_select(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 4) {
    error = "select.select() expected rlist, wlist, xlist, optional timeout";
    return false;
  }

  double timeout = -1.0;
  if (argc == 4 && !timeout_value_seconds(args[3], timeout, error)) {
    return false;
  }

  fd_set read_set;
  fd_set write_set;
  fd_set except_set;
  FD_ZERO(&read_set);
  FD_ZERO(&write_set);
  FD_ZERO(&except_set);

  std::vector<SelectEntry> read_entries;
  std::vector<SelectEntry> write_entries;
  std::vector<SelectEntry> except_entries;
  NativeSocket max_fd = kInvalidSocket;
  if (!collect_select_entries(runtime, args[0], read_entries, read_set, max_fd, error) ||
      !collect_select_entries(runtime, args[1], write_entries, write_set, max_fd, error) ||
      !collect_select_entries(runtime, args[2], except_entries, except_set, max_fd, error)) {
    return false;
  }

  if (read_entries.empty() && write_entries.empty() && except_entries.empty()) {
    if (timeout > 0.0) {
      std::this_thread::sleep_for(std::chrono::duration<double>(timeout));
    }
    out = Value::tuple({Value::list({}), Value::list({}), Value::list({})});
    return true;
  }

  timeval tv{};
  timeval* tv_ptr = nullptr;
  if (timeout >= 0.0) {
    tv.tv_sec = static_cast<long>(timeout);
    tv.tv_usec = static_cast<long>((timeout - static_cast<double>(tv.tv_sec)) * 1000000.0);
    tv_ptr = &tv;
  }

#ifdef _WIN32
  const int ready = ::select(0, &read_set, &write_set, &except_set, tv_ptr);
#else
  const int ready = ::select(static_cast<int>(max_fd) + 1, &read_set, &write_set, &except_set, tv_ptr);
#endif
  if (ready < 0) {
    error = socket_last_error_text("select");
    return false;
  }

  out = Value::tuple({
      select_ready_values(read_entries, read_set),
      select_ready_values(write_entries, write_set),
      select_ready_values(except_entries, except_set),
  });
  return true;
}

} // namespace

void emit_pending_socket_resource_warnings(Runtime& runtime) {
  std::vector<std::string> pending;
  {
    std::lock_guard<std::mutex> lock(g_socket_resource_warning_mutex);
    pending.swap(g_socket_resource_warnings);
  }
  if (pending.empty()) return;
  std::string ignored;
  Value warnings;
  Value warn;
  const Value* warning_class = runtime.find_builtin("ResourceWarning");
  if (warning_class == nullptr ||
      !runtime.import_module("warnings", warnings, ignored) ||
      !module_get_attr(warnings, "warn", warn, ignored)) {
    return;
  }
  for (auto message = pending.rbegin(); message != pending.rend(); ++message) {
    Value warning_args[] = {Value::string(*message), *warning_class};
    Value warning_result;
    (void)runtime_call_callable(runtime, warn, warning_args, 2, warning_result, ignored);
  }
}

void register_socket_modules(Runtime& runtime) {
  value_set_none(g_default_socket_timeout);

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
