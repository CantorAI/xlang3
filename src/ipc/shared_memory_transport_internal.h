/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "shared_memory_transport.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace xlang3::ipc {

constexpr uint32_t kSharedMagic = 0x33435049u;
constexpr uint32_t kSharedVersion = 2;
constexpr uint32_t kSharedSlotCount = 32;
constexpr uint32_t kSharedSlotSize = 64 * 1024;

enum SlotState : uint32_t {
  SlotFree = 0,
  SlotWriting = 1,
  SlotRequestReady = 2,
  SlotProcessing = 3,
  SlotResponseReady = 4,
};

struct SharedSlot {
  uint32_t state;
  uint32_t owner_pid;
  uint32_t call_id;
  uint32_t next_slot;
  uint32_t total_size;
  uint32_t payload_size;
  char payload[kSharedSlotSize];
};

constexpr uint32_t kNoSharedSlot = UINT32_MAX;

extern std::atomic_bool g_server_started;
extern LrpcDispatch g_dispatch;

std::string strip_lrpc_prefix(const std::string& endpoint);
void make_error_response(const std::string& message, std::string& out);

bool lrpc_shared_memory_request_platform(
    const std::string& port,
    LrpcRequestWriter write_request,
    LrpcResponseReader read_response,
    std::string& error);

bool lrpc_start_shared_memory_server_platform(const std::string& port, std::string& error);
void lrpc_stop_shared_memory_server_platform();
void lrpc_wait_forever_platform();

} // namespace xlang3::ipc
