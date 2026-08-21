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

#include "xlang3/compiler.h"
#include "xlang3/value.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace xlang3 {

constexpr uint32_t xlang_perf_object_kind_count = 25;

struct XlangPerfCounters {
  std::atomic_bool enabled{false};
  std::array<std::atomic_uint64_t, xlang_perf_object_kind_count> object_allocations{};
  std::array<std::atomic_uint64_t, xlang_perf_object_kind_count> object_final_releases{};
  std::array<std::atomic_uint64_t, xlang_perf_object_kind_count> value_incref{};
  std::array<std::atomic_uint64_t, xlang_perf_object_kind_count> value_decref{};
  std::atomic_uint64_t native_calls{0};
  std::atomic_uint64_t native_fast_calls{0};
  std::atomic_uint64_t native_cached_fast_calls{0};
  std::atomic_uint64_t store_local_moves{0};
  std::atomic_uint64_t store_local_copies{0};
};

XlangPerfCounters& xlang_perf_counters();
void xlang_perf_set_enabled(bool enabled);
void xlang_perf_reset();
std::string xlang_perf_report();
const char* xlang_perf_object_kind_name(ObjectKind kind);

XLANG3_HOT_INLINE bool xlang_perf_enabled() {
  return xlang_perf_counters().enabled.load(std::memory_order_relaxed);
}

XLANG3_HOT_INLINE uint32_t xlang_perf_kind_index(ObjectKind kind) {
  return static_cast<uint32_t>(kind);
}

XLANG3_HOT_INLINE void xlang_perf_count_object_alloc(ObjectKind kind) {
  if (!xlang_perf_enabled()) {
    return;
  }
  xlang_perf_counters().object_allocations[xlang_perf_kind_index(kind)].fetch_add(1, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void xlang_perf_count_object_final_release(ObjectKind kind) {
  if (!xlang_perf_enabled()) {
    return;
  }
  xlang_perf_counters().object_final_releases[xlang_perf_kind_index(kind)].fetch_add(1, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void xlang_perf_count_value_incref(ObjectKind kind) {
  if (!xlang_perf_enabled()) {
    return;
  }
  xlang_perf_counters().value_incref[xlang_perf_kind_index(kind)].fetch_add(1, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void xlang_perf_count_value_decref(ObjectKind kind) {
  if (!xlang_perf_enabled()) {
    return;
  }
  xlang_perf_counters().value_decref[xlang_perf_kind_index(kind)].fetch_add(1, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void xlang_perf_count_native_call(bool fast) {
  if (!xlang_perf_enabled()) {
    return;
  }
  auto& counters = xlang_perf_counters();
  counters.native_calls.fetch_add(1, std::memory_order_relaxed);
  if (fast) {
    counters.native_fast_calls.fetch_add(1, std::memory_order_relaxed);
  }
}

XLANG3_HOT_INLINE void xlang_perf_count_cached_native_fast_call() {
  if (!xlang_perf_enabled()) {
    return;
  }
  auto& counters = xlang_perf_counters();
  counters.native_calls.fetch_add(1, std::memory_order_relaxed);
  counters.native_fast_calls.fetch_add(1, std::memory_order_relaxed);
  counters.native_cached_fast_calls.fetch_add(1, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void xlang_perf_count_store_local(bool moved) {
  if (!xlang_perf_enabled()) {
    return;
  }
  auto& counters = xlang_perf_counters();
  if (moved) {
    counters.store_local_moves.fetch_add(1, std::memory_order_relaxed);
  } else {
    counters.store_local_copies.fetch_add(1, std::memory_order_relaxed);
  }
}

} // namespace xlang3
