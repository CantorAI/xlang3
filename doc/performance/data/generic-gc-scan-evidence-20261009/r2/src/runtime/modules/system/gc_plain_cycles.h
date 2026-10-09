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

#include <limits>
#include <unordered_map>

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
    std::unordered_map<Object*, size_t> index;
    nodes.reserve(snapshot.size()); index.reserve(snapshot.size());
    for (size_t pin = 0; pin < snapshot.size(); ++pin) {
      Object* object = snapshot[pin].as.obj;
      if (!gc_plain_storage_kind(object->kind)) continue;
      index.emplace(object, nodes.size());
      nodes.push_back(Node{object, pin, 0, std::numeric_limits<size_t>::max(),
          gc_plain_callback_boundary(object), false});
    }
    std::vector<std::vector<size_t>> parents(nodes.size());
    for (size_t source = 0; source < nodes.size(); ++source) {
      gc_visit_plain_storage(nodes[source].object, [&](const Value& value) {
        if (value.tag != ValueTag::Object || value.as.obj == nullptr ||
            (value.flags & kXlangValueBorrowedRefFlag) != 0) return;
        const auto target = index.find(value.as.obj);
        if (target == index.end()) {
          if (!gc_plain_atomic_leaf(value.as.obj)) nodes[source].unsafe = true;
          return;
        }
        auto& node = nodes[target->second];
        ++node.incoming; // Multiplicity matters, even for repeated list items.
        if (node.last_parent != source) {
          parents[target->second].push_back(source); node.last_parent = source;
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
      for (size_t parent : parents[child]) {
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
      gc_visit_plain_storage(nodes[source].object, [&](const Value& value) {
        if (value.tag != ValueTag::Object || value.as.obj == nullptr ||
            (value.flags & kXlangValueBorrowedRefFlag) != 0) return;
        const auto target = index.find(value.as.obj);
        if (target != index.end() && !nodes[target->second].reachable) {
          nodes[target->second].reachable = true; pending.push_back(target->second);
        }
      });
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
