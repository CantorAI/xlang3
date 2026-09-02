/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "shared_memory_transport_internal.h"

#if !defined(_WIN32)

#include "shared_memory_block_stream.h"
#include "serialize/ipc_value_marshal.h"
#include "serialize/xlang_stream.h"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace xlang3::ipc {

namespace {

struct SharedRegion {
  pthread_mutex_t mutex;
  pthread_cond_t server_cond;
  pthread_cond_t slot_conds[kSharedSlotCount];
  uint32_t magic;
  uint32_t version;
  uint32_t slot_count;
  uint32_t slot_size;
  uint32_t server_pid;
  uint32_t next_call_id;
  SharedSlot slots[kSharedSlotCount];
};

struct PosixMapping {
  int fd = -1;
  SharedRegion* region = nullptr;
  std::string name;
};

PosixMapping g_server_mapping;

std::string posix_name(const std::string& port) {
  return "/xlang3_lrpc_" + port;
}

void close_posix_mapping(PosixMapping& mapping) {
  if (mapping.region != nullptr) {
    munmap(mapping.region, sizeof(SharedRegion));
    mapping.region = nullptr;
  }
  if (mapping.fd >= 0) {
    close(mapping.fd);
    mapping.fd = -1;
  }
}

bool wait_with_timeout(pthread_cond_t& cond, pthread_mutex_t& mutex, int seconds, std::string& error) {
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += seconds;
  const int rc = pthread_cond_timedwait(&cond, &mutex, &ts);
  if (rc == 0) return true;
  error = "timed out waiting for lrpc shared-memory condition";
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
  if (pid == static_cast<uint32_t>(getpid())) {
    return true;
  }
  if (kill(static_cast<pid_t>(pid), 0) == 0) {
    return true;
  }
  return errno == EPERM;
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

void publish_response_chain_header(
    SharedRegion& region,
    uint32_t call_slot,
    uint32_t response_first,
    uint32_t response_size,
    uint32_t owner_pid,
    uint32_t call_id) {
  SharedSlot& slot = region.slots[call_slot];
  slot.owner_pid = owner_pid;
  slot.call_id = call_id;
  slot.next_slot = response_first;
  slot.total_size = response_size;
  slot.payload_size = 0;
  slot.state = SlotResponseReady;
}

bool init_process_shared_sync(SharedRegion* region, std::string& error) {
  pthread_mutexattr_t mutex_attr;
  pthread_condattr_t cond_attr;
  if (pthread_mutexattr_init(&mutex_attr) != 0 || pthread_condattr_init(&cond_attr) != 0 ||
      pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED) != 0 ||
      pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED) != 0) {
    error = "failed to initialize lrpc process-shared sync attributes";
    return false;
  }
  if (pthread_mutex_init(&region->mutex, &mutex_attr) != 0 ||
      pthread_cond_init(&region->server_cond, &cond_attr) != 0) {
    error = "failed to initialize lrpc process-shared sync objects";
    return false;
  }
  for (uint32_t i = 0; i < kSharedSlotCount; ++i) {
    if (pthread_cond_init(&region->slot_conds[i], &cond_attr) != 0) {
      error = "failed to initialize lrpc slot condition";
      return false;
    }
  }
  pthread_mutexattr_destroy(&mutex_attr);
  pthread_condattr_destroy(&cond_attr);
  return true;
}

bool init_server_region(const std::string& port, PosixMapping& mapping, std::string& error) {
  mapping.name = posix_name(port);
  shm_unlink(mapping.name.c_str());
  mapping.fd = shm_open(mapping.name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
  if (mapping.fd < 0) {
    error = "failed to create lrpc shared-memory object";
    return false;
  }
  if (ftruncate(mapping.fd, static_cast<off_t>(sizeof(SharedRegion))) != 0) {
    error = "failed to size lrpc shared-memory object";
    return false;
  }
  mapping.region = static_cast<SharedRegion*>(
      mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, mapping.fd, 0));
  if (mapping.region == MAP_FAILED) {
    mapping.region = nullptr;
    error = "failed to map lrpc shared-memory object";
    return false;
  }
  std::memset(mapping.region, 0, sizeof(SharedRegion));
  if (!init_process_shared_sync(mapping.region, error)) {
    return false;
  }
  pthread_mutex_lock(&mapping.region->mutex);
  mapping.region->magic = kSharedMagic;
  mapping.region->version = kSharedVersion;
  mapping.region->slot_count = kSharedSlotCount;
  mapping.region->slot_size = kSharedSlotSize;
  mapping.region->server_pid = static_cast<uint32_t>(getpid());
  mapping.region->next_call_id = 1;
  initialize_slots(*mapping.region);
  pthread_mutex_unlock(&mapping.region->mutex);
  return true;
}

bool open_client_region(const std::string& port, PosixMapping& mapping, std::string& error) {
  mapping.name = posix_name(port);
  mapping.fd = shm_open(mapping.name.c_str(), O_RDWR, 0600);
  if (mapping.fd < 0) {
    error = "cannot open lrpc shared-memory endpoint lrpc:" + port;
    return false;
  }
  mapping.region = static_cast<SharedRegion*>(
      mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, mapping.fd, 0));
  if (mapping.region == MAP_FAILED) {
    mapping.region = nullptr;
    error = "failed to map lrpc shared-memory endpoint";
    return false;
  }
  if (mapping.region->magic != kSharedMagic || mapping.region->version != kSharedVersion) {
    error = "invalid lrpc shared-memory endpoint";
    return false;
  }
  return true;
}

void write_error_to_call_slot(uint32_t slot_index, const std::string& message) {
  std::string error;
  SharedRegion* region = g_server_mapping.region;
  std::string response;
  make_error_response(message, response);
  pthread_mutex_lock(&region->mutex);
  SharedSlot& slot = region->slots[slot_index];
  const uint32_t old_next = slot.next_slot;
  if (old_next != kNoSharedSlot) {
    free_slot_chain(*region, old_next);
  }
  reset_slot(slot);
  slot.state = SlotWriting;
  SharedMemoryBlockStream response_stream({SharedSlotRef{slot_index, &slot}}, true);
  (void)response_stream.append(response.data(), static_cast<serialize::STREAM_SIZE>(response.size()));
  response_stream.commit(0, 0, SlotResponseReady, SlotProcessing);
  pthread_cond_broadcast(&region->slot_conds[slot_index]);
  pthread_mutex_unlock(&region->mutex);
}

void process_slot(uint32_t slot_index, std::vector<SharedSlotRef> request_slots) {
  std::string error;
  SharedRegion* region = g_server_mapping.region;
  SharedSlot& call_slot = region->slots[slot_index];
  const uint32_t owner_pid = call_slot.owner_pid;
  const uint32_t call_id = call_slot.call_id;
  SharedMemoryBlockStream request_stream(std::move(request_slots), false);
  auto allocate_response_slot = [region]() -> SharedSlotRef {
    pthread_mutex_lock(&region->mutex);
    SharedSlotRef ref = allocate_free_slot(*region);
    pthread_mutex_unlock(&region->mutex);
    return ref;
  };
  pthread_mutex_lock(&region->mutex);
  SharedSlotRef response_first = allocate_free_slot(*region);
  pthread_mutex_unlock(&region->mutex);
  if (response_first.slot == nullptr) {
    write_error_to_call_slot(slot_index, "no free lrpc shared-memory slots for response");
    return;
  }
  SharedMemoryBlockStream response_stream({response_first}, true, allocate_response_slot);
  if (!(g_dispatch && g_dispatch(request_stream, response_stream, error))) {
    response_stream.reset_to_single_slot_for_write(response_first.index, *response_first.slot);
    response_stream.MarshalError(error.empty() ? "lrpc request failed" : error);
  }
  pthread_mutex_lock(&region->mutex);
  SharedSlot& slot = region->slots[slot_index];
  const uint32_t request_next = slot.next_slot;
  if (request_next != kNoSharedSlot) {
    free_slot_chain(*region, request_next);
  }
  if (!process_is_alive(owner_pid)) {
    free_slot_chain(*region, response_stream.first_slot_index());
    reset_slot(slot);
    pthread_mutex_unlock(&region->mutex);
    return;
  }
  response_stream.commit(owner_pid, call_id, SlotProcessing, SlotProcessing);
  publish_response_chain_header(
      *region,
      slot_index,
      response_stream.first_slot_index(),
      static_cast<uint32_t>(response_stream.Size()),
      owner_pid,
      call_id);
  pthread_cond_broadcast(&region->slot_conds[slot_index]);
  pthread_mutex_unlock(&region->mutex);
}

void server_loop() {
  SharedRegion* region = g_server_mapping.region;
  for (;;) {
    pthread_mutex_lock(&region->mutex);
    bool has_ready = false;
    for (uint32_t i = 0; i < region->slot_count; ++i) {
      if (region->slots[i].state == SlotRequestReady) {
        has_ready = true;
        break;
      }
    }
    if (!has_ready) {
      pthread_cond_wait(&region->server_cond, &region->mutex);
    }
    cleanup_abandoned_client_slots(*region);
    std::vector<std::pair<uint32_t, std::vector<SharedSlotRef>>> ready;
    for (uint32_t i = 0; i < region->slot_count; ++i) {
      SharedSlot& slot = region->slots[i];
      if (slot.state == SlotRequestReady) {
        slot.state = SlotProcessing;
        std::vector<SharedSlotRef> request_slots = collect_slot_chain(*region, i);
        if (!request_slots.empty()) {
          ready.push_back({i, std::move(request_slots)});
        } else {
          std::string error;
          std::string request;
          make_error_response(error.empty() ? "lrpc request failed" : error, request);
          slot.state = SlotWriting;
          SharedMemoryBlockStream response_stream({SharedSlotRef{i, &slot}}, true);
          (void)response_stream.append(request.data(), static_cast<serialize::STREAM_SIZE>(request.size()));
          response_stream.commit(slot.owner_pid, slot.call_id, SlotResponseReady, SlotProcessing);
          pthread_cond_broadcast(&region->slot_conds[i]);
        }
      }
    }
    pthread_mutex_unlock(&region->mutex);
    for (auto& item : ready) {
      std::thread(process_slot, item.first, std::move(item.second)).detach();
    }
  }
}

} // namespace

bool lrpc_start_shared_memory_server_platform(const std::string& port, std::string& error) {
  if (!init_server_region(port, g_server_mapping, error)) {
    return false;
  }
  std::thread(server_loop).detach();
  return true;
}

void lrpc_wait_forever_platform() {
  for (;;) {
    sleep(1);
  }
}

bool lrpc_shared_memory_request_platform(
    const std::string& port,
    LrpcRequestWriter write_request,
    LrpcResponseReader read_response,
    std::string& error) {
  PosixMapping mapping;
  if (!open_client_region(port, mapping, error)) {
    close_posix_mapping(mapping);
    return false;
  }
  SharedRegion* region = mapping.region;
  pthread_mutex_lock(&region->mutex);
  cleanup_abandoned_client_slots(*region);
  uint32_t slot_index = UINT32_MAX;
  for (uint32_t i = 0; i < region->slot_count; ++i) {
    if (region->slots[i].state == SlotFree) {
      slot_index = i;
      break;
    }
  }
  if (slot_index == UINT32_MAX) {
    pthread_mutex_unlock(&region->mutex);
    close_posix_mapping(mapping);
    error = "no free lrpc shared-memory slots";
    return false;
  }
  SharedSlot& slot = region->slots[slot_index];
  const uint32_t call_id = region->next_call_id++;
  slot.state = SlotWriting;
  slot.next_slot = kNoSharedSlot;
  SharedMemoryBlockStream request_stream(
      {SharedSlotRef{slot_index, &slot}},
      true,
      [region]() -> SharedSlotRef {
        return allocate_free_slot(*region);
      });
  if (!write_request(request_stream, error)) {
    pthread_mutex_unlock(&region->mutex);
    close_posix_mapping(mapping);
    return false;
  }
  request_stream.commit(static_cast<uint32_t>(getpid()), call_id, SlotRequestReady, SlotProcessing);
  const uint32_t server_pid = region->server_pid;
  pthread_cond_signal(&region->server_cond);
  int waited_seconds = 0;
  while (slot.state != SlotResponseReady) {
    if (!process_is_alive(server_pid)) {
      if (slot.owner_pid == static_cast<uint32_t>(getpid()) && slot.call_id == call_id) {
        free_slot_chain(*region, slot_index);
      }
      pthread_mutex_unlock(&region->mutex);
      close_posix_mapping(mapping);
      error = "lrpc shared-memory server process exited during call";
      return false;
    }
    if (!wait_with_timeout(region->slot_conds[slot_index], region->mutex, 1, error)) {
      if (process_is_alive(server_pid)) {
        ++waited_seconds;
        if (waited_seconds < 30) {
          continue;
        }
        if (slot.owner_pid == static_cast<uint32_t>(getpid()) &&
            slot.call_id == call_id &&
            slot.state != SlotProcessing) {
          free_slot_chain(*region, slot_index);
        }
        pthread_mutex_unlock(&region->mutex);
        close_posix_mapping(mapping);
        error = "timed out waiting for lrpc shared-memory response";
        return false;
      }
      if (slot.owner_pid == static_cast<uint32_t>(getpid()) &&
          slot.call_id == call_id &&
          slot.state != SlotProcessing) {
        free_slot_chain(*region, slot_index);
      }
      pthread_mutex_unlock(&region->mutex);
      close_posix_mapping(mapping);
      error = "lrpc shared-memory server process exited during call";
      return false;
    }
    waited_seconds = 0;
  }
  std::vector<SharedSlotRef> response_slots = collect_slot_chain(*region, slot_index);
  SharedMemoryBlockStream response_stream(std::move(response_slots), false);
  if (!read_response(response_stream, error)) {
    pthread_mutex_unlock(&region->mutex);
    close_posix_mapping(mapping);
    return false;
  }
  free_slot_chain(*region, slot_index);
  pthread_mutex_unlock(&region->mutex);
  close_posix_mapping(mapping);
  return true;
}

} // namespace xlang3::ipc

#endif
