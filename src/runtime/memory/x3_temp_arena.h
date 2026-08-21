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
#pragma once

#include "runtime/memory/x3_memory_stats.h"
#include "runtime/memory/x3_page_allocator.h"

#include <cstddef>
#include <vector>

namespace xlang3::memory {

class X3TempArena {
public:
  explicit X3TempArena(size_t block_bytes = 16 * 1024);
  ~X3TempArena();

  X3TempArena(const X3TempArena&) = delete;
  X3TempArena& operator=(const X3TempArena&) = delete;

  void* allocate(size_t bytes, size_t alignment = alignof(std::max_align_t));
  void reset() noexcept;

  const X3MemoryCounter& stats() const { return stats_; }

private:
  struct Block {
    unsigned char* memory = nullptr;
    size_t capacity = 0;
    size_t used = 0;
  };

  static size_t align_up(size_t value, size_t alignment);
  Block& ensure_block(size_t bytes, size_t alignment);

  X3PageAllocator pages_;
  std::vector<Block> blocks_;
  size_t block_bytes_ = 0;
  size_t current_ = 0;
  X3MemoryCounter stats_;
};

} // namespace xlang3::memory
