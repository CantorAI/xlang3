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
#include "gc_plain_node_index.h"

#include <cstddef>
#if defined(_M_X64) || defined(__x86_64__)
#include <emmintrin.h>
#endif
#include <limits>

namespace xlang3 {
// Shared internal negative-finalizer cache, implemented beside final release.
bool class_has_release_finalizer(const Value& klass_value);

namespace {

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
    snapshot = gc_snapshot_tracked_objects();
    std::vector<Node> nodes;
    nodes.reserve(snapshot.size());
    for (size_t pin = 0; pin < snapshot.size(); ++pin) {
      Object* object = snapshot[pin].as.obj;
      if (!gc_plain_storage_kind(object->kind)) continue;
      nodes.push_back(Node{object, pin, 0, std::numeric_limits<size_t>::max(),
          gc_plain_callback_boundary(object), false});
    }
    gc_detail::PlainNodeIndex<Node> index(nodes, snapshot.size());
    // One contiguous graph buffer avoids a tiny heap allocation for each
    // parent/child adjacency vector. Index links retain unique topology in
    // both directions; incoming ownership above still counts every Value.
    struct Edge { size_t source, target, next_parent, next_child; };
    constexpr size_t no_edge = std::numeric_limits<size_t>::max();
    std::vector<Edge> edges;
    edges.reserve(nodes.size());
    std::vector<size_t> parent_heads(nodes.size(), no_edge), child_heads(nodes.size(), no_edge);
    for (size_t source = 0; source < nodes.size(); ++source) {
      gc_visit_plain_owned_runs(nodes[source].object, [&](Object* object, uint64_t multiplicity) {
        // Non-storage metadata and atomic empty tuples cannot be graph nodes;
        // avoid a pointless dense-table miss/fallback for these ordinary leaves.
        const size_t target = gc_plain_storage_kind(object->kind) &&
            !(object->kind == ObjectKind::Tuple && gc_plain_atomic_leaf(object))
            ? index.find(object) : gc_detail::PlainNodeIndex<Node>::missing;
        if (target == gc_detail::PlainNodeIndex<Node>::missing) {
          if (!gc_plain_atomic_leaf(object)) nodes[source].unsafe = true;
          return;
        }
        auto& node = nodes[target];
        node.incoming += multiplicity; // Count every owning edge, not just distinct targets.
        if (node.last_parent != source) {
          const size_t edge = edges.size();
          edges.push_back(Edge{source, target, parent_heads[target], child_heads[source]});
          parent_heads[target] = edge;
          child_heads[source] = edge;
          node.last_parent = source;
        }
      });
    }
    std::vector<size_t> pending;
    pending.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].unsafe) pending.push_back(i);
    // A parent that could release a finalizer/native/opaque owner must also
    // remain intact. Reverse propagation makes the clear phase callback-free,
    // instead of emptying a parent before a child's finalizer observes it.
    while (!pending.empty()) {
      const size_t child = pending.back(); pending.pop_back();
      for (size_t edge = parent_heads[child]; edge != no_edge; edge = edges[edge].next_parent) {
        const size_t parent = edges[edge].source;
        if (!nodes[parent].unsafe) { nodes[parent].unsafe = true; pending.push_back(parent); }
      }
    }
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
    while (!pending.empty()) {
      const size_t source = pending.back(); pending.pop_back();
      // Reachability needs each target once, unlike incoming ownership.
      // Reuse the exact adjacency built above instead of rereading potentially
      // large repeated-reference containers and probing their graph indexes.
      for (size_t edge = child_heads[source]; edge != no_edge; edge = edges[edge].next_child) {
        const size_t target = edges[edge].target;
        if (!nodes[target].reachable) {
          nodes[target].reachable = true; pending.push_back(target);
        }
      }
    }
    size_t retired_count = 0;
    for (const auto& node : nodes) if (!node.reachable) {
      ++collected;
      gc_visit_plain_storage(node.object, [&](const Value&) { ++retired_count; });
    }
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
  }
  snapshot.clear(); // No registry lock or cached graph references across cleanup/reentry.
  return collected;
#endif
}

} // namespace
} // namespace xlang3
