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

#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/set_object.h"
#include "runtime/modules/thread/runtime_lock.h"

#include <cstddef>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif
#include <chrono>
#include <cstdio>
#include <limits>
#include <unordered_map>

namespace xlang3 {
// Shared internal negative-finalizer cache, implemented beside final release.
bool class_has_release_finalizer(const Value& klass_value);

namespace {

// TEMPORARY DIAGNOSTIC ONLY: removed before any accepted engine build.
struct GcPhaseDiagnostic {
  using Clock = std::chrono::steady_clock;
  const char* group;
  Clock::time_point last = Clock::now();
  explicit GcPhaseDiagnostic(const char* name) : group(name) {}
  void mark(const char* phase, uint64_t count = 0) {
    const auto end = Clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - last).count();
    std::fprintf(stderr, "XLANG3_GC_PHASE %s %s %lld %llu\n", group, phase,
        static_cast<long long>(ns), static_cast<unsigned long long>(count));
    last = Clock::now(); // Exclude diagnostic output from the next phase.
  }
};

bool gc_plain_storage_kind(ObjectKind kind) {
  return kind == ObjectKind::Instance || kind == ObjectKind::List ||
      kind == ObjectKind::Dict || kind == ObjectKind::Tuple ||
      kind == ObjectKind::Set || kind == ObjectKind::Cell;
}

bool gc_plain_callback_boundary(Object* object) {
  if ((object->gc_tracking_state.load(std::memory_order_relaxed) &
       kObjectWeakrefFlagsMask) != 0) return true;
  if (object->kind == ObjectKind::Dict)
    return reinterpret_cast<DictObject*>(object)->backing_module != nullptr;
  if (object->kind != ObjectKind::Instance) return false;
  const auto* instance = reinterpret_cast<InstanceObject*>(object);
  return instance->native_data != nullptr || instance->native_owner != nullptr ||
      instance->native_data_cast != nullptr || instance->native_data_cleanup != nullptr ||
      instance->native_data_clear != nullptr || instance->native_gc_traverse != nullptr ||
      !instance->native_gc_references.empty() || instance->native_gc_registered ||
      instance->native_get_attr != nullptr || instance->native_set_attr != nullptr ||
      instance->native_delete_attr != nullptr || instance->native_data_truthy != nullptr ||
      !instance->native_type.empty() || class_has_release_finalizer(instance->klass);
}

bool gc_plain_atomic_leaf(Object* object) {
  if ((object->gc_tracking_state.load(std::memory_order_relaxed) &
       kObjectWeakrefFlagsMask) != 0) return false;
  switch (object->kind) {
    case ObjectKind::String:
    case ObjectKind::Bytes:
    case ObjectKind::ByteArray:
    case ObjectKind::BigInt:
    case ObjectKind::Complex:
      return true;
    case ObjectKind::Tuple:
      return reinterpret_cast<TupleObject*>(object)->items.empty();
    default:
      return false;
  }
}

template <class Visit>
void gc_visit_plain_storage(Object* object, Visit&& visit) {
  switch (object->kind) {
    case ObjectKind::List:
      for (auto& value : reinterpret_cast<ListObject*>(object)->items) visit(value);
      break;
    case ObjectKind::Tuple:
      for (auto& value : reinterpret_cast<TupleObject*>(object)->items) visit(value);
      break;
    case ObjectKind::Set:
      for (auto& value : reinterpret_cast<SetObject*>(object)->items) visit(value);
      break;
    case ObjectKind::Dict:
      for (auto& entry : reinterpret_cast<DictObject*>(object)->entries) {
        visit(entry.first); visit(entry.second);
      }
      break;
    case ObjectKind::Cell:
      visit(reinterpret_cast<CellObject*>(object)->value);
      break;
    case ObjectKind::Instance: {
      auto* instance = reinterpret_cast<InstanceObject*>(object);
      // Class metadata stays owned until final release. All classes retain
      // their snapshot pins until every collected instance has been released.
      visit(instance->mapping_storage);
      visit(instance->sequence_storage);
      for (auto& entry : instance->attrs) visit(entry.second);
      for (uint32_t index = 0; index < instance->slot_count; ++index)
        visit(instance_slot_at(instance, index));
      break;
    }
    default: break;
  }
}

template <class Visit>
void gc_visit_plain_sequence_owned_runs(const std::vector<Value>& items, Visit&& visit) {
  size_t position = 0;
  while (position < items.size()) {
    const Value& first = items[position++];
    if (first.tag != ValueTag::Object || first.as.obj == nullptr ||
        (first.flags & kXlangValueBorrowedRefFlag) != 0) continue;
    Object* target = first.as.obj;
    uint64_t multiplicity = 1;
#if defined(_M_X64) || defined(__x86_64__)
    // x86-64 guarantees SSE2. Compare object representations without copying
    // Values or creating owners. Exact equality includes the borrowed flag;
    // the admitted first Value owns a reference, so every matching lane does.
    // A full four-Value bounds check precedes every unaligned load. Other
    // architectures and differing/tail Values use the same scalar accounting.
    static_assert(sizeof(Value) == 16 && offsetof(Value, tag) == 0 &&
        offsetof(Value, flags) == 4 && offsetof(Value, as) == 8,
        "Packed GC scanning requires the complete 16-byte Value representation");
    const __m128i needle = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&first));
    while (items.size() - position >= 4) {
      const auto* current = items.data() + position;
      const __m128i a = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(current)), needle);
      const __m128i b = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(current + 1)), needle);
      const __m128i c = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(current + 2)), needle);
      const __m128i d = _mm_xor_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(current + 3)), needle);
      const __m128i difference = _mm_or_si128(_mm_or_si128(a, b), _mm_or_si128(c, d));
      if (_mm_movemask_epi8(_mm_cmpeq_epi8(difference, _mm_setzero_si128())) != 0xffff) break;
      position += 4;
      multiplicity += 4; // Each equal owning Value counts; this is not deduplication.
    }
#endif
    while (position < items.size() && items[position].tag == ValueTag::Object &&
        items[position].flags == first.flags && items[position].as.obj == target) {
      ++position; ++multiplicity;
    }
    visit(target, multiplicity);
  }
}

// Repeated owning references still contribute their full multiplicity to
// trial deletion. Group consecutive equal targets only to avoid repeating the
// graph-index hash lookup and reachability work for every list/tuple item.
// Borrowed Values contribute no ownership; the clear phase visits every Value.
template <class Visit>
void gc_visit_plain_owned_runs(Object* object, Visit&& visit) {
  if (object->kind == ObjectKind::List) {
    gc_visit_plain_sequence_owned_runs(reinterpret_cast<ListObject*>(object)->items, visit);
    return;
  }
  if (object->kind == ObjectKind::Tuple) {
    gc_visit_plain_sequence_owned_runs(reinterpret_cast<TupleObject*>(object)->items, visit);
    return;
  }
  Object* target = nullptr;
  uint64_t multiplicity = 0;
  gc_visit_plain_storage(object, [&](const Value& value) {
    if (value.tag != ValueTag::Object || value.as.obj == nullptr ||
        (value.flags & kXlangValueBorrowedRefFlag) != 0) return;
    if (target == value.as.obj) {
      ++multiplicity;
    } else {
      if (target != nullptr) visit(target, multiplicity);
      target = value.as.obj;
      multiplicity = 1;
    }
  });
  if (target != nullptr) visit(target, multiplicity);
}

void gc_publish_empty_plain_storage(Object* object) {
  switch (object->kind) {
    case ObjectKind::List:
      reinterpret_cast<ListObject*>(object)->items.clear(); break;
    case ObjectKind::Tuple: {
      auto* tuple = reinterpret_cast<TupleObject*>(object);
      tuple->items.clear(); tuple_object_complete_construction(*tuple); break;
    }
    case ObjectKind::Dict: {
      auto* dict = reinterpret_cast<DictObject*>(object);
      dict->entries.clear(); dict->integer_index.clear(); dict->string_index.clear();
      dict->runtime_hash_index.clear(); dict->runtime_hash_indexed_entry_count = 0;
      dict->intrinsic_hash_checked_entry_count = 0; dict->intrinsic_hash_keys_only = true;
      dict->indexed_entry_count = 0; dict->index_has_other_keys = false;
      dict->index_has_non_string_keys = false; break;
    }
    case ObjectKind::Set: {
      auto* set = reinterpret_cast<SetObject*>(object);
      set->items.clear(); set->item_hashes.clear(); set->hash_cached = false;
      set_note_content_change(*set); set->membership_index_version = 0;
      set->membership_index_mask = 0; set->membership_index_heads.clear();
      set->membership_index_next.clear(); set->membership_identity_heads.clear();
      set->membership_identity_next.clear(); break;
    }
    case ObjectKind::Instance: {
      auto* instance = reinterpret_cast<InstanceObject*>(object);
      instance->attrs.clear(); instance->has_separate_attribute_storage = false; break;
    }
    default: break;
  }
}

uint64_t gc_collect_plain_cycles() {
#if !XLANG3_VM_GLOBAL_LOCK
  // Snapshot locking protects registry entries, not mutations of object edges.
  // Unsynchronized embedding/global-lock-disabled builds need a separate
  // collection contract; never infer reachability from a racing graph.
  return 0;
#else
  GcPhaseDiagnostic diagnostic("plain");
  struct Node {
    Object* object;
    size_t pin;
    uint64_t incoming = 0;
    size_t last_parent = std::numeric_limits<size_t>::max();
    bool unsafe = false;
    bool reachable = false;
  };
  // Release opaque metadata pins after leaving VM serialization. There are no
  // graph/field reads then, and the outer collect guard rejects reentrant GC.
  std::vector<Value> snapshot;
  uint64_t collected = 0;
  {
    XlangRuntimeExecutionGuard execution_lock;
    diagnostic.mark("lock");
    snapshot = gc_snapshot_tracked_objects();
    diagnostic.mark("snapshot", snapshot.size());
    std::vector<Node> nodes;
    std::unordered_map<Object*, size_t> index;
    nodes.reserve(snapshot.size()); index.reserve(snapshot.size());
    for (size_t pin = 0; pin < snapshot.size(); ++pin) {
      Object* object = snapshot[pin].as.obj;
      if (!gc_plain_storage_kind(object->kind)) continue;
      index.emplace(object, nodes.size());
      nodes.push_back(Node{object, pin, 0, std::numeric_limits<size_t>::max(),
          gc_plain_callback_boundary(object), false});
    }
    diagnostic.mark("nodes", nodes.size());
    std::vector<std::vector<size_t>> parents(nodes.size()), children(nodes.size());
    for (size_t source = 0; source < nodes.size(); ++source) {
      gc_visit_plain_owned_runs(nodes[source].object, [&](Object* object, uint64_t multiplicity) {
        const auto target = index.find(object);
        if (target == index.end()) {
          if (!gc_plain_atomic_leaf(object)) nodes[source].unsafe = true;
          return;
        }
        auto& node = nodes[target->second];
        node.incoming += multiplicity; // Count every owning edge, not just distinct targets.
        if (node.last_parent != source) {
          parents[target->second].push_back(source);
          children[source].push_back(target->second);
          node.last_parent = source;
        }
      });
    }
    diagnostic.mark("owning_edges");
    std::vector<size_t> pending;
    pending.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].unsafe) pending.push_back(i);
    // A parent that could release a finalizer/native/opaque owner must also
    // remain intact. Reverse propagation makes the clear phase callback-free,
    // instead of emptying a parent before a child's finalizer observes it.
    while (!pending.empty()) {
      const size_t child = pending.back(); pending.pop_back();
      for (size_t parent : parents[child]) {
        if (!nodes[parent].unsafe) { nodes[parent].unsafe = true; pending.push_back(parent); }
      }
    }
    diagnostic.mark("unsafe_propagation");
    for (size_t i = 0; i < nodes.size(); ++i) {
      auto& node = nodes[i];
      const uint64_t references = node.object->refcnt.load(std::memory_order_acquire);
      // Exactly one reference belongs to our snapshot. Uncounted ownership
      // from modules, frames, functions and native payloads remains external.
      // Inconsistent counts fail closed; borrowed Values are never subtracted.
      if (node.unsafe || references != node.incoming + 1) {
        node.reachable = true; pending.push_back(i);
      }
    }
    diagnostic.mark("root_counts", pending.size());
    while (!pending.empty()) {
      const size_t source = pending.back(); pending.pop_back();
      // Reachability needs each target once, unlike incoming ownership.
      // Reuse the exact adjacency built above instead of rereading potentially
      // large repeated-reference containers and probing their graph indexes.
      for (size_t target : children[source]) {
        if (!nodes[target].reachable) {
          nodes[target].reachable = true; pending.push_back(target);
        }
      }
    }
    diagnostic.mark("reachability");
    size_t retired_count = 0;
    for (const auto& node : nodes) if (!node.reachable) {
      ++collected;
      gc_visit_plain_storage(node.object, [&](const Value&) { ++retired_count; });
    }
    diagnostic.mark("retirement_count", collected);
    std::vector<Value> retired, candidate_pins;
    retired.reserve(retired_count); candidate_pins.reserve(static_cast<size_t>(collected));
    // Allocate everything before mutation. Move ownership rather than adding
    // pins, publish the entire empty graph and coherent indexes, then retire.
    // The specialized weakref/native collector runs separately, before this
    // snapshot exists, so its exact refcount checks do not include our pins.
    for (const auto& node : nodes) if (!node.reachable) {
      candidate_pins.push_back(std::move(snapshot[node.pin]));
      gc_visit_plain_storage(node.object, [&](Value& value) { retired.push_back(std::move(value)); });
      gc_publish_empty_plain_storage(node.object);
    }
    retired.clear();
    candidate_pins.clear(); // Class/opaque snapshot pins still prevent cleanup callbacks.
    diagnostic.mark("retirement", collected);
  }
  diagnostic.mark("graph_teardown");
  snapshot.clear(); // No registry lock or cached graph references across cleanup/reentry.
  diagnostic.mark("snapshot_release");
  return collected;
#endif
}

} // namespace
} // namespace xlang3
