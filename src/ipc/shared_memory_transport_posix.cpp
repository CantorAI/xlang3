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

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <system_error>
#if defined(__linux__)
#include <linux/futex.h>
#include <sys/syscall.h>
#endif
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace xlang3::ipc {

namespace {

#if defined(__linux__)
// A dead pthread_cond waiter can strand glibc's userspace waiter groups and
// block broadcast while the region mutex is held. Futex queues live in the
// kernel and retire a killed process without leaving such shared counters.
union SharedCondition {
  pthread_cond_t reserved_layout;
  uint32_t sequence;
};
static_assert(sizeof(SharedCondition) == sizeof(pthread_cond_t));
static_assert(alignof(SharedCondition) == alignof(pthread_cond_t));
constexpr uint32_t kPosixSharedVersion = kSharedVersion + 1;
#else
using SharedCondition = pthread_cond_t;
constexpr uint32_t kPosixSharedVersion = kSharedVersion;
#endif

int lock_shared_mutex(pthread_mutex_t* mutex, bool try_only = false) {
  int result = try_only ? pthread_mutex_trylock(mutex) : pthread_mutex_lock(mutex);
#if defined(__linux__)
  if (result == EOWNERDEAD) result = pthread_mutex_consistent(mutex);
#endif
  return result;
}

void lock_region_mutex(pthread_mutex_t* mutex) {
  const int result = lock_shared_mutex(mutex);
  if (result) throw std::system_error(result, std::generic_category(), "lrpc shared mutex");
}

int init_condition(SharedCondition* condition, pthread_condattr_t* attributes) {
#if defined(__linux__)
  (void)attributes;
  condition->sequence = 0;
  return 0;
#else
  return pthread_cond_init(condition, attributes);
#endif
}

void notify_condition(SharedCondition* condition, bool all = true) {
#if defined(__linux__)
  __atomic_add_fetch(&condition->sequence, 1u, __ATOMIC_RELEASE);
  (void)syscall(SYS_futex, &condition->sequence, FUTEX_WAKE, all ? INT_MAX : 1, nullptr, nullptr, 0);
#else
  if (all) pthread_cond_broadcast(condition);
  else pthread_cond_signal(condition);
#endif
}

struct SharedRegion {
  pthread_mutex_t mutex;
  SharedCondition server_cond;
  SharedCondition slot_conds[kSharedSlotCount];
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

bool wait_with_timeout(SharedCondition& cond, pthread_mutex_t& mutex, int milliseconds, std::string& error) {
#if defined(__linux__)
  // Snapshot before unlocking: a notification between unlock and FUTEX_WAIT
  // changes the value and returns EAGAIN instead of losing the wakeup.
  const auto observed = __atomic_load_n(&cond.sequence, __ATOMIC_ACQUIRE);
  pthread_mutex_unlock(&mutex);
  const timespec timeout{milliseconds / 1000, (milliseconds % 1000) * 1000000L};
  const int rc = syscall(SYS_futex, &cond.sequence, FUTEX_WAIT, observed, &timeout, nullptr, 0);
  const int saved_errno = errno;
  lock_region_mutex(&mutex);
  if (rc == 0 || saved_errno == EAGAIN || saved_errno == EINTR) return true;
#else
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += milliseconds / 1000;
  ts.tv_nsec += (milliseconds % 1000) * 1000000L;
  if (ts.tv_nsec >= 1000000000L) { ++ts.tv_sec; ts.tv_nsec -= 1000000000L; }
  const int rc = pthread_cond_timedwait(&cond, &mutex, &ts);
  if (rc == 0) return true;
#endif
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
#if defined(__linux__)
  if (pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST) != 0) {
    error = "failed to initialize lrpc robust shared mutex";
    return false;
  }
#endif
  if (pthread_mutex_init(&region->mutex, &mutex_attr) != 0 ||
      init_condition(&region->server_cond, &cond_attr) != 0) {
    error = "failed to initialize lrpc process-shared sync objects";
    return false;
  }
  for (uint32_t i = 0; i < kSharedSlotCount; ++i) {
    if (init_condition(&region->slot_conds[i], &cond_attr) != 0) {
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
  lock_region_mutex(&mapping.region->mutex);
  mapping.region->magic = kSharedMagic;
  mapping.region->version = kPosixSharedVersion;
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
  if (mapping.region->magic != kSharedMagic || mapping.region->version != kPosixSharedVersion) {
    error = "invalid lrpc shared-memory endpoint";
    return false;
  }
  return true;
}


void process_slot(SharedCallKey key) {
  std::string error;
  auto* region = g_server_mapping.region;
  std::vector<char> request;
  lock_region_mutex(&region->mutex);
  if (!detach_shared_request(*region, key, request, free_slot_chain, error)) {
    if (owns_shared_call(*region, key)) free_slot_chain(*region, key.index);
    pthread_mutex_unlock(&region->mutex);
    return;
  }
  pthread_mutex_unlock(&region->mutex);
  auto cancelled = [region, key] {
    std::string error;
    lock_region_mutex(&region->mutex);
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
  lock_region_mutex(&region->mutex);
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
        free_slot_chain, allocate_free_slot, [region] { pthread_mutex_unlock(&region->mutex); std::this_thread::sleep_for(std::chrono::milliseconds(1)); lock_region_mutex(&region->mutex); }, process_is_alive, error,
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
  notify_condition(&region->slot_conds[key.index]);
  pthread_mutex_unlock(&region->mutex);
}

void server_loop() {
  SharedRegion* region = g_server_mapping.region;
  for (;;) {
    lock_region_mutex(&region->mutex);
    if (g_server_threads.stopping) { pthread_mutex_unlock(&region->mutex); return; }
    bool has_ready = false;
    for (uint32_t i = 0; i < region->slot_count; ++i) {
      if (region->slots[i].state == SlotRequestReady) {
        has_ready = true;
        break;
      }
    }
    if (!has_ready) {
      std::string wait_error;
      wait_with_timeout(region->server_cond, region->mutex, 1000, wait_error);
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
        if (lock_shared_mutex(&region->mutex, true) != 0) return true;
        const bool current = owns_shared_call(*region, key) && region->slots[key.index].state == SlotQueued;
        pthread_mutex_unlock(&region->mutex);
        return current;
      };
      if (g_server_threads.launch([key] { process_slot(key); }, valid)) continue;
      lock_region_mutex(&region->mutex);
      if (owns_shared_call(*region, key)) {
        auto& slot = region->slots[key.index];
        free_slot_chain(*region, slot.next_slot);
        SharedMemoryBlockStream response({SharedSlotRef{key.index, &slot}}, true);
        response.MarshalError("lrpc dispatch queue is full");
        response.commit(key.owner, key.call, SlotResponseReady, SlotProcessing);
        notify_condition(&region->slot_conds[key.index]);
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
  if (region->magic != kSharedMagic || region->version != kPosixSharedVersion) {
    error = "incompatible lrpc shared-memory protocol";
    close_posix_mapping(mapping); return false;
  }
  int locked = lock_shared_mutex(&region->mutex, true);
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
  lock_region_mutex(&region->mutex);
  g_server_threads.stopping = true;
  notify_condition(&region->server_cond);
  pthread_mutex_unlock(&region->mutex);
  g_server_threads.drain();
  lock_region_mutex(&region->mutex);
  region->server_pid = 0;
  region->magic = 0;
  for (uint32_t i = 0; i < kSharedSlotCount; ++i) notify_condition(&region->slot_conds[i]);
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
  lock_region_mutex(&region->mutex);
  cleanup_abandoned_client_slots(*region);
  if (!wait_for_message_capacity(*region, 1, region->server_pid,
          [region] {
            pthread_mutex_unlock(&region->mutex);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            lock_region_mutex(&region->mutex);
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
            lock_region_mutex(&region->mutex);
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
  notify_condition(&region->server_cond, false);
  const auto configured_timeout_ms = lrpc_request_timeout_ms();
  const bool wait_forever = configured_timeout_ms == 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(configured_timeout_ms);
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
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    const int wait_ms = wait_forever ? 1000 : static_cast<int>(std::clamp<int64_t>(remaining, 1, 1000));
    const bool timed_out = !wait_forever && remaining <= 0;
    if (timed_out || !wait_with_timeout(region->slot_conds[slot_index], region->mutex, wait_ms, error)) {
      if (process_is_alive(server_pid)) {
        if (wait_forever || std::chrono::steady_clock::now() < deadline) {
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
  }
  std::vector<SharedSlotRef> response_slots = collect_slot_chain(*region, slot_index);
  SharedMemoryBlockStream response_stream(std::move(response_slots), false);
  error.clear(); // An earlier health-check wait may have expired before this response.
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
