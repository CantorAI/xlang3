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
#include "xlang3/sequence.h"

#include "../thread/runtime_lock.h"

#include <algorithm>
#include <deque>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
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
#include <mswsock.h>
#include <ws2tcpip.h>
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
#if defined(_WIN32)
  enum class SocketOperation : uint8_t {
    None,
    Accept,
    Connect,
    Recv,
    RecvInto,
    Send,
    FileRead,
    FileReadInto,
    FileWrite,
  } socket_operation = SocketOperation::None;
  OVERLAPPED native{};
  SOCKET socket = INVALID_SOCKET;
  HANDLE handle = INVALID_HANDLE_VALUE;
  std::vector<char> buffer;
  Value buffer_target;
#endif
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

#if defined(_WIN32)
struct WaitRegistration {
  HANDLE handle = nullptr;
  int64_t port = 0;
  int64_t address = 0;
  uint32_t timeout_ms = INFINITE;
  std::chrono::steady_clock::time_point started;
};
#endif

OverlappedState* overlapped_state(const Value& self, std::string& error);

std::mutex g_iocp_mutex;
std::condition_variable g_iocp_condition;
std::unordered_map<int64_t, IocpState> g_iocp_ports;
std::unordered_map<int64_t, int64_t> g_iocp_handle_ports;
std::unordered_map<int64_t, OverlappedState*> g_overlapped_by_address;
#if defined(_WIN32)
std::unordered_map<int64_t, WaitRegistration> g_wait_registrations;
#endif

int64_t next_overlapped_address() {
  static int64_t next = 0x10000;
  return ++next;
}

#if defined(_WIN32)
bool overlapped_bytes_view(const Value& value, std::string_view& out) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_view(*bytes);
    return true;
  }
  if (auto* bytes = value_as_bytearray(value)) {
    out = bytes->value;
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    out = memoryview_object_view(*view);
    return !view->released;
  }
  return false;
}

bool overlapped_copy_into(const Value& target, const char* data, size_t size) {
  if (auto* bytes = value_as_bytearray(target)) {
    if (size > bytes->value.size()) return false;
    if (size != 0) std::memcpy(bytes->value.data(), data, size);
    return true;
  }
  if (auto* view = value_as_memoryview(target)) {
    char* destination = memoryview_object_writable_data(*view);
    if (destination == nullptr || size > view->size) return false;
    if (size != 0) std::memcpy(destination, data, size);
    return true;
  }
  return false;
}

bool overlapped_socket_address(
    const Value& value,
    sockaddr_storage& storage,
    int& length,
    std::string& error) {
  auto* tuple = value_as_tuple(value);
  if (tuple == nullptr || tuple->items.size() < 2 ||
      value_as_string(tuple->items[0]) == nullptr || tuple->items[1].tag != ValueTag::Int64) {
    error = "socket address must be a (host, port) tuple";
    return false;
  }
  const std::string host(string_object_view(*value_as_string(tuple->items[0])));
  const auto port = static_cast<unsigned short>(tuple->items[1].as.i64);
  std::memset(&storage, 0, sizeof(storage));
  if (host.find(':') != std::string::npos) {
    auto* address = reinterpret_cast<sockaddr_in6*>(&storage);
    address->sin6_family = AF_INET6;
    address->sin6_port = htons(port);
    if (InetPtonA(AF_INET6, host.c_str(), &address->sin6_addr) != 1) {
      error = "invalid IPv6 socket address";
      return false;
    }
    length = sizeof(*address);
    return true;
  }
  auto* address = reinterpret_cast<sockaddr_in*>(&storage);
  address->sin_family = AF_INET;
  address->sin_port = htons(port);
  if (InetPtonA(AF_INET, host.c_str(), &address->sin_addr) != 1) {
    error = "invalid IPv4 socket address";
    return false;
  }
  length = sizeof(*address);
  return true;
}

template <typename Function>
bool overlapped_extension_function(SOCKET socket, GUID guid, Function& function, std::string& error) {
  DWORD bytes = 0;
  function = nullptr;
  if (WSAIoctl(
          socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
          &function, sizeof(function), &bytes, nullptr, nullptr) == SOCKET_ERROR) {
    error = "WSAIoctl failed with WSA error " + std::to_string(WSAGetLastError());
    return false;
  }
  return true;
}

void overlapped_finalize_socket_result(OverlappedState& state, DWORD transferred) {
  state.completion_transferred = static_cast<int64_t>(transferred);
  using Operation = OverlappedState::SocketOperation;
  if (state.socket_operation == Operation::Recv || state.socket_operation == Operation::FileRead) {
    state.result = Value::bytes(std::string(state.buffer.data(), transferred));
  } else if (state.socket_operation == Operation::RecvInto || state.socket_operation == Operation::FileReadInto) {
    if (!overlapped_copy_into(state.buffer_target, state.buffer.data(), transferred)) {
      state.completion_error = WSAEFAULT;
    }
    value_set_invalid(state.buffer_target);
    state.result = Value::int64(static_cast<int64_t>(transferred));
  } else if (state.socket_operation == Operation::Send || state.socket_operation == Operation::FileWrite) {
    state.result = Value::int64(static_cast<int64_t>(transferred));
  } else {
    state.result = Value::int64(0);
  }
  state.completed = true;
  state.pending = false;
  state.completion_delivered = false;
}

bool overlapped_poll_socket(OverlappedState& state) {
  if (!state.pending || state.socket_operation == OverlappedState::SocketOperation::None) {
    return state.completed;
  }
  DWORD transferred = 0;
  BOOL completed = FALSE;
  const bool file_operation = state.socket_operation == OverlappedState::SocketOperation::FileRead ||
      state.socket_operation == OverlappedState::SocketOperation::FileReadInto ||
      state.socket_operation == OverlappedState::SocketOperation::FileWrite;
  if (file_operation) {
    completed = state.handle != INVALID_HANDLE_VALUE &&
        GetOverlappedResult(state.handle, &state.native, &transferred, FALSE);
  } else {
    DWORD flags = 0;
    completed = state.socket != INVALID_SOCKET &&
        WSAGetOverlappedResult(state.socket, &state.native, &transferred, FALSE, &flags);
  }
  if (completed) {
    state.completion_error = 0;
    overlapped_finalize_socket_result(state, transferred);
    return true;
  }
  const int code = file_operation ? static_cast<int>(GetLastError()) : WSAGetLastError();
  if (code == ERROR_IO_INCOMPLETE || code == ERROR_IO_PENDING || code == WSA_IO_INCOMPLETE || code == WSA_IO_PENDING) return false;
  if (file_operation && code == ERROR_BROKEN_PIPE) {
    state.completion_error = 0;
    overlapped_finalize_socket_result(state, 0);
    return true;
  }
  state.completion_error = code;
  overlapped_finalize_socket_result(state, transferred);
  return true;
}
#endif

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
  g_iocp_condition.notify_all();
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
#if defined(_WIN32)
    if (state->pending) {
      HANDLE operation_handle = state->handle != INVALID_HANDLE_VALUE
          ? state->handle : reinterpret_cast<HANDLE>(state->socket);
      if (operation_handle != INVALID_HANDLE_VALUE) (void)CancelIoEx(operation_handle, &state->native);
    }
#endif
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

bool overlapped_getresult(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "Overlapped.getresult() expected optional wait flag";
    return false;
  }
  auto* state = overlapped_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
#if defined(_WIN32)
  (void)overlapped_poll_socket(*state);
#endif
  if (state->completion_error != 0) {
    error = "overlapped operation failed with WSA error " + std::to_string(state->completion_error);
    runtime.raise_class_error("OSError", error);
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
#if defined(_WIN32)
  if (state->pending) {
    HANDLE operation_handle = state->handle != INVALID_HANDLE_VALUE
        ? state->handle : reinterpret_cast<HANDLE>(state->socket);
    if (operation_handle != INVALID_HANDLE_VALUE) (void)CancelIoEx(operation_handle, &state->native);
  }
#endif
  state->pending = false;
  post_iocp_completion(state->port, 0, 0, state->address, 995);
  out = Value::boolean(false);
  return true;
}

#if defined(_WIN32)
bool overlapped_begin_socket_operation(
    Runtime& runtime,
    OverlappedState& state,
    const Value* args,
    uint32_t argc,
    std::string_view method,
    Value& out,
    std::string& error,
    bool& handled) {
  const bool file_operation = method == "ReadFile" || method == "ReadFileInto" || method == "WriteFile";
  handled = method == "AcceptEx" || method == "ConnectEx" || method == "WSARecv" ||
      method == "WSARecvInto" || method == "WSASend" || file_operation;
  if (!handled) return true;
  if (argc < 2 || args[1].tag != ValueTag::Int64) {
    error = std::string(method) + " expected an integer handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  state.native = {};
  state.socket = file_operation ? INVALID_SOCKET : static_cast<SOCKET>(args[1].as.i64);
  state.handle = file_operation ? reinterpret_cast<HANDLE>(args[1].as.i64) : INVALID_HANDLE_VALUE;
  state.buffer.clear();
  value_set_invalid(state.buffer_target);
  state.completed = false;
  state.completion_delivered = false;
  state.completion_error = 0;
  state.completion_transferred = 0;
  state.socket_operation = OverlappedState::SocketOperation::None;

  BOOL immediate = FALSE;
  if (method == "AcceptEx") {
    if (argc != 3 || args[2].tag != ValueTag::Int64) {
      error = "AcceptEx expected listener and accept socket handles";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    LPFN_ACCEPTEX accept_ex = nullptr;
    GUID guid = WSAID_ACCEPTEX;
    if (!overlapped_extension_function(state.socket, guid, accept_ex, error)) {
      runtime.raise_class_error("OSError", error);
      return false;
    }
    constexpr DWORD address_size = sizeof(sockaddr_in6) + 16;
    state.buffer.resize(address_size * 2);
    DWORD transferred = 0;
    state.socket_operation = OverlappedState::SocketOperation::Accept;
    immediate = accept_ex(
        state.socket, static_cast<SOCKET>(args[2].as.i64), state.buffer.data(),
        0, address_size, address_size, &transferred, &state.native);
    if (immediate) overlapped_finalize_socket_result(state, transferred);
  } else if (method == "ConnectEx") {
    if (argc != 3) {
      error = "ConnectEx expected socket handle and address";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    sockaddr_storage address{};
    int address_length = 0;
    if (!overlapped_socket_address(args[2], address, address_length, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    LPFN_CONNECTEX connect_ex = nullptr;
    GUID guid = WSAID_CONNECTEX;
    if (!overlapped_extension_function(state.socket, guid, connect_ex, error)) {
      runtime.raise_class_error("OSError", error);
      return false;
    }
    DWORD transferred = 0;
    state.socket_operation = OverlappedState::SocketOperation::Connect;
    immediate = connect_ex(
        state.socket, reinterpret_cast<const sockaddr*>(&address), address_length,
        nullptr, 0, &transferred, &state.native);
    if (immediate) overlapped_finalize_socket_result(state, transferred);
  } else if (method == "ReadFile" || method == "ReadFileInto") {
    if (argc != 3) {
      error = std::string(method) + " expected a handle and buffer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    size_t size = 0;
    if (method == "ReadFile") {
      if (args[2].tag != ValueTag::Int64 || args[2].as.i64 < 0) {
        error = "ReadFile size must be a non-negative integer";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      size = static_cast<size_t>(args[2].as.i64);
      state.socket_operation = OverlappedState::SocketOperation::FileRead;
    } else {
      if (auto* bytes = value_as_bytearray(args[2])) size = bytes->value.size();
      else if (auto* view = value_as_memoryview(args[2]); view != nullptr && !view->released) size = view->size;
      else {
        error = "ReadFileInto buffer must be writable bytes-like storage";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(state.buffer_target, args[2]);
      state.socket_operation = OverlappedState::SocketOperation::FileReadInto;
    }
    state.buffer.resize(size);
    DWORD transferred = 0;
    immediate = ::ReadFile(
        state.handle, state.buffer.data(), static_cast<DWORD>(state.buffer.size()),
        &transferred, &state.native);
    if (immediate) overlapped_finalize_socket_result(state, transferred);
  } else if (method == "WriteFile") {
    if (argc != 3) {
      error = "WriteFile expected a handle and data";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    std::string_view data;
    if (!overlapped_bytes_view(args[2], data)) {
      error = "WriteFile data must be bytes-like";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state.buffer.assign(data.begin(), data.end());
    state.socket_operation = OverlappedState::SocketOperation::FileWrite;
    DWORD transferred = 0;
    immediate = ::WriteFile(
        state.handle, state.buffer.data(), static_cast<DWORD>(state.buffer.size()),
        &transferred, &state.native);
    if (immediate) overlapped_finalize_socket_result(state, transferred);
  } else if (method == "WSARecv" || method == "WSARecvInto") {
    if (argc < 3) {
      error = std::string(method) + " expected socket handle and buffer";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    size_t size = 0;
    if (method == "WSARecv") {
      if (args[2].tag != ValueTag::Int64 || args[2].as.i64 < 0) {
        error = "WSARecv size must be a non-negative integer";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      size = static_cast<size_t>(args[2].as.i64);
      state.socket_operation = OverlappedState::SocketOperation::Recv;
    } else {
      if (auto* bytes = value_as_bytearray(args[2])) size = bytes->value.size();
      else if (auto* view = value_as_memoryview(args[2])) size = view->size;
      else {
        error = "WSARecvInto buffer must be writable bytes-like storage";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      value_assign_fast(state.buffer_target, args[2]);
      state.socket_operation = OverlappedState::SocketOperation::RecvInto;
    }
    state.buffer.resize(size);
    WSABUF buffer{static_cast<ULONG>(size), state.buffer.data()};
    DWORD transferred = 0;
    DWORD flags = argc >= 4 && args[3].tag == ValueTag::Int64
        ? static_cast<DWORD>(args[3].as.i64) : 0;
    const int status = WSARecv(state.socket, &buffer, 1, &transferred, &flags, &state.native, nullptr);
    immediate = status == 0;
    if (immediate) overlapped_finalize_socket_result(state, transferred);
  } else {
    if (argc < 3) {
      error = "WSASend expected socket handle and data";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    std::string_view data;
    if (!overlapped_bytes_view(args[2], data)) {
      error = "WSASend data must be bytes-like";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    state.buffer.assign(data.begin(), data.end());
    state.socket_operation = OverlappedState::SocketOperation::Send;
    WSABUF buffer{static_cast<ULONG>(state.buffer.size()), state.buffer.data()};
    DWORD transferred = 0;
    DWORD flags = argc >= 4 && args[3].tag == ValueTag::Int64
        ? static_cast<DWORD>(args[3].as.i64) : 0;
    const int status = WSASend(state.socket, &buffer, 1, &transferred, flags, &state.native, nullptr);
    immediate = status == 0;
    if (immediate) overlapped_finalize_socket_result(state, transferred);
  }

  if (!immediate) {
    const int code = file_operation ? static_cast<int>(GetLastError()) : WSAGetLastError();
    if (file_operation && code == ERROR_BROKEN_PIPE) {
      state.completion_error = 0;
      overlapped_finalize_socket_result(state, 0);
    } else if (code != ERROR_IO_PENDING && code != WSA_IO_PENDING) {
      state.pending = false;
      state.completion_error = code;
      error = std::string(method) + " failed with Windows error " + std::to_string(code);
      runtime.raise_class_error("OSError", error);
      return false;
    } else {
      state.pending = true;
    }
  }
  out = Value::boolean(false);
  return true;
}
#endif

bool overlapped_io_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
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
#if defined(_WIN32)
  bool handled = false;
  if (!overlapped_begin_socket_operation(
          runtime, *state, args, argc, method == nullptr ? std::string_view{} : std::string_view(method),
          out, error, handled)) {
    return false;
  }
  if (handled) {
    g_iocp_condition.notify_all();
    return true;
  }
#endif
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

bool create_event(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4) {
    error = "CreateEvent() expected security, manual_reset, initial_state, and name";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const BOOL manual_reset = value_truthy(args[1]) ? TRUE : FALSE;
  const BOOL initial_state = value_truthy(args[2]) ? TRUE : FALSE;
  HANDLE event = CreateEventW(nullptr, manual_reset, initial_state, nullptr);
  if (event == nullptr) {
    error = "CreateEvent failed with Windows error " + std::to_string(GetLastError());
    runtime.raise_class_error("OSError", error);
    return false;
  }
  out = Value::int64(reinterpret_cast<int64_t>(event));
#else
  out = Value::int64(next_overlapped_address());
#endif
  return true;
}

bool set_or_reset_event(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "event operation expected one handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const bool reset = user_data != nullptr;
  const BOOL ok = reset
      ? ResetEvent(reinterpret_cast<HANDLE>(args[0].as.i64))
      : SetEvent(reinterpret_cast<HANDLE>(args[0].as.i64));
  if (!ok) {
    error = "event operation failed with Windows error " + std::to_string(GetLastError());
    runtime.raise_class_error("OSError", error);
    return false;
  }
#endif
  out = Value::int64(1);
  return true;
}

bool reset_event(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return set_or_reset_event(runtime, args, argc, out, error, reinterpret_cast<void*>(1));
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
  int64_t timeout_ms = 0;
  if (!value_to_i64(args[1], timeout_ms) || timeout_ms < 0 || timeout_ms > 0xffffffffLL) {
    error = "GetQueuedCompletionStatus() timeout must be a DWORD";
    return false;
  }
  const bool infinite_timeout = timeout_ms == 0xffffffffLL;
  const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::milliseconds(infinite_timeout ? 0 : timeout_ms);
  std::unique_lock<std::mutex> lock(g_iocp_mutex);
  for (;;) {
  auto it = g_iocp_ports.find(port);
#if defined(_WIN32)
  if (it != g_iocp_ports.end()) {
    const auto now = std::chrono::steady_clock::now();
    for (auto wait = g_wait_registrations.begin(); wait != g_wait_registrations.end();) {
      if (wait->second.port != port) {
        ++wait;
        continue;
      }
      const DWORD status = WaitForSingleObject(wait->second.handle, 0);
      const bool timed_out = wait->second.timeout_ms != INFINITE &&
          std::chrono::duration_cast<std::chrono::milliseconds>(now - wait->second.started).count() >=
              wait->second.timeout_ms;
      if (status == WAIT_OBJECT_0 || timed_out) {
        it->second.completions.push_back(IocpCompletion{0, 0, 0, wait->second.address});
        wait = g_wait_registrations.erase(wait);
      } else {
        ++wait;
      }
    }
  }
#endif
  if (it == g_iocp_ports.end() || it->second.completions.empty()) {
    bool has_pollable_operation = false;
    if (it != g_iocp_ports.end()) {
      for (auto& entry : g_overlapped_by_address) {
        auto* state = entry.second;
        if (state == nullptr || state->port != port || state->completion_delivered) {
          continue;
        }
        has_pollable_operation = true;
#if defined(_WIN32)
        (void)overlapped_poll_socket(*state);
#endif
        if (!state->completed) continue;
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
    if (it == g_iocp_ports.end() || timeout_ms == 0 ||
        (!infinite_timeout && std::chrono::steady_clock::now() >= deadline)) {
      value_set_none(out);
      return true;
    }
#if defined(_WIN32)
    for (const auto& wait : g_wait_registrations) {
      if (wait.second.port == port) {
        has_pollable_operation = true;
        break;
      }
    }
#endif
    // Socket and registered-handle completions are polled by this runtime;
    // queued completions wake the condition variable immediately.
    const auto poll_interval = std::chrono::milliseconds(1);
    const auto remaining = infinite_timeout ? poll_interval :
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
    const auto wait_time = has_pollable_operation ? std::min(poll_interval, remaining) : remaining;
    {
      XlangRuntimeExecutionSuspension suspension;
      if (infinite_timeout && !has_pollable_operation) {
        g_iocp_condition.wait(lock);
      } else if (wait_time.count() > 0) {
        g_iocp_condition.wait_for(lock, wait_time);
      }
      lock.unlock();
    }
    lock.lock();
    continue;
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
    g_iocp_condition.notify_all();
#if defined(_WIN32)
    for (auto wait = g_wait_registrations.begin(); wait != g_wait_registrations.end();) {
      if (wait->second.port == port) wait = g_wait_registrations.erase(wait);
      else ++wait;
    }
#endif
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

bool bind_local(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  int64_t family = 0;
  if (argc != 2 || args[0].tag != ValueTag::Int64 || !value_int_like_to_i64(args[1], family)) {
    error = "BindLocal() expected socket handle and address family";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const SOCKET socket = static_cast<SOCKET>(args[0].as.i64);
  int status = SOCKET_ERROR;
  if (family == AF_INET6) {
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = in6addr_any;
    status = bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
  } else {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    status = bind(socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
  }
  if (status == SOCKET_ERROR) {
    error = "BindLocal failed with WSA error " + std::to_string(WSAGetLastError());
    runtime.raise_class_error("OSError", error);
    return false;
  }
  value_set_none(out);
  return true;
#else
  return overlapped_not_implemented(runtime, "BindLocal", error);
#endif
}

bool register_wait_with_queue(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 4 || args[0].tag != ValueTag::Int64 || args[1].tag != ValueTag::Int64 ||
      args[2].tag != ValueTag::Int64 || args[3].tag != ValueTag::Int64) {
    error = "RegisterWaitWithQueue() expected handle, port, address, and timeout";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  const int64_t registration = next_overlapped_address();
  WaitRegistration wait;
  wait.handle = reinterpret_cast<HANDLE>(args[0].as.i64);
  wait.port = args[1].as.i64;
  wait.address = args[2].as.i64;
  wait.timeout_ms = static_cast<uint32_t>(args[3].as.i64);
  wait.started = std::chrono::steady_clock::now();
  {
    std::lock_guard<std::mutex> lock(g_iocp_mutex);
    g_wait_registrations[registration] = wait;
    g_iocp_condition.notify_all();
  }
  out = Value::int64(registration);
#else
  return overlapped_not_implemented(runtime, "RegisterWaitWithQueue", error);
#endif
  return true;
}

bool unregister_wait_common(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    bool signal_event) {
  if (argc != (signal_event ? 2u : 1u) || args[0].tag != ValueTag::Int64 ||
      (signal_event && args[1].tag != ValueTag::Int64)) {
    error = signal_event ? "UnregisterWaitEx() expected wait and event handles"
                         : "UnregisterWait() expected a wait handle";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
#if defined(_WIN32)
  {
    std::lock_guard<std::mutex> lock(g_iocp_mutex);
    g_wait_registrations.erase(args[0].as.i64);
  }
  if (signal_event) {
    (void)SetEvent(reinterpret_cast<HANDLE>(args[1].as.i64));
  }
#endif
  value_set_none(out);
  return true;
}

bool unregister_wait(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unregister_wait_common(runtime, args, argc, out, error, false);
}

bool unregister_wait_ex(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return unregister_wait_common(runtime, args, argc, out, error, true);
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
  add_function(builder, "ResetEvent", reset_event);
  add_function(builder, "SetEvent", set_or_reset_event);
  add_function(builder, "CreateIoCompletionPort", create_iocp);
  add_function(builder, "GetQueuedCompletionStatus", get_queued_completion_status);
  add_function(builder, "PostQueuedCompletionStatus", post_queued_completion_status);
  add_function(builder, "BindLocal", bind_local);
  add_function(builder, "RegisterWaitWithQueue", register_wait_with_queue);
  add_function(builder, "UnregisterWait", unregister_wait);
  add_function(builder, "UnregisterWaitEx", unregister_wait_ex);
  for (const char* name : {
           "ConnectPipe",
           "FormatMessage",
           "WSAConnect",
       }) {
    add_unimplemented_function(runtime, builder, name);
  }

  runtime.register_module("_overlapped", builder.finish());
}

} // namespace xlang3
