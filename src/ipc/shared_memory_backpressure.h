/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "shared_memory_block_stream.h"
#include <chrono>

namespace xlang3::ipc {

struct SharedCapacityWaiter {
  uint64_t ticket;
  uint32_t process;
};

// Called with the region mutex held; wait_space releases it while waiting.
template<class Region, class Wait, class Alive>
bool wait_for_message_capacity(Region& region, uint32_t count, uint32_t peer,
    Wait wait_space, Alive alive, std::string& error, uint32_t owner = 0) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  bool waited = false;
  SharedCapacityWaiter* reservation = nullptr;
  struct Release {
    SharedCapacityWaiter*& entry;
    ~Release() { if (entry) entry->process = 0; }
  } release{reservation};
  for (;;) {
    SharedCapacityWaiter* first = nullptr;
    SharedCapacityWaiter* empty = nullptr;
    for (auto& entry : region.capacity_waiters) {
      if (entry.process && !alive(entry.process)) entry.process = 0;
      if (!entry.process) { if (!empty) empty = &entry; }
      else if (!first || entry.ticket < first->ticket) first = &entry;
    }
    uint32_t available = 0;
    for (uint32_t i = 0; i < region.slot_count; ++i)
      available += region.slots[i].state == SlotFree;
    if (available >= count && (!first || first == reservation) && (!waited || alive(peer))) return true;
    if (!alive(peer)) {
      error = "lrpc peer exited while waiting for shared-memory capacity";
      return false;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      error = "timed out waiting for lrpc shared-memory capacity";
      return false;
    }
    // Completed messages have priority over admitting new request heads.
    if (owner && !reservation) {
      if (!empty) { error = "too many lrpc capacity waiters"; return false; }
      reservation = empty;
      reservation->ticket = ++region.next_capacity_ticket;
      reservation->process = owner;
    }
    waited = true;
    wait_space();
  }
}

template<class Region, class Free, class Allocate, class Wait, class Alive>
bool publish_spilled_message(Region& region, SharedMemoryBlockStream& stream,
    uint32_t& head, bool retain_head, uint32_t peer, Free free_chain,
    Allocate allocate, Wait wait_space, Alive alive, std::string& error) {
  if (!stream.has_spill()) return true;
  auto staged = stream.stage_spill();
  const uint32_t needed = static_cast<uint32_t>((staged->Size() + kSharedSlotSize - 1) / kSharedSlotSize);
  const uint32_t owner = retain_head ? region.server_pid : region.slots[head].owner_pid;
  if (retain_head) {
    free_chain(region, region.slots[head].next_slot);
    region.slots[head].next_slot = kNoSharedSlot;
    stream.reset_to_single_slot_for_write(head, region.slots[head]);
  } else {
    free_chain(region, head);
    head = kNoSharedSlot;
  }
  if (!wait_for_message_capacity(region, needed - (retain_head ? 1 : 0), peer,
          wait_space, alive, error, owner)) return false;
  std::vector<SharedSlotRef> slots;
  slots.reserve(needed);
  if (retain_head) slots.push_back({head, &region.slots[head]});
  while (slots.size() < needed) slots.push_back(allocate(region));
  head = slots.front().index;
  return stream.restore(*staged, std::move(slots));
}

} // namespace xlang3::ipc
