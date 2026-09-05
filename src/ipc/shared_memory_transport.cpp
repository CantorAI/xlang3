/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "shared_memory_transport_internal.h"

#include "serialize/block_stream.h"
#include "serialize/xlang_stream.h"
#include "serialize/ipc_value_marshal.h"

#include <limits>
#include <utility>

namespace xlang3::ipc {

std::atomic_bool g_server_started{false};
LrpcDispatch g_dispatch;

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
  return lrpc_shared_memory_request_platform(strip_lrpc_prefix(endpoint), std::move(write_request), std::move(read_response), error);
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
