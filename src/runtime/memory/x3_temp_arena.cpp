/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "runtime/memory/x3_temp_arena.h"

#include <algorithm>

namespace xlang3::memory {

X3TempArena::X3TempArena(size_t block_bytes)
    : block_bytes_(std::max<size_t>(1024, block_bytes)) {}

X3TempArena::~X3TempArena() {
  for (auto& block : blocks_) {
    pages_.release(block.memory, block.capacity, alignof(std::max_align_t));
  }
}

void* X3TempArena::allocate(size_t bytes, size_t alignment) {
  if (bytes == 0) {
    bytes = 1;
  }
  Block& block = ensure_block(bytes, alignment);
  const size_t aligned = align_up(block.used, alignment);
  void* out = block.memory + aligned;
  block.used = aligned + bytes;
  x3_memory_note_alloc(stats_, bytes);
  return out;
}

void X3TempArena::reset() noexcept {
  for (auto& block : blocks_) {
    block.used = 0;
  }
  current_ = 0;
  if (stats_.live_bytes != 0) {
    x3_memory_note_free(stats_, stats_.live_bytes);
  }
}

size_t X3TempArena::align_up(size_t value, size_t alignment) {
  if (alignment == 0) {
    return value;
  }
  const size_t mask = alignment - 1;
  return (value + mask) & ~mask;
}

X3TempArena::Block& X3TempArena::ensure_block(size_t bytes, size_t alignment) {
  for (size_t attempt = 0; attempt < blocks_.size(); ++attempt) {
    Block& block = blocks_[(current_ + attempt) % blocks_.size()];
    const size_t aligned = align_up(block.used, alignment);
    if (aligned + bytes <= block.capacity) {
      current_ = (current_ + attempt) % blocks_.size();
      return block;
    }
  }

  const size_t capacity = std::max(block_bytes_, bytes + alignment);
  auto* memory = static_cast<unsigned char*>(pages_.allocate(capacity, alignof(std::max_align_t)));
  blocks_.push_back(Block{memory, capacity, 0});
  current_ = blocks_.size() - 1;
  ++stats_.slab_count;
  return blocks_.back();
}

} // namespace xlang3::memory
