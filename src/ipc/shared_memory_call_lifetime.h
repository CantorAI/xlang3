/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0 */
#pragma once
#include "shared_memory_transport_internal.h"
#include <vector>

namespace xlang3::ipc {
struct SharedCallKey {
  uint32_t index;
  uint32_t owner;
  uint32_t call;
};

// All access, including cancellation checks, requires the region mutex.
template<class Region>
bool owns_shared_call(const Region& region, const SharedCallKey& key) {
  if (key.index >= region.slot_count) return false;
  const auto& slot = region.slots[key.index];
  return slot.state != SlotFree && slot.owner_pid == key.owner && slot.call_id == key.call;
}

template<class Region, class Free>
bool detach_shared_request(Region& region, const SharedCallKey& key,
    std::vector<char>& request, Free free_chain, std::string& error) {
  if (!owns_shared_call(region, key) || region.slots[key.index].state != SlotQueued)
    return false; // Cancelled while waiting in the dispatch pool.
  const auto total = region.slots[key.index].total_size;
  if (total > uint64_t(region.slot_count) * kSharedSlotSize) {
    error = "invalid lrpc request size"; return false;
  }
  request.reserve(total);
  auto index = key.index;
  uint32_t visited = 0;
  while (index != kNoSharedSlot) {
    if (index >= region.slot_count || ++visited > region.slot_count) {
      error = "invalid lrpc request chain"; return false;
    }
    const auto& slot = region.slots[index];
    if (slot.owner_pid != key.owner || slot.call_id != key.call ||
        slot.payload_size > kSharedSlotSize || request.size() + slot.payload_size > total) {
      error = "invalid lrpc request block"; return false;
    }
    request.insert(request.end(), slot.payload, slot.payload + slot.payload_size);
    index = slot.next_slot;
  }
  if (request.size() != total) { error = "truncated lrpc request"; return false; }
  auto& head = region.slots[key.index];
  free_chain(region, head.next_slot);
  head.next_slot = kNoSharedSlot;
  head.payload_size = head.total_size = 0;
  head.state = SlotProcessing;
  return true;
}
} // namespace xlang3::ipc
