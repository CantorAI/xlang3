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
#include "runtime/memory/x3_object_pool.h"

#include <algorithm>
#include <stdexcept>

namespace xlang3::memory {

namespace {

constexpr size_t kObjectPoolSlabBytes = 16 * 1024;

size_t align_up(size_t value, size_t alignment) {
  if (alignment == 0) {
    return value;
  }
  const size_t mask = alignment - 1;
  return (value + mask) & ~mask;
}

} // namespace

X3ObjectPoolManager::~X3ObjectPoolManager() {
  for (auto& entry : pools_) {
    auto& pool = entry.second;
    for (auto& slab : pool.slabs) {
      pages_.release(slab.memory, slab.bytes, pool.desc.object_align);
    }
  }
}

bool X3ObjectPoolManager::register_pool(X3ObjectPoolDesc desc) {
  if (desc.kind == 0 || desc.object_size == 0 || desc.object_align == 0) {
    return false;
  }
  if (pools_.find(desc.kind) != pools_.end()) {
    return false;
  }

  Pool pool;
  pool.desc = desc;
  pool.slot_size = std::max<size_t>(sizeof(FreeSlot), align_up(desc.object_size, desc.object_align));
  pools_.emplace(desc.kind, std::move(pool));
  return true;
}

bool X3ObjectPoolManager::has_pool(uint32_t kind) const {
  return pools_.find(kind) != pools_.end();
}

X3ObjectPoolManager::PoolHandle X3ObjectPoolManager::register_or_get_pool(X3ObjectPoolDesc desc) {
  auto it = pools_.find(desc.kind);
  if (it == pools_.end()) {
    if (!register_pool(desc)) {
      return {};
    }
    it = pools_.find(desc.kind);
  }
  return PoolHandle{desc.kind, &it->second};
}

void* X3ObjectPoolManager::allocate(uint32_t kind) {
  Pool& pool = require_pool(kind);
  return allocate(PoolHandle{kind, &pool});
}

void* X3ObjectPoolManager::allocate(PoolHandle handle) {
  auto* pool_ptr = static_cast<Pool*>(handle.pool);
  if (pool_ptr == nullptr) {
    throw std::runtime_error("xlang3 object pool handle is not registered");
  }
  Pool& pool = *pool_ptr;
  if (pool.free_list == nullptr) {
    refill(pool);
  } else {
    ++stats_.reuse_count;
  }

  FreeSlot* slot = pool.free_list;
  pool.free_list = slot->next;
  x3_memory_note_alloc(stats_, pool.slot_size);
  return slot;
}

void X3ObjectPoolManager::release(uint32_t kind, void* ptr) noexcept {
  if (ptr == nullptr) {
    return;
  }
  auto it = pools_.find(kind);
  if (it == pools_.end()) {
    return;
  }
  release(PoolHandle{kind, &it->second}, ptr);
}

void X3ObjectPoolManager::release(PoolHandle handle, void* ptr) noexcept {
  if (ptr == nullptr || handle.pool == nullptr) {
    return;
  }

  Pool& pool = *static_cast<Pool*>(handle.pool);
  auto* slot = static_cast<FreeSlot*>(ptr);
  slot->next = pool.free_list;
  pool.free_list = slot;
  x3_memory_note_free(stats_, pool.slot_size);
}

X3ObjectPoolManager::Pool& X3ObjectPoolManager::require_pool(uint32_t kind) {
  auto it = pools_.find(kind);
  if (it == pools_.end()) {
    throw std::runtime_error("xlang3 object pool kind is not registered");
  }
  return it->second;
}

void X3ObjectPoolManager::refill(Pool& pool) {
  const size_t slot_count = std::max<size_t>(1, kObjectPoolSlabBytes / pool.slot_size);
  const size_t slab_bytes = pool.slot_size * slot_count;
  void* memory = pages_.allocate(slab_bytes, pool.desc.object_align);
  pool.slabs.push_back(Slab{memory, slab_bytes});
  ++stats_.slab_count;

  auto* bytes = static_cast<unsigned char*>(memory);
  for (size_t i = 0; i < slot_count; ++i) {
    auto* slot = reinterpret_cast<FreeSlot*>(bytes + i * pool.slot_size);
    slot->next = pool.free_list;
    pool.free_list = slot;
  }
}

} // namespace xlang3::memory
