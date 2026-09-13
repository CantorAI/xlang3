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

constexpr uint32_t xlang_perf_object_kind_count = static_cast<uint32_t>(ObjectKind::TypeParam) + 1;
constexpr uint32_t xlang_perf_monitoring_event_count = 18;

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
  std::array<std::atomic_uint64_t, xlang_perf_monitoring_event_count> monitoring_dispatches{};
  std::array<std::atomic_uint64_t, xlang_perf_monitoring_event_count> monitoring_callbacks{};
  std::atomic_uint64_t frame_snapshot_calls{0};
  std::atomic_uint64_t frame_snapshot_frames{0};
  std::atomic_uint64_t frame_locals_materializations{0};
  std::atomic_uint64_t frame_refresh_calls{0};
  std::atomic_uint64_t frame_refresh_items{0};
};

XlangPerfCounters& xlang_perf_counters();
void xlang_perf_set_enabled(bool enabled);
void xlang_perf_reset();
std::string xlang_perf_report();
void xlang_perf_count_native_name(const std::string& name, bool fast);
const char* xlang_perf_object_kind_name(ObjectKind kind);

XLANG3_HOT_INLINE bool xlang_perf_enabled() {
  return g_xlang_perf_enabled.load(std::memory_order_relaxed);
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

XLANG3_HOT_INLINE void xlang_perf_count_monitoring_event(int64_t event, bool callback) {
  if (!xlang_perf_enabled() || event <= 0) return;
  uint32_t index = 0;
  uint64_t bits = static_cast<uint64_t>(event);
  while ((bits & 1u) == 0u && index < xlang_perf_monitoring_event_count) {
    bits >>= 1u;
    ++index;
  }
  if (index >= xlang_perf_monitoring_event_count) return;
  auto& counters = callback
      ? xlang_perf_counters().monitoring_callbacks
      : xlang_perf_counters().monitoring_dispatches;
  counters[index].fetch_add(1, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void xlang_perf_count_frame_snapshot(uint64_t frames) {
  if (!xlang_perf_enabled()) return;
  auto& counters = xlang_perf_counters();
  counters.frame_snapshot_calls.fetch_add(1, std::memory_order_relaxed);
  counters.frame_snapshot_frames.fetch_add(frames, std::memory_order_relaxed);
}

XLANG3_HOT_INLINE void xlang_perf_count_frame_locals_materialization() {
  if (xlang_perf_enabled()) {
    xlang_perf_counters().frame_locals_materializations.fetch_add(1, std::memory_order_relaxed);
  }
}

XLANG3_HOT_INLINE void xlang_perf_count_frame_refresh(uint64_t items) {
  if (!xlang_perf_enabled()) return;
  auto& counters = xlang_perf_counters();
  counters.frame_refresh_calls.fetch_add(1, std::memory_order_relaxed);
  counters.frame_refresh_items.fetch_add(items, std::memory_order_relaxed);
}


} // namespace xlang3
