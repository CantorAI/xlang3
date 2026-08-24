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
#include "xlang3/perf_counters.h"

#include <sstream>

namespace xlang3 {

namespace {

XlangPerfCounters g_perf_counters;

uint64_t load_counter(const std::atomic_uint64_t& counter) {
  return counter.load(std::memory_order_relaxed);
}

} // namespace

XlangPerfCounters& xlang_perf_counters() {
  return g_perf_counters;
}

void xlang_perf_set_enabled(bool enabled) {
  g_perf_counters.enabled.store(enabled, std::memory_order_relaxed);
}

void xlang_perf_reset() {
  for (auto& counter : g_perf_counters.object_allocations) {
    counter.store(0, std::memory_order_relaxed);
  }
  for (auto& counter : g_perf_counters.object_final_releases) {
    counter.store(0, std::memory_order_relaxed);
  }
  for (auto& counter : g_perf_counters.value_incref) {
    counter.store(0, std::memory_order_relaxed);
  }
  for (auto& counter : g_perf_counters.value_decref) {
    counter.store(0, std::memory_order_relaxed);
  }
  g_perf_counters.native_calls.store(0, std::memory_order_relaxed);
  g_perf_counters.native_fast_calls.store(0, std::memory_order_relaxed);
  g_perf_counters.native_cached_fast_calls.store(0, std::memory_order_relaxed);
  g_perf_counters.store_local_moves.store(0, std::memory_order_relaxed);
  g_perf_counters.store_local_copies.store(0, std::memory_order_relaxed);
}

const char* xlang_perf_object_kind_name(ObjectKind kind) {
  switch (kind) {
    case ObjectKind::String: return "String";
    case ObjectKind::Bytes: return "Bytes";
    case ObjectKind::ByteArray: return "ByteArray";
    case ObjectKind::MemoryView: return "MemoryView";
    case ObjectKind::Slice: return "Slice";
    case ObjectKind::Tuple: return "Tuple";
    case ObjectKind::List: return "List";
    case ObjectKind::Dict: return "Dict";
    case ObjectKind::DictKeysView: return "DictKeysView";
    case ObjectKind::DictValuesView: return "DictValuesView";
    case ObjectKind::DictItemsView: return "DictItemsView";
    case ObjectKind::Set: return "Set";
    case ObjectKind::DictIterator: return "DictIterator";
    case ObjectKind::SetIterator: return "SetIterator";
    case ObjectKind::Range: return "Range";
    case ObjectKind::RangeIterator: return "RangeIterator";
    case ObjectKind::SequenceIterator: return "SequenceIterator";
    case ObjectKind::Generator: return "Generator";
    case ObjectKind::AsyncGeneratorAwaitable: return "AsyncGeneratorAwaitable";
    case ObjectKind::Module: return "Module";
    case ObjectKind::Cell: return "Cell";
    case ObjectKind::Function: return "Function";
    case ObjectKind::NativeFunction: return "NativeFunction";
    case ObjectKind::Class: return "Class";
    case ObjectKind::Instance: return "Instance";
    case ObjectKind::BoundMethod: return "BoundMethod";
    case ObjectKind::StaticMethod: return "StaticMethod";
    case ObjectKind::ClassMethod: return "ClassMethod";
    case ObjectKind::Super: return "Super";
    case ObjectKind::SlotDescriptor: return "SlotDescriptor";
    case ObjectKind::Property: return "Property";
    case ObjectKind::File: return "File";
    case ObjectKind::TypeParam: return "TypeParam";
  }
  return "Unknown";
}

std::string xlang_perf_report() {
  std::ostringstream out;
  out << "perf: native_calls=" << load_counter(g_perf_counters.native_calls)
      << " fast=" << load_counter(g_perf_counters.native_fast_calls)
      << " cached_fast=" << load_counter(g_perf_counters.native_cached_fast_calls) << "\n";
  out << "perf: store_local moves=" << load_counter(g_perf_counters.store_local_moves)
      << " copies=" << load_counter(g_perf_counters.store_local_copies) << "\n";
  out << "perf: objects kind alloc final_release incref decref\n";
  for (uint32_t i = 1; i < xlang_perf_object_kind_count; ++i) {
    const auto kind = static_cast<ObjectKind>(i);
    const uint64_t alloc = load_counter(g_perf_counters.object_allocations[i]);
    const uint64_t final_release = load_counter(g_perf_counters.object_final_releases[i]);
    const uint64_t incref = load_counter(g_perf_counters.value_incref[i]);
    const uint64_t decref = load_counter(g_perf_counters.value_decref[i]);
    if (alloc == 0 && final_release == 0 && incref == 0 && decref == 0) {
      continue;
    }
    out << "perf: " << xlang_perf_object_kind_name(kind)
        << " " << alloc
        << " " << final_release
        << " " << incref
        << " " << decref << "\n";
  }
  return out.str();
}

} // namespace xlang3
