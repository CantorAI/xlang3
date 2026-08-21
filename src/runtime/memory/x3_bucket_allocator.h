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
#include "runtime/memory/x3_size_class.h"

#include <array>
#include <cstddef>
#include <vector>

namespace xlang3::memory {

class X3BucketAllocator {
public:
  X3BucketAllocator();
  ~X3BucketAllocator();

  X3BucketAllocator(const X3BucketAllocator&) = delete;
  X3BucketAllocator& operator=(const X3BucketAllocator&) = delete;

  void* allocate(size_t bytes);
  void release(void* ptr, size_t bytes) noexcept;

  const X3MemoryCounter& bucket_stats() const { return bucket_stats_; }
  const X3MemoryCounter& large_stats() const { return large_stats_; }

private:
  struct FreeSlot {
    FreeSlot* next = nullptr;
  };

  struct Slab {
    void* memory = nullptr;
    size_t bytes = 0;
    uint32_t size_class = kInvalidSizeClass;
  };

  struct Bucket {
    FreeSlot* free_list = nullptr;
    std::vector<Slab> slabs;
  };

  // Buckets are cache entries: no Bucket and no backing slab exists until a
  // size class is first allocated. Callers pass the original request size on
  // release so this first phase does not need a pointer-to-page lookup table.
  void refill(uint32_t size_class);

  X3PageAllocator pages_;
  std::array<Bucket, x3_size_class_count()> buckets_;
  X3MemoryCounter bucket_stats_;
  X3MemoryCounter large_stats_;
};

} // namespace xlang3::memory
