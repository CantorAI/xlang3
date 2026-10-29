/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "shared_memory_transport_internal.h"

#if !defined(_WIN32)

#include "shared_memory_block_stream.h"
#include "shared_memory_backpressure.h"
#include "shared_memory_server_threads.h"
#include "shared_memory_call_lifetime.h"
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
  uint64_t session_id;
  uint64_t next_capacity_ticket;
  SharedCapacityWaiter capacity_waiters[kSharedSlotCount];
  SharedSlot slots[kSharedSlotCount];
};

struct PosixMapping {
  int fd = -1;
  SharedRegion* region = nullptr;
  std::string name;
};

PosixMapping g_server_mapping;
SharedMemoryServerThreads g_server_threads;

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
    if (slot.state != SlotFree &&
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
  mapping.region->session_id = next_listener_session();
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


void process_slot(SharedCallKey key) {
  std::string error;
  auto* region = g_server_mapping.region;
  std::vector<char> request;
  pthread_mutex_lock(&region->mutex);
  if (!detach_shared_request(*region, key, request, free_slot_chain, error)) {
    if (owns_shared_call(*region, key)) free_slot_chain(*region, key.index);
    pthread_mutex_unlock(&region->mutex);
    return;
  }
  pthread_mutex_unlock(&region->mutex);
  auto cancelled = [region, key] {
    std::string error;
    pthread_mutex_lock(&region->mutex);
    const bool cancelled = !owns_shared_call(*region, key) || !process_is_alive(key.owner);
    if (cancelled && owns_shared_call(*region, key)) free_slot_chain(*region, key.index);
    pthread_mutex_unlock(&region->mutex);
    return cancelled;
  };
  serialize::BlockStream request_stream(request.data(), static_cast<serialize::STREAM_SIZE>(request.size()), false);
  serialize::BlockStream response;
  bool dispatched = false;
  {
    LrpcCancellationScope cancellation(cancelled);
    try { dispatched = !cancelled() && g_dispatch && g_dispatch(request_stream, response, error); }
    catch (const std::exception& exception) { error = exception.what(); }
    catch (...) { error = "lrpc dispatch threw an unknown exception"; }
  }
  pthread_mutex_lock(&region->mutex);
  // A timed-out head can already belong to a newer call. Never publish an
  // old result, cancellation, or error into the new call's slot.
  if (!owns_shared_call(*region, key)) { pthread_mutex_unlock(&region->mutex); return; }
  if (!process_is_alive(key.owner)) {
    free_slot_chain(*region, key.index); pthread_mutex_unlock(&region->mutex); return;
  }
  auto& slot = region->slots[key.index];
  SharedMemoryBlockStream response_stream({SharedSlotRef{key.index, &slot}}, true,
      [region] { return allocate_free_slot(*region); }, true);
  if (dispatched) {
    for (int i = 0; i < response.BlockNum(); ++i) {
      const auto& block = response.GetBlockInfo(i);
      if (!response_stream.append(block.buf, block.data_size)) {
        dispatched = false; error = "lrpc response exceeds shared-memory capacity"; break;
      }
    }
  }
  auto head = key.index;
  if (dispatched && response_stream.has_spill()) {
    dispatched = publish_spilled_message(*region, response_stream, head, true, key.owner,
        free_slot_chain, allocate_free_slot, [region] { pthread_mutex_unlock(&region->mutex); std::this_thread::sleep_for(std::chrono::milliseconds(1)); pthread_mutex_lock(&region->mutex); }, process_is_alive, error,
        [region, key] { return !owns_shared_call(*region, key); });
  }
  if (!owns_shared_call(*region, key)) { pthread_mutex_unlock(&region->mutex); return; }
  if (!dispatched || !response_stream.Size()) {
    free_slot_chain(*region, slot.next_slot);
    response_stream.reset_to_single_slot_for_write(key.index, slot);
    response_stream.MarshalError(error.empty() ? "lrpc request failed" : error);
  }
  if (!process_is_alive(key.owner)) {
    free_slot_chain(*region, key.index); pthread_mutex_unlock(&region->mutex); return;
  }
  response_stream.commit(key.owner, key.call, SlotResponseReady, SlotProcessing);
  pthread_cond_broadcast(&region->slot_conds[key.index]);
  pthread_mutex_unlock(&region->mutex);
}

void server_loop() {
  SharedRegion* region = g_server_mapping.region;
  for (;;) {
    pthread_mutex_lock(&region->mutex);
    if (g_server_threads.stopping) { pthread_mutex_unlock(&region->mutex); return; }
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
    if (g_server_threads.stopping) { pthread_mutex_unlock(&region->mutex); return; }
    cleanup_abandoned_client_slots(*region);
    std::vector<SharedCallKey> ready;
    for (uint32_t i = 0; i < region->slot_count; ++i) {
      auto& slot = region->slots[i];
      if (slot.state == SlotRequestReady) {
        slot.state = SlotQueued;
        ready.push_back({i, slot.owner_pid, slot.call_id});
      }
    }
    pthread_mutex_unlock(&region->mutex);
    for (const auto& key : ready) {
      auto valid = [region, key] {
        if (pthread_mutex_trylock(&region->mutex) != 0) return true;
        const bool current = owns_shared_call(*region, key) && region->slots[key.index].state == SlotQueued;
        pthread_mutex_unlock(&region->mutex);
        return current;
      };
      if (g_server_threads.launch([key] { process_slot(key); }, valid)) continue;
      pthread_mutex_lock(&region->mutex);
      if (owns_shared_call(*region, key)) {
        auto& slot = region->slots[key.index];
        free_slot_chain(*region, slot.next_slot);
        SharedMemoryBlockStream response({SharedSlotRef{key.index, &slot}}, true);
        response.MarshalError("lrpc dispatch queue is full");
        response.commit(key.owner, key.call, SlotResponseReady, SlotProcessing);
        pthread_cond_broadcast(&region->slot_conds[key.index]);
      }
      pthread_mutex_unlock(&region->mutex);
    }
  }
}

} // namespace

bool lrpc_probe_platform(const std::string& port, LrpcEndpointInfo& info, std::string& error) {
  info = {};
  PosixMapping mapping;
  mapping.fd = shm_open(posix_name(port).c_str(), O_RDWR, 0);
  if (mapping.fd < 0) {
    if (errno == ENOENT) return true;
    error = "cannot probe lrpc shared memory: " + std::string(std::strerror(errno));
    return false;
  }
  struct stat status{};
  // Darwin rounds POSIX shared-memory objects up to the VM page size.
  if (fstat(mapping.fd, &status) != 0 || status.st_size < static_cast<off_t>(sizeof(SharedRegion))) {
    close_posix_mapping(mapping);
    error = "incompatible lrpc shared-memory region size";
    return false;
  }
  void* address = mmap(nullptr, sizeof(SharedRegion), PROT_READ | PROT_WRITE, MAP_SHARED, mapping.fd, 0);
  if (address == MAP_FAILED) {
    error = "cannot map lrpc probe: " + std::string(std::strerror(errno));
    close_posix_mapping(mapping); return false;
  }
  mapping.region = static_cast<SharedRegion*>(address);
  auto* region = mapping.region;
  if (!region->magic) { close_posix_mapping(mapping); return true; }
  if (region->magic != kSharedMagic || region->version != kSharedVersion) {
    error = "incompatible lrpc shared-memory protocol";
    close_posix_mapping(mapping); return false;
  }
  int locked = pthread_mutex_trylock(&region->mutex);
#if defined(__linux__)
  if (locked == EOWNERDEAD) { pthread_mutex_consistent(&region->mutex); locked = 0; }
#endif
  if (locked == EBUSY) { close_posix_mapping(mapping); return true; }
  if (locked != 0) {
    error = "cannot lock lrpc probe mutex: " + std::string(std::strerror(locked));
    close_posix_mapping(mapping); return false;
  }
  if (region->magic == kSharedMagic && process_is_alive(region->server_pid))
    info = {region->server_pid, region->session_id};
  pthread_mutex_unlock(&region->mutex);
  close_posix_mapping(mapping);
  return true;
}

bool lrpc_start_shared_memory_server_platform(const std::string& port, std::string& error) {
  if (!init_server_region(port, g_server_mapping, error)) {
    return false;
  }
  g_server_threads.stopping = false;
  g_server_threads.listener = std::thread(server_loop);
  return true;
}

void lrpc_stop_shared_memory_server_platform() {
  auto* region = g_server_mapping.region;
  if (!region) return;
  pthread_mutex_lock(&region->mutex);
  g_server_threads.stopping = true;
  pthread_cond_broadcast(&region->server_cond);
  pthread_mutex_unlock(&region->mutex);
  g_server_threads.drain();
  pthread_mutex_lock(&region->mutex);
  region->server_pid = 0;
  region->magic = 0;
  for (uint32_t i = 0; i < kSharedSlotCount; ++i) pthread_cond_broadcast(&region->slot_conds[i]);
  pthread_mutex_unlock(&region->mutex);
  shm_unlink(g_server_mapping.name.c_str());
  close_posix_mapping(g_server_mapping);
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
  if (!wait_for_message_capacity(*region, 1, region->server_pid,
          [region] {
            pthread_mutex_unlock(&region->mutex);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            pthread_mutex_lock(&region->mutex);
            cleanup_abandoned_client_slots(*region);
          }, process_is_alive, error)) {
    pthread_mutex_unlock(&region->mutex);
    close_posix_mapping(mapping);
    return false;
  }
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
  const uint32_t call_id = region->next_call_id++;
  region->slots[slot_index].state = SlotWriting;
  region->slots[slot_index].owner_pid = static_cast<uint32_t>(getpid());
  region->slots[slot_index].next_slot = kNoSharedSlot;
  SharedMemoryBlockStream request_stream(
      {SharedSlotRef{slot_index, &region->slots[slot_index]}},
      true,
      [region]() -> SharedSlotRef {
        return allocate_free_slot(*region);
      }, true);
  if (!write_request(request_stream, error)) {
    free_slot_chain(*region, slot_index);
    pthread_mutex_unlock(&region->mutex);
    close_posix_mapping(mapping);
    return false;
  }
  if (!publish_spilled_message(*region, request_stream, slot_index, false,
          region->server_pid, free_slot_chain, allocate_free_slot,
          [region] {
            pthread_mutex_unlock(&region->mutex);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            pthread_mutex_lock(&region->mutex);
            cleanup_abandoned_client_slots(*region);
          }, process_is_alive, error)) {
    free_slot_chain(*region, slot_index);
    pthread_mutex_unlock(&region->mutex);
    close_posix_mapping(mapping);
    return false;
  }
  SharedSlot& slot = region->slots[slot_index];
  request_stream.commit(static_cast<uint32_t>(getpid()), call_id, SlotRequestReady, SlotProcessing);
  const uint32_t server_pid = region->server_pid;
  pthread_cond_signal(&region->server_cond);
  int waited_seconds = 0;
  const auto configured_timeout_ms = lrpc_request_timeout_ms();
  const bool wait_forever = configured_timeout_ms == 0;
  const int timeout_seconds = static_cast<int>((configured_timeout_ms + 999) / 1000);
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
        if (wait_forever || waited_seconds < timeout_seconds) {
          continue;
        }
        if (slot.owner_pid == static_cast<uint32_t>(getpid()) &&
            slot.call_id == call_id) {
          free_slot_chain(*region, slot_index);
        }
        pthread_mutex_unlock(&region->mutex);
        close_posix_mapping(mapping);
        error = "timed out waiting for lrpc shared-memory response";
        return false;
      }
      if (slot.owner_pid == static_cast<uint32_t>(getpid()) &&
          slot.call_id == call_id) {
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
    free_slot_chain(*region, slot_index);
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
