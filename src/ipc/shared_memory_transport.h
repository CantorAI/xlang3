/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace xlang3::serialize {
class XLangStream;
}

namespace xlang3::ipc {

// Dispatch must finish reading the request before its first response write.
// The transport may reuse request blocks for that response.
using LrpcDispatch = std::function<bool(serialize::XLangStream& request, serialize::XLangStream& response, std::string& error)>;
using LrpcRequestWriter = std::function<bool(serialize::XLangStream& request, std::string& error)>;
using LrpcResponseReader = std::function<bool(serialize::XLangStream& response, std::string& error)>;

bool lrpc_listen_shared_memory(int64_t port, bool wait, LrpcDispatch dispatch, std::string& error);
bool lrpc_shared_memory_request(
    const std::string& endpoint,
    LrpcRequestWriter write_request,
    LrpcResponseReader read_response,
    std::string& error);
bool lrpc_shared_memory_request(const std::string& endpoint, const std::string& request, std::string& response, std::string& error);

} // namespace xlang3::ipc
