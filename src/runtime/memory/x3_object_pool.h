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
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace xlang3::memory {

enum class X3ObjectKind : uint32_t {
  String = 1,
  List,
  Tuple,
  Dict,
  Set,
  Bytes,
  MemoryView,
  Function,
  NativeFunction,
  Class,
  Instance,
  Module,
  Iterator,
  Generator,
  Exception,
  UserBase = 1024,
};

struct X3ObjectPoolDesc {
  uint32_t kind = 0;
  uint32_t object_size = 0;
  uint32_t object_align = 0;
};

class X3ObjectPoolManager {
public:
  struct PoolHandle {
    uint32_t kind = 0;
    void* pool = nullptr;
  };

  X3ObjectPoolManager() = default;
  ~X3ObjectPoolManager();

  X3ObjectPoolManager(const X3ObjectPoolManager&) = delete;
  X3ObjectPoolManager& operator=(const X3ObjectPoolManager&) = delete;

  bool register_pool(X3ObjectPoolDesc desc);
  bool has_pool(uint32_t kind) const;
  PoolHandle register_or_get_pool(X3ObjectPoolDesc desc);

  void* allocate(uint32_t kind);
  void* allocate(PoolHandle handle);
  void release(uint32_t kind, void* ptr) noexcept;
  void release(PoolHandle handle, void* ptr) noexcept;

  const X3MemoryCounter& stats() const { return stats_; }

private:
  struct FreeSlot {
    FreeSlot* next = nullptr;
  };

  struct Slab {
    void* memory = nullptr;
    size_t bytes = 0;
  };

  struct Pool {
    X3ObjectPoolDesc desc;
    size_t slot_size = 0;
    FreeSlot* free_list = nullptr;
    std::vector<Slab> slabs;
  };

  // Pools are registered by object kind and allocate slabs lazily. Registering
  // String/List/Dict/etc. defines the fixed object shape, but it does not
  // reserve object storage until allocate(kind) first needs a slot.
  Pool& require_pool(uint32_t kind);
  void refill(Pool& pool);

  X3PageAllocator pages_;
  std::unordered_map<uint32_t, Pool> pools_;
  X3MemoryCounter stats_;
};

inline uint32_t x3_object_kind_id(X3ObjectKind kind) {
  return static_cast<uint32_t>(kind);
}

} // namespace xlang3::memory
