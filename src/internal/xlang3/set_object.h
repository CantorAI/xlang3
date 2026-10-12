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

#include <string>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace xlang3 {

class Runtime;

struct SetObject {
  Object header;
  bool frozen = false;
  mutable bool hash_cached = false;
  mutable size_t cached_hash = 0;
  std::vector<Value> items;
  std::vector<size_t> item_hashes;
  // Keep ordered entries authoritative; this lazy index only narrows the
  // candidates for membership, like CPython's hash-table probe. Small sets
  // stay allocation-free. Arbitrary mutation invalidates the index; appends
  // update a valid index until geometric growth requires one rebuild.
  uint64_t content_version = 1;
  mutable uint64_t membership_index_version = 0;
  mutable size_t membership_index_mask = 0;
  mutable std::vector<size_t> membership_index_heads;
  mutable std::vector<size_t> membership_index_next;
  mutable std::vector<size_t> membership_identity_heads;
  mutable std::vector<size_t> membership_identity_next;
};

constexpr size_t kSetMembershipIndexThreshold = 8;
constexpr size_t kSetMembershipIndexEmpty = std::numeric_limits<size_t>::max();

XLANG3_HOT_INLINE void set_note_content_change(SetObject& set) {
  ++set.content_version;
  if (set.content_version == 0) {
    set.content_version = 1;
    set.membership_index_version = 0;
  }
}

XLANG3_HOT_INLINE bool set_prepare_membership_index(const SetObject& set) {
  const size_t item_count = set.items.size();
  if (item_count < kSetMembershipIndexThreshold ||
      set.item_hashes.size() != item_count ||
      item_count > std::numeric_limits<size_t>::max() / 2) {
    return false;
  }
  if (set.membership_index_version != set.content_version) {
    size_t capacity = 8;
    while (capacity < item_count * 2) {
      if (capacity > std::numeric_limits<size_t>::max() / 2) return false;
      capacity *= 2;
    }
    auto& heads = set.membership_index_heads;
    auto& next = set.membership_index_next;
    auto& identity_heads = set.membership_identity_heads;
    auto& identity_next = set.membership_identity_next;
    heads.assign(capacity, kSetMembershipIndexEmpty);
    next.resize(item_count);
    identity_heads.assign(capacity, kSetMembershipIndexEmpty);
    identity_next.assign(item_count, kSetMembershipIndexEmpty);
    const size_t mask = capacity - 1;
    for (size_t index = 0; index < item_count; ++index) {
      const size_t bucket = set.item_hashes[index] & mask;
      next[index] = heads[bucket];
      heads[bucket] = index;
      const Value& item = set.items[index];
      if (item.tag == ValueTag::Object && item.as.obj != nullptr) {
        // Some native helpers construct a set before they have a Runtime to
        // call an object's Python __hash__. Retain an identity chain too, so
        // membership of that exact object keeps working when its later,
        // runtime-aware hash differs from the construction-time hash.
        const size_t identity = static_cast<size_t>(
            reinterpret_cast<uintptr_t>(item.as.obj) >> 3) & mask;
        identity_next[index] = identity_heads[identity];
        identity_heads[identity] = index;
      }
    }
    set.membership_index_mask = mask;
    set.membership_index_version = set.content_version;
  }
  return true;
}

XLANG3_HOT_INLINE void set_note_append(SetObject& set) {
  const uint64_t previous_version = set.content_version;
  const size_t index = set.items.size() - 1;
  set_note_content_change(set);
  if (set.membership_index_version != previous_version ||
      set.membership_index_heads.empty() ||
      set.membership_index_next.size() != index ||
      set.membership_identity_next.size() != index ||
      set.items.size() > set.membership_index_heads.size() / 2) return;
  // A growing visited set must not rebuild all buckets after every insertion.
  // Preserve ordered entries and both hash/identity chains in O(1); exceeding
  // the load limit leaves the index invalid for a geometric rebuild on demand.
  const size_t bucket = set.item_hashes[index] & set.membership_index_mask;
  set.membership_index_next.push_back(set.membership_index_heads[bucket]);
  set.membership_index_heads[bucket] = index;
  const Value& item = set.items[index];
  size_t identity_next = kSetMembershipIndexEmpty;
  if (item.tag == ValueTag::Object && item.as.obj != nullptr) {
    const size_t identity = static_cast<size_t>(
        reinterpret_cast<uintptr_t>(item.as.obj) >> 3) & set.membership_index_mask;
    identity_next = set.membership_identity_heads[identity];
    set.membership_identity_heads[identity] = index;
  }
  set.membership_identity_next.push_back(identity_next);
  set.membership_index_version = set.content_version;
}

XLANG3_HOT_INLINE size_t set_membership_index_first(
    const SetObject& set, size_t hash) {
  return set.membership_index_heads[hash & set.membership_index_mask];
}

XLANG3_HOT_INLINE size_t set_membership_index_next(
    const SetObject& set, size_t index) {
  return set.membership_index_next[index];
}

XLANG3_HOT_INLINE size_t set_membership_identity_first(
    const SetObject& set, const Value& value) {
  const size_t identity = static_cast<size_t>(
      reinterpret_cast<uintptr_t>(value.as.obj) >> 3);
  return set.membership_identity_heads[identity & set.membership_index_mask];
}

XLANG3_HOT_INLINE size_t set_membership_identity_next(
    const SetObject& set, size_t index) {
  return set.membership_identity_next[index];
}

struct SetIteratorObject {
  Object header;
  Value source;
  uint64_t index = 0;
};

XLANG3_HOT_INLINE SetObject* value_as_set(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::Set) {
    return nullptr;
  }
  return reinterpret_cast<SetObject*>(value.as.obj);
}

XLANG3_HOT_INLINE SetIteratorObject* value_as_set_iterator(const Value& value) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::SetIterator) {
    return nullptr;
  }
  return reinterpret_cast<SetIteratorObject*>(value.as.obj);
}

void set_release_object(Object* object);
std::string set_to_string(const Value& value);
bool set_truthy(const Value& value);

bool set_get_iter(const Value& object, Value& out, std::string& error);
bool set_iter_next(Value& iterator, bool& done, Value& out, std::string& error);
bool set_len(const Value& value, Value& out, std::string& error);
bool set_add(Value& set, const Value& item, std::string& error);
bool set_add_runtime(Runtime& runtime, Value& set, const Value& item,
                     std::string& error);
bool set_union_values(const Value& left, const Value& right, Value& out,
                      std::string& error);

} // namespace xlang3
