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

#include "xlang3/value.h"

#include <cstdint>

/*
XlangVM instruction cache
Author: Shawn Xiong

Instruction caches are the common per-IR storage used by adaptive execution.
The first layer records the instruction domain, state, and counters. The second
layer records runtime operand-shape specialization, such as "this GetItem keeps
seeing list + int". It must not encode source-expression patterns.

This mirrors CPython-style inline caches: one cache entry per instruction,
adaptive counters, explicit guards, and normal semantics on miss/deopt.
*/

namespace xlang3 {

enum class XlangVMCacheDomain : uint8_t {
  Empty,
  Global,
  Attr,
  GetItem,
  Len,
  Call,
  CallMethod,
  BinaryOp,
};

enum class XlangVMCacheState : uint8_t {
  Empty,
  Adaptive,
  Specialized,
  Disabled,
};

enum class XlangVMSpecializationId : uint16_t {
  None,
  LenObjectKind,
  GetItemListInt,
  GetItemTupleInt,
  GetItemStringInt,
  GetItemBytesInt,
  GetItemByteArrayInt,
  GetItemMemoryViewInt,
};

struct XlangVMInstrCacheCore {
  XlangVMCacheDomain domain = XlangVMCacheDomain::Empty;
  XlangVMCacheState state = XlangVMCacheState::Empty;
  XlangVMSpecializationId specialization = XlangVMSpecializationId::None;
  ObjectKind object_kind = ObjectKind::String;
  uint16_t hit_count = 0;
  uint16_t miss_count = 0;
};

XLANG3_HOT_INLINE void xlang_vm_cache_touch(
    XlangVMInstrCacheCore& cache,
    XlangVMCacheDomain domain) {
  if (cache.domain != domain) {
    cache.domain = domain;
    cache.state = XlangVMCacheState::Adaptive;
    cache.specialization = XlangVMSpecializationId::None;
    cache.object_kind = ObjectKind::String;
    cache.hit_count = 0;
    cache.miss_count = 0;
  }
}

XLANG3_HOT_INLINE void xlang_vm_cache_note_hit(XlangVMInstrCacheCore& cache) {
  if (cache.hit_count != UINT16_MAX) {
    ++cache.hit_count;
  }
}

XLANG3_HOT_INLINE void xlang_vm_cache_note_miss(XlangVMInstrCacheCore& cache) {
  if (cache.miss_count != UINT16_MAX) {
    ++cache.miss_count;
  }
}

XLANG3_HOT_INLINE void xlang_vm_cache_clear(XlangVMInstrCacheCore& cache) {
  cache.domain = XlangVMCacheDomain::Empty;
  cache.state = XlangVMCacheState::Empty;
  cache.specialization = XlangVMSpecializationId::None;
  cache.object_kind = ObjectKind::String;
  cache.hit_count = 0;
  cache.miss_count = 0;
}

XLANG3_HOT_INLINE void xlang_vm_cache_specialize(
    XlangVMInstrCacheCore& cache,
    XlangVMSpecializationId specialization,
    ObjectKind object_kind = ObjectKind::String) {
  cache.state = XlangVMCacheState::Specialized;
  cache.specialization = specialization;
  cache.object_kind = object_kind;
  cache.miss_count = 0;
}

XLANG3_HOT_INLINE void xlang_vm_cache_deopt(XlangVMInstrCacheCore& cache) {
  cache.state = XlangVMCacheState::Adaptive;
  cache.specialization = XlangVMSpecializationId::None;
  cache.object_kind = ObjectKind::String;
  xlang_vm_cache_note_miss(cache);
}

} // namespace xlang3
