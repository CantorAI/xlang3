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

#include <cstddef>
#include <cstdint>

namespace xlang3::memory {

struct X3MemoryCounter {
  uint64_t alloc_count = 0;
  uint64_t free_count = 0;
  uint64_t reuse_count = 0;
  uint64_t slab_count = 0;
  size_t live_bytes = 0;
  size_t peak_live_bytes = 0;
};

struct X3MemoryStats {
  X3MemoryCounter object_pools;
  X3MemoryCounter buckets;
  X3MemoryCounter large;
  X3MemoryCounter temp_arenas;
};

inline void x3_memory_note_alloc(X3MemoryCounter& counter, size_t bytes) {
  ++counter.alloc_count;
  counter.live_bytes += bytes;
  if (counter.live_bytes > counter.peak_live_bytes) {
    counter.peak_live_bytes = counter.live_bytes;
  }
}

inline void x3_memory_note_free(X3MemoryCounter& counter, size_t bytes) {
  ++counter.free_count;
  counter.live_bytes = bytes <= counter.live_bytes ? counter.live_bytes - bytes : 0;
}

} // namespace xlang3::memory
