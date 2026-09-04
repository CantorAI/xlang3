/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#pragma once

#include "shared_memory_transport_internal.h"
#include "xlang3/serialize/xlang_stream.h"

#include <functional>
#include <vector>

namespace xlang3::ipc {

struct SharedSlotRef {
  uint32_t index = kNoSharedSlot;
  SharedSlot* slot = nullptr;
};

class SharedMemoryBlockStream : public serialize::XLangStream {
public:
  using AllocateSlot = std::function<SharedSlotRef()>;

  SharedMemoryBlockStream(std::vector<SharedSlotRef> slots, bool writable, AllocateSlot allocate_slot = {});

  serialize::STREAM_SIZE Size() override;
  void Refresh() override {}
  int BlockNum() override;
  serialize::blockInfo& GetBlockInfo(int index) override;
  bool NewBlock() override;
  bool MoveToNextBlock() override;
  bool FullCopyTo(char* buf, serialize::STREAM_SIZE bufSize) override;

  uint32_t first_slot_index() const;
  void commit(uint32_t owner_pid, uint32_t call_id, uint32_t first_state, uint32_t continuation_state);
  void reset_to_single_slot_for_write(uint32_t first_index, SharedSlot& first_slot);

private:
  struct Block {
    SharedSlotRef ref;
    serialize::blockInfo info;
  };

  void append_ref(SharedSlotRef ref, bool writable_block);

  std::vector<Block> blocks_;
  bool writable_ = false;
  AllocateSlot allocate_slot_;
};

} // namespace xlang3::ipc
