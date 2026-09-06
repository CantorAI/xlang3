/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "shared_memory_transport_internal.h"

#if defined(_WIN32)

#include "shared_memory_block_stream.h"
#include "shared_memory_backpressure.h"
#include "shared_memory_server_threads.h"
#include "serialize/ipc_value_marshal.h"
#include "serialize/xlang_stream.h"

#define NOMINMAX
#include <windows.h>

#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace xlang3::ipc {

namespace {

struct SharedRegion {
  uint32_t magic;
  uint32_t version;
  uint32_t slot_count;
  uint32_t slot_size;
  uint32_t server_pid;
  uint32_t next_call_id;
  uint64_t next_capacity_ticket;
  SharedCapacityWaiter capacity_waiters[kSharedSlotCount];
  SharedSlot slots[kSharedSlotCount];
};

struct SharedNames {
  std::string mapping;
  std::string mutex;
  std::string server_event;
  std::string slot_event_prefix;
};

struct ServerState {
  HANDLE mapping = nullptr;
  HANDLE mutex = nullptr;
  HANDLE server_event = nullptr;
  SharedRegion* region = nullptr;
  SharedNames names;
  std::vector<HANDLE> slot_events;
};

struct ClientRegion {
  HANDLE mapping = nullptr;
  HANDLE mutex = nullptr;
  HANDLE server_event = nullptr;
  HANDLE slot_event = nullptr;
  SharedRegion* region = nullptr;
  SharedNames names;
};

ServerState g_server_state;
SharedMemoryServerThreads g_server_threads;

struct WinSecurityAttributes {
  SECURITY_ATTRIBUTES sa{};
  SECURITY_DESCRIPTOR sd{};
  bool valid = false;

  explicit WinSecurityAttributes(bool enabled) {
    if (!enabled) {
      return;
    }
    if (!InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION)) {
      return;
    }
    if (!SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE)) {
      return;
    }
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;
    valid = true;
  }

  SECURITY_ATTRIBUTES* get() {
    return valid ? &sa : nullptr;
  }
};

std::string server_namespace() {
  DWORD session_id = 0;
  if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id) && session_id == 0) {
    return "Global\\";
  }
  return "Local\\";
}

bool is_global_namespace(const SharedNames& names) {
  return names.mapping.rfind("Global\\", 0) == 0;
}

SharedNames make_names(const std::string& port, const std::string& ns) {
  const std::string base = ns + "xlang3_lrpc_" + port;
  return SharedNames{base + "_region", base + "_mutex", base + "_server_event", base + "_slot_"};
}

bool wait_mutex(HANDLE mutex, std::string& error) {
  DWORD wait = WaitForSingleObject(mutex, 30000);
  if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
    return true;
  }
  error = "timed out waiting for lrpc shared-memory mutex";
  return false;
}

void reset_slot(SharedSlot& slot) {
  slot.state = SlotFree;
  slot.owner_pid = 0;
  slot.call_id = 0;
  slot.next_slot = kNoSharedSlot;
  slot.total_size = 0;
  slot.payload_size = 0;
}

void initialize_slots(SharedRegion& region) {
  for (uint32_t i = 0; i < region.slot_count; ++i) {
    reset_slot(region.slots[i]);
  }
}

void free_slot_chain(SharedRegion& region, uint32_t first) {
  uint32_t slot_index = first;
  while (slot_index != kNoSharedSlot && slot_index < region.slot_count) {
    SharedSlot& slot = region.slots[slot_index];
    const uint32_t next = slot.next_slot;
    reset_slot(slot);
    slot_index = next;
  }
}

bool process_is_alive(uint32_t pid) {
  if (pid == 0) {
    return false;
  }
  if (pid == GetCurrentProcessId()) {
    return true;
  }
  HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (process == nullptr) {
    const DWORD err = GetLastError();
    return err == ERROR_ACCESS_DENIED;
  }
  DWORD exit_code = 0;
  const bool alive = GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
  CloseHandle(process);
  return alive;
}

void cleanup_abandoned_client_slots(SharedRegion& region) {
  for (uint32_t i = 0; i < region.slot_count; ++i) {
    SharedSlot& slot = region.slots[i];
    if ((slot.state == SlotWriting || slot.state == SlotRequestReady || slot.state == SlotResponseReady) &&
        slot.owner_pid != 0 && !process_is_alive(slot.owner_pid)) {
      free_slot_chain(region, i);
    }
  }
}

SharedSlotRef allocate_free_slot(SharedRegion& region) {
  for (uint32_t i = 0; i < region.slot_count; ++i) {
    if (region.slots[i].state == SlotFree) {
      region.slots[i].state = SlotWriting;
      region.slots[i].next_slot = kNoSharedSlot;
      region.slots[i].payload_size = 0;
      region.slots[i].total_size = 0;
      return SharedSlotRef{i, &region.slots[i]};
    }
  }
  return {};
}

std::vector<SharedSlotRef> collect_slot_chain(SharedRegion& region, uint32_t first) {
  std::vector<SharedSlotRef> slots;
  uint32_t slot_index = first;
  while (slot_index != kNoSharedSlot && slot_index < region.slot_count) {
    SharedSlot& slot = region.slots[slot_index];
    slots.push_back(SharedSlotRef{slot_index, &slot});
    slot_index = slot.next_slot;
  }
  return slots;
}


HANDLE create_slot_event(const SharedNames& names, uint32_t slot_index, SECURITY_ATTRIBUTES* sa) {
  return CreateEventA(sa, TRUE, FALSE, (names.slot_event_prefix + std::to_string(slot_index)).c_str());
}

HANDLE open_slot_event(const SharedNames& names, uint32_t slot_index) {
  return OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, (names.slot_event_prefix + std::to_string(slot_index)).c_str());
}

void close_client_region(ClientRegion& client) {
  if (client.region != nullptr) UnmapViewOfFile(client.region);
  if (client.mapping != nullptr) CloseHandle(client.mapping);
  if (client.mutex != nullptr) CloseHandle(client.mutex);
  if (client.server_event != nullptr) CloseHandle(client.server_event);
  if (client.slot_event != nullptr) CloseHandle(client.slot_event);
}

bool init_server_region(const std::string& port, ServerState& state, std::string& error) {
  state.names = make_names(port, server_namespace());
  WinSecurityAttributes security(is_global_namespace(state.names));
  state.mapping = CreateFileMappingA(
      INVALID_HANDLE_VALUE,
      security.get(),
      PAGE_READWRITE,
      0,
      static_cast<DWORD>(sizeof(SharedRegion)),
      state.names.mapping.c_str());
  if (state.mapping == nullptr) {
    error = "failed to create lrpc shared-memory region";
    return false;
  }
  state.region = static_cast<SharedRegion*>(MapViewOfFile(state.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedRegion)));
  state.mutex = CreateMutexA(security.get(), FALSE, state.names.mutex.c_str());
  state.server_event = CreateEventA(security.get(), FALSE, FALSE, state.names.server_event.c_str());
  if (state.region == nullptr || state.mutex == nullptr || state.server_event == nullptr) {
    error = "failed to create lrpc shared-memory sync objects";
    return false;
  }
  if (!wait_mutex(state.mutex, error)) {
    return false;
  }
  if (state.region->magic == kSharedMagic && process_is_alive(state.region->server_pid)) {
    const auto owner = state.region->server_pid;
    ReleaseMutex(state.mutex);
    error = "lrpc:" + port + " is already owned by server PID " + std::to_string(owner);
    return false;
  }
  std::memset(state.region, 0, sizeof(SharedRegion));
  state.region->magic = kSharedMagic;
  state.region->version = kSharedVersion;
  state.region->slot_count = kSharedSlotCount;
  state.region->slot_size = kSharedSlotSize;
  state.region->server_pid = GetCurrentProcessId();
  state.region->next_call_id = 1;
  initialize_slots(*state.region);
  ReleaseMutex(state.mutex);
  for (uint32_t i = 0; i < kSharedSlotCount; ++i) {
    HANDLE event = create_slot_event(state.names, i, security.get());
    if (event == nullptr) {
      error = "failed to create lrpc shared-memory slot event";
      return false;
    }
    state.slot_events.push_back(event);
  }
  return true;
}

bool open_client_region_with_namespace(const std::string& port, const std::string& ns, ClientRegion& client) {
  client.names = make_names(port, ns);
  client.mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, client.names.mapping.c_str());
  if (client.mapping == nullptr) return false;
  client.region = static_cast<SharedRegion*>(MapViewOfFile(client.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedRegion)));
  client.mutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, client.names.mutex.c_str());
  client.server_event = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, client.names.server_event.c_str());
  if (client.region == nullptr || client.mutex == nullptr || client.server_event == nullptr ||
      client.region->magic != kSharedMagic || client.region->version != kSharedVersion) {
    close_client_region(client);
    client = ClientRegion{};
    return false;
  }
  return true;
}

bool open_client_region(const std::string& port, ClientRegion& client, std::string& error) {
  if (open_client_region_with_namespace(port, "Global\\", client) ||
      open_client_region_with_namespace(port, "Local\\", client)) {
    return true;
  }
  error = "cannot open lrpc shared-memory endpoint lrpc:" + port;
  return false;
}


void process_slot(uint32_t slot_index, std::vector<SharedSlotRef> request_slots) {
  std::string error;
  SharedSlot& call_slot = g_server_state.region->slots[slot_index];
  const uint32_t owner_pid = call_slot.owner_pid;
  const uint32_t call_id = call_slot.call_id;
  SharedMemoryBlockStream request_stream(request_slots, false);
  size_t reused = 0;
  auto allocate_response_slot = [&request_slots, &reused]() -> SharedSlotRef {
    std::string ignored;
    if (!wait_mutex(g_server_state.mutex, ignored)) {
      return {};
    }
    SharedSlotRef ref = reused < request_slots.size()
        ? request_slots[reused++] : allocate_free_slot(*g_server_state.region);
    if (ref.slot) ref.slot->next_slot = kNoSharedSlot;
    ReleaseMutex(g_server_state.mutex);
    return ref;
  };
  SharedMemoryBlockStream response_stream({}, true, allocate_response_slot, true);
  bool dispatched = false;
  try { dispatched = g_dispatch && g_dispatch(request_stream, response_stream, error); }
  catch (const std::exception& exception) { error = exception.what(); }
  catch (...) { error = "lrpc dispatch threw an unknown exception"; }
  if (!wait_mutex(g_server_state.mutex, error)) {
    return;
  }
  SharedSlot& slot = g_server_state.region->slots[slot_index];
  // Unconsumed request blocks were never linked into the response.
  for (size_t i = reused; i < request_slots.size(); ++i) {
    if (request_slots[i].index != slot_index) reset_slot(*request_slots[i].slot);
  }
  if (dispatched && response_stream.has_spill()) {
    dispatched = publish_spilled_message(*g_server_state.region, response_stream,
        slot_index, true, owner_pid, free_slot_chain, allocate_free_slot,
        [] {
          ReleaseMutex(g_server_state.mutex);
          Sleep(1);
          WaitForSingleObject(g_server_state.mutex, INFINITE);
        }, process_is_alive, error);
  }
  if (!dispatched || response_stream.BlockNum() == 0) {
    if (reused && slot.next_slot != kNoSharedSlot)
      free_slot_chain(*g_server_state.region, slot.next_slot);
    response_stream.reset_to_single_slot_for_write(slot_index, slot);
    response_stream.MarshalError(error.empty() ? "lrpc request failed" : error);
  }
  if (!process_is_alive(owner_pid)) {
    free_slot_chain(*g_server_state.region, response_stream.first_slot_index());
    reset_slot(slot);
    ReleaseMutex(g_server_state.mutex);
    return;
  }
  response_stream.commit(owner_pid, call_id, SlotResponseReady, SlotProcessing);
  ReleaseMutex(g_server_state.mutex);
  HANDLE event = open_slot_event(g_server_state.names, slot_index);
  if (event != nullptr) {
    SetEvent(event);
    CloseHandle(event);
  }
}

void server_loop() {
  for (;;) {
    WaitForSingleObject(g_server_state.server_event, INFINITE);
    if (g_server_threads.stopping) return;
    for (;;) {
      if (g_server_threads.stopping) return;
      std::vector<std::pair<uint32_t, std::vector<SharedSlotRef>>> ready;
      std::string error;
      if (!wait_mutex(g_server_state.mutex, error)) {
        return;
      }
      cleanup_abandoned_client_slots(*g_server_state.region);
      for (uint32_t i = 0; i < g_server_state.region->slot_count; ++i) {
        SharedSlot& slot = g_server_state.region->slots[i];
        if (slot.state == SlotRequestReady) {
          slot.state = SlotProcessing;
          std::vector<SharedSlotRef> request_slots = collect_slot_chain(*g_server_state.region, i);
          if (!request_slots.empty()) {
            ready.push_back({i, std::move(request_slots)});
          } else {
            std::string response;
            make_error_response("invalid lrpc shared-memory request chain", response);
            slot.payload_size = 0;
            slot.total_size = static_cast<uint32_t>(response.size());
            slot.next_slot = kNoSharedSlot;
            SharedMemoryBlockStream response_stream({SharedSlotRef{i, &slot}}, true);
            (void)response_stream.append(response.data(), static_cast<serialize::STREAM_SIZE>(response.size()));
            response_stream.commit(slot.owner_pid, slot.call_id, SlotResponseReady, SlotProcessing);
            HANDLE event = open_slot_event(g_server_state.names, i);
            if (event != nullptr) {
              SetEvent(event);
              CloseHandle(event);
            }
          }
        }
      }
      ReleaseMutex(g_server_state.mutex);
      if (ready.empty()) {
        break;
      }
      for (auto& item : ready) {
        g_server_threads.launch([index = item.first, slots = std::move(item.second)]() mutable {
          process_slot(index, std::move(slots));
        });
      }
    }
  }
}

} // namespace

bool lrpc_start_shared_memory_server_platform(const std::string& port, std::string& error) {
  if (!init_server_region(port, g_server_state, error)) {
    for (HANDLE event : g_server_state.slot_events) CloseHandle(event);
    if (g_server_state.region) UnmapViewOfFile(g_server_state.region);
    if (g_server_state.mapping) CloseHandle(g_server_state.mapping);
    if (g_server_state.mutex) CloseHandle(g_server_state.mutex);
    if (g_server_state.server_event) CloseHandle(g_server_state.server_event);
    g_server_state = ServerState{};
    return false;
  }
  g_server_threads.stopping = false;
  g_server_threads.listener = std::thread(server_loop);
  return true;
}

void lrpc_stop_shared_memory_server_platform() {
  if (!g_server_state.region) return;
  g_server_threads.stopping = true;
  SetEvent(g_server_state.server_event);
  g_server_threads.drain();
  std::string ignored;
  if (wait_mutex(g_server_state.mutex, ignored)) {
    g_server_state.region->server_pid = 0;
    g_server_state.region->magic = 0;
    ReleaseMutex(g_server_state.mutex);
  }
  for (HANDLE event : g_server_state.slot_events) { SetEvent(event); CloseHandle(event); }
  UnmapViewOfFile(g_server_state.region);
  CloseHandle(g_server_state.mapping);
  CloseHandle(g_server_state.mutex);
  CloseHandle(g_server_state.server_event);
  g_server_state = ServerState{};
}

void lrpc_wait_forever_platform() {
  for (;;) {
    Sleep(1000);
  }
}

bool lrpc_shared_memory_request_platform(
    const std::string& port,
    LrpcRequestWriter write_request,
    LrpcResponseReader read_response,
    std::string& error) {
  ClientRegion client;
  if (!open_client_region(port, client, error)) {
    return false;
  }
  uint32_t slot_index = UINT32_MAX;
  if (!wait_mutex(client.mutex, error)) {
    close_client_region(client);
    return false;
  }
  cleanup_abandoned_client_slots(*client.region);
  if (!wait_for_message_capacity(*client.region, 1, client.region->server_pid,
          [&client] {
            ReleaseMutex(client.mutex);
            Sleep(1);
            WaitForSingleObject(client.mutex, INFINITE);
            cleanup_abandoned_client_slots(*client.region);
          }, process_is_alive, error)) {
    ReleaseMutex(client.mutex);
    close_client_region(client);
    return false;
  }
  for (uint32_t i = 0; i < client.region->slot_count; ++i) {
    if (client.region->slots[i].state == SlotFree) {
      slot_index = i;
      break;
    }
  }
  if (slot_index == UINT32_MAX) {
    ReleaseMutex(client.mutex);
    close_client_region(client);
    error = "no free lrpc shared-memory slots";
    return false;
  }
  const uint32_t call_id = client.region->next_call_id++;
  SharedSlotRef first{slot_index, &client.region->slots[slot_index]};
  first.slot->state = SlotWriting;
  first.slot->owner_pid = GetCurrentProcessId();
  first.slot->next_slot = kNoSharedSlot;
  SharedMemoryBlockStream request_stream(
      {first},
      true,
      [&client]() -> SharedSlotRef {
        return allocate_free_slot(*client.region);
      }, true);
  if (!write_request(request_stream, error)) {
    free_slot_chain(*client.region, slot_index);
    ReleaseMutex(client.mutex);
    close_client_region(client);
    return false;
  }
  if (!publish_spilled_message(*client.region, request_stream, slot_index, false,
          client.region->server_pid, free_slot_chain, allocate_free_slot,
          [&client] {
            ReleaseMutex(client.mutex);
            Sleep(1);
            WaitForSingleObject(client.mutex, INFINITE);
            cleanup_abandoned_client_slots(*client.region);
          }, process_is_alive, error)) {
    free_slot_chain(*client.region, slot_index);
    ReleaseMutex(client.mutex);
    close_client_region(client);
    return false;
  }
  client.slot_event = open_slot_event(client.names, slot_index);
  if (client.slot_event == nullptr) {
    free_slot_chain(*client.region, slot_index);
    ReleaseMutex(client.mutex);
    close_client_region(client);
    error = "failed to open lrpc shared-memory response event";
    return false;
  }
  ResetEvent(client.slot_event);
  request_stream.commit(GetCurrentProcessId(), call_id, SlotRequestReady, SlotProcessing);
  const uint32_t server_pid = client.region->server_pid;
  ReleaseMutex(client.mutex);
  SetEvent(client.server_event);
  HANDLE server_process = OpenProcess(SYNCHRONIZE, FALSE, server_pid);
  HANDLE waits[2] = {client.slot_event, server_process};
  DWORD wait_result = server_process != nullptr
      ? WaitForMultipleObjects(2, waits, FALSE, 30000)
      : WaitForSingleObject(client.slot_event, 30000);
  if (server_process != nullptr) {
    CloseHandle(server_process);
  }
  if (wait_result == WAIT_OBJECT_0 + 1) {
    std::string cleanup_error;
    if (wait_mutex(client.mutex, cleanup_error)) {
      SharedSlot& dead_server_slot = client.region->slots[slot_index];
      if (dead_server_slot.owner_pid == GetCurrentProcessId() && dead_server_slot.call_id == call_id) {
        free_slot_chain(*client.region, slot_index);
      }
      ReleaseMutex(client.mutex);
    }
    close_client_region(client);
    error = "lrpc shared-memory server process exited during call";
    return false;
  }
  if (wait_result != WAIT_OBJECT_0) {
    std::string cleanup_error;
    if (wait_mutex(client.mutex, cleanup_error)) {
      SharedSlot& timed_out_slot = client.region->slots[slot_index];
      if (timed_out_slot.owner_pid == GetCurrentProcessId() &&
          timed_out_slot.call_id == call_id &&
          timed_out_slot.state != SlotProcessing) {
        free_slot_chain(*client.region, slot_index);
      }
      ReleaseMutex(client.mutex);
    }
    close_client_region(client);
    error = "timed out waiting for lrpc shared-memory response";
    return false;
  }
  if (!wait_mutex(client.mutex, error)) {
    close_client_region(client);
    return false;
  }
  SharedSlot& slot = client.region->slots[slot_index];
  if (slot.state != SlotResponseReady) {
    ReleaseMutex(client.mutex);
    close_client_region(client);
    error = "invalid lrpc shared-memory slot state";
    return false;
  }
  std::vector<SharedSlotRef> response_slots = collect_slot_chain(*client.region, slot_index);
  SharedMemoryBlockStream response_stream(std::move(response_slots), false);
  if (!read_response(response_stream, error)) {
    free_slot_chain(*client.region, slot_index);
    ReleaseMutex(client.mutex);
    close_client_region(client);
    return false;
  }
  free_slot_chain(*client.region, slot_index);
  ReleaseMutex(client.mutex);
  close_client_region(client);
  return true;
}

} // namespace xlang3::ipc

#endif
