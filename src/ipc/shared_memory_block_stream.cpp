/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "shared_memory_block_stream.h"

#include <cstring>
#include <utility>
#include <stdexcept>

namespace xlang3::ipc {

SharedMemoryBlockStream::SharedMemoryBlockStream(std::vector<SharedSlotRef> slots, bool writable, AllocateSlot allocate_slot, bool allow_spill)
    : writable_(writable), allocate_slot_(std::move(allocate_slot)), allow_spill_(allow_spill) {
  for (auto ref : slots) {
    append_ref(ref, writable_);
  }
  SetPos({0, 0});
}

void SharedMemoryBlockStream::append_ref(SharedSlotRef ref, bool writable_block) {
  if (ref.slot == nullptr) {
    return;
  }
  Block block;
  block.ref = ref;
  block.info.buf = ref.slot->payload;
  block.info.block_size = kSharedSlotSize;
  block.info.data_size = writable_block ? 0 : ref.slot->payload_size;
  blocks_.push_back(std::move(block));
}

serialize::STREAM_SIZE SharedMemoryBlockStream::Size() {
  serialize::STREAM_SIZE size = 0;
  for (const auto& block : blocks_) {
    size += block.info.data_size;
  }
  return size;
}

int SharedMemoryBlockStream::BlockNum() {
  return static_cast<int>(blocks_.size());
}

serialize::blockInfo& SharedMemoryBlockStream::GetBlockInfo(int index) {
  return blocks_[static_cast<size_t>(index)].info;
}

bool SharedMemoryBlockStream::NewBlock() {
  if (!writable_ || !allocate_slot_) {
    return false;
  }
  if (blocks_.size() >= kSharedSlotCount) return false;
  SharedSlotRef next = spilled_ ? SharedSlotRef{} : allocate_slot_();
  if (next.slot == nullptr || next.index == kNoSharedSlot) {
    if (allow_spill_) {
      Block block{};
      block.owned = std::make_unique<char[]>(kSharedSlotSize);
      block.info = {block.owned.get(), kSharedSlotSize, 0};
      blocks_.push_back(std::move(block));
      spilled_ = true;
      return true;
    }
    return false;
  }
  if (!blocks_.empty()) {
    blocks_.back().ref.slot->next_slot = next.index;
  }
  next.slot->next_slot = kNoSharedSlot;
  append_ref(next, true);
  return true;
}

std::unique_ptr<serialize::BlockStream> SharedMemoryBlockStream::stage_spill() {
  auto staged = std::make_unique<serialize::BlockStream>();
  for (const auto& block : blocks_) {
    if (!staged->append(block.info.buf, block.info.data_size))
      throw std::runtime_error("cannot stage lrpc message under capacity pressure");
  }
  return staged;
}

bool SharedMemoryBlockStream::restore(serialize::BlockStream& staged, std::vector<SharedSlotRef> slots) {
  blocks_.clear();
  spilled_ = false;
  for (auto ref : slots) append_ref(ref, true);
  SetPos({0, 0});
  for (int i = 0; i < staged.BlockNum(); ++i) {
    const auto& block = staged.GetBlockInfo(i);
    if (!append(block.buf, block.data_size)) return false;
  }
  return true;
}

bool SharedMemoryBlockStream::MoveToNextBlock() {
  return true;
}

bool SharedMemoryBlockStream::FullCopyTo(char* buf, serialize::STREAM_SIZE bufSize) {
  if (bufSize < Size()) {
    return false;
  }
  for (auto& block : blocks_) {
    if (block.info.data_size > 0) {
      std::memcpy(buf, block.info.buf, static_cast<size_t>(block.info.data_size));
      buf += block.info.data_size;
    }
  }
  return true;
}

uint32_t SharedMemoryBlockStream::first_slot_index() const {
  return blocks_.empty() ? kNoSharedSlot : blocks_.front().ref.index;
}

void SharedMemoryBlockStream::commit(uint32_t owner_pid, uint32_t call_id, uint32_t first_state, uint32_t continuation_state) {
  const auto total = static_cast<uint32_t>(Size());
  for (size_t i = 0; i < blocks_.size(); ++i) {
    SharedSlot& slot = *blocks_[i].ref.slot;
    slot.owner_pid = owner_pid;
    slot.call_id = call_id;
    slot.total_size = total;
    slot.payload_size = static_cast<uint32_t>(blocks_[i].info.data_size);
    slot.next_slot = (i + 1 < blocks_.size()) ? blocks_[i + 1].ref.index : kNoSharedSlot;
    slot.state = i == 0 ? first_state : continuation_state;
  }
}

void SharedMemoryBlockStream::reset_to_single_slot_for_write(uint32_t first_index, SharedSlot& first_slot) {
  blocks_.clear();
  spilled_ = false;
  first_slot.next_slot = kNoSharedSlot;
  append_ref(SharedSlotRef{first_index, &first_slot}, true);
  SetPos({0, 0});
}

} // namespace xlang3::ipc
