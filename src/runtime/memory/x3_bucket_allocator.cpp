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
#include "runtime/memory/x3_bucket_allocator.h"

#include <algorithm>

namespace xlang3::memory {

namespace {

constexpr size_t kBucketSlabBytes = 16 * 1024;

} // namespace

X3BucketAllocator::X3BucketAllocator() = default;

X3BucketAllocator::~X3BucketAllocator() {
  for (auto& bucket : buckets_) {
    for (auto& slab : bucket.slabs) {
      pages_.release(slab.memory, slab.bytes, alignof(std::max_align_t));
    }
  }
}

void* X3BucketAllocator::allocate(size_t bytes) {
  const uint32_t size_class = x3_size_class_for(bytes);
  if (size_class == kInvalidSizeClass) {
    void* mem = pages_.allocate(bytes == 0 ? 1 : bytes, alignof(std::max_align_t));
    x3_memory_note_alloc(large_stats_, bytes);
    return mem;
  }

  auto& bucket = buckets_[size_class];
  if (bucket.free_list == nullptr) {
    refill(size_class);
  } else {
    ++bucket_stats_.reuse_count;
  }

  FreeSlot* slot = bucket.free_list;
  bucket.free_list = slot->next;
  x3_memory_note_alloc(bucket_stats_, x3_size_class_bytes(size_class));
  return slot;
}

void X3BucketAllocator::release(void* ptr, size_t bytes) noexcept {
  if (ptr == nullptr) {
    return;
  }

  const uint32_t size_class = x3_size_class_for(bytes);
  if (size_class == kInvalidSizeClass) {
    pages_.release(ptr, bytes == 0 ? 1 : bytes, alignof(std::max_align_t));
    x3_memory_note_free(large_stats_, bytes);
    return;
  }

  auto* slot = static_cast<FreeSlot*>(ptr);
  auto& bucket = buckets_[size_class];
  slot->next = bucket.free_list;
  bucket.free_list = slot;
  x3_memory_note_free(bucket_stats_, x3_size_class_bytes(size_class));
}

void X3BucketAllocator::refill(uint32_t size_class) {
  const size_t slot_size = x3_size_class_bytes(size_class);
  const size_t slot_count = std::max<size_t>(1, kBucketSlabBytes / slot_size);
  const size_t slab_bytes = slot_size * slot_count;
  void* memory = pages_.allocate(slab_bytes, alignof(std::max_align_t));

  auto& bucket = buckets_[size_class];
  bucket.slabs.push_back(Slab{memory, slab_bytes, size_class});
  ++bucket_stats_.slab_count;

  auto* bytes = static_cast<unsigned char*>(memory);
  for (size_t i = 0; i < slot_count; ++i) {
    auto* slot = reinterpret_cast<FreeSlot*>(bytes + i * slot_size);
    slot->next = bucket.free_list;
    bucket.free_list = slot;
  }
}

} // namespace xlang3::memory
