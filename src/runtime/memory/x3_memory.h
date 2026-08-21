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

#include "runtime/memory/x3_bucket_allocator.h"
#include "runtime/memory/x3_object_pool.h"
#include "runtime/memory/x3_temp_arena.h"

namespace xlang3::memory {

class X3MemoryManager {
public:
  X3MemoryManager() = default;

  X3ObjectPoolManager& object_pools() { return object_pools_; }
  const X3ObjectPoolManager& object_pools() const { return object_pools_; }

  X3BucketAllocator& buckets() { return buckets_; }
  const X3BucketAllocator& buckets() const { return buckets_; }

  X3MemoryStats stats() const;

private:
  X3ObjectPoolManager object_pools_;
  X3BucketAllocator buckets_;
};

} // namespace xlang3::memory
