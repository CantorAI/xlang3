/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "shared_memory_transport_internal.h"

#include "serialize/block_stream.h"
#include "serialize/xlang_stream.h"
#include "serialize/ipc_value_marshal.h"
#include "runtime_lock.h"

#include <limits>
#include <utility>
#include <chrono>
#include <thread>
#include <algorithm>

namespace xlang3::ipc {

std::atomic_bool g_server_started{false};
LrpcDispatch g_dispatch;

uint64_t next_listener_session() {
  static std::atomic<uint64_t> previous{0};
  const auto now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count());
  auto old = previous.load();
  for (;;) {
    const auto next = std::max(now, old + 1);
    if (previous.compare_exchange_weak(old, next)) return next;
  }
}

bool lrpc_probe(const std::string& endpoint, uint32_t timeout_ms,
    LrpcEndpointInfo& info, std::string& error) {
  info = {};
  const auto port = strip_lrpc_prefix(endpoint);
  if (endpoint.rfind("lrpc:", 0) != 0 || port.empty() ||
      port.find_first_not_of("0123456789") != std::string::npos ||
      port.find_first_not_of('0') == std::string::npos || port.size() > 19) {
    error = "probe requires an lrpc endpoint with a positive numeric port";
    return false;
  }
  XlangRuntimeExecutionSuspension suspension;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  for (;;) {
    if (!lrpc_probe_platform(port, info, error)) return false;
    if (info.pid || std::chrono::steady_clock::now() >= deadline) return true;
    std::this_thread::sleep_until(std::min(deadline,
        std::chrono::steady_clock::now() + std::chrono::milliseconds(10)));
  }
}

std::string strip_lrpc_prefix(const std::string& endpoint) {
  constexpr const char prefix[] = "lrpc:";
  return endpoint.rfind(prefix, 0) == 0 ? endpoint.substr(sizeof(prefix) - 1) : endpoint;
}

void make_error_response(const std::string& message, std::string& out) {
  out.clear();
  serialize::BlockStream block;
  serialize::XLangStream stream(&block);
  stream.MarshalError(message);
  out.resize(static_cast<size_t>(stream.Size()));
  (void)stream.FullCopyTo(out.data(), static_cast<serialize::STREAM_SIZE>(out.size()));
}

bool lrpc_shared_memory_request(
    const std::string& endpoint,
    LrpcRequestWriter write_request,
    LrpcResponseReader read_response,
    std::string& error) {
  // Embedding calls may still own the VM lock here. A remote peer can call back
  // before replying, so retain the lock only while accessing runtime values.
  XlangRuntimeExecutionSuspension suspension;
  return lrpc_shared_memory_request_platform(strip_lrpc_prefix(endpoint),
      [&](serialize::XLangStream& stream, std::string& message) {
        XlangRuntimeExecutionGuard guard;
        return write_request(stream, message);
      },
      [&](serialize::XLangStream& stream, std::string& message) {
        XlangRuntimeExecutionGuard guard;
        return read_response(stream, message);
      }, error);
}

bool lrpc_shared_memory_request(const std::string& endpoint, const std::string& request, std::string& response, std::string& error) {
  if (request.size() > std::numeric_limits<uint32_t>::max()) {
    error = "lrpc request exceeds shared-memory region addressable size";
    return false;
  }
  return lrpc_shared_memory_request(
      endpoint,
      [&request](serialize::XLangStream& stream, std::string&) {
        return request.empty() || stream.append(request.data(), static_cast<serialize::STREAM_SIZE>(request.size()));
      },
      [&response](serialize::XLangStream& stream, std::string& read_error) {
        const auto size = stream.Size();
        if (size < 0 || size > std::numeric_limits<uint32_t>::max()) {
          read_error = "lrpc response exceeds addressable size";
          return false;
        }
        response.resize(static_cast<size_t>(size));
        return size == 0 || stream.FullCopyTo(response.data(), size);
      },
      error);
}

bool lrpc_listen_shared_memory(int64_t port, bool wait, LrpcDispatch dispatch, std::string& error) {
  if (port <= 0) {
    error = "lrpc port must be positive";
    return false;
  }
  g_dispatch = std::move(dispatch);
  if (!g_server_started.exchange(true)) {
    if (!lrpc_start_shared_memory_server_platform(std::to_string(port), error)) {
      g_server_started.store(false);
      return false;
    }
  }
  if (wait) {
    lrpc_wait_forever_platform();
  }
  return true;
}

} // namespace xlang3::ipc
