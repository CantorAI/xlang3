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
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <vector>

namespace xlang3::gc_detail {

// This index owns no objects. Its nodes must remain at the same positions and
// snapshot pins must retain their objects through the last lookup. A bounded
// dense table replaces one hash-node allocation per supported storage object.
template <class Node>
class PlainNodeIndex {
 public:
  static constexpr size_t missing = std::numeric_limits<size_t>::max();

  PlainNodeIndex(const std::vector<Node>& nodes, size_t snapshot_count)
      : nodes_(nodes) {
    if (nodes.empty()) return;
    uint64_t maximum = 0;
    for (const auto& node : nodes) {
      const uint64_t slot = tracking_slot(node.object);
      // Division avoids overflowing 4*snapshot_count; the sentinel must not
      // become an allocation size. Sparse registries keep pointer hashing.
      if (slot == kGcObjectIndexNone || slot / 4 >= snapshot_count ||
          slot >= std::numeric_limits<size_t>::max()) {
        prepare_sparse(); return;
      }
      maximum = std::max(maximum, slot);
    }
    dense_.assign(static_cast<size_t>(maximum) + 1, missing);
    for (size_t node = 0; node < nodes.size(); ++node) {
      const uint64_t slot = tracking_slot(nodes[node].object);
      if (slot >= dense_.size()) { prepare_sparse(); return; }
      dense_[static_cast<size_t>(slot)] = node;
    }
    dense_enabled_ = true;
  }

  size_t find(Object* object) {
    if (dense_enabled_) {
      const uint64_t slot = tracking_slot(object);
      if (slot < dense_.size()) {
        const size_t node = dense_[static_cast<size_t>(slot)];
        if (node < nodes_.size() && nodes_[node].object == object) return node;
      }
      // Snapshot ownership does not freeze registry indexes: another thread's
      // zero-ref cache teardown may compact the registry. On any mismatch,
      // rebuild exact pointer membership before interpreting an owning edge.
      // All lookups/allocations finish before graph mutation or pin release.
      prepare_sparse();
    }
    const auto found = sparse_.find(object);
    return found == sparse_.end() ? missing : found->second;
  }

  bool uses_dense_table() const { return dense_enabled_; }

 private:
  static uint64_t tracking_slot(Object* object) {
    return object->gc_tracking_state.load(std::memory_order_relaxed) & kGcObjectIndexMask;
  }

  void prepare_sparse() {
    sparse_.reserve(nodes_.size());
    for (size_t node = 0; node < nodes_.size(); ++node)
      sparse_.emplace(nodes_[node].object, node);
    dense_enabled_ = false;
    dense_.clear();
  }

  const std::vector<Node>& nodes_;
  std::vector<size_t> dense_;
  std::unordered_map<Object*, size_t> sparse_;
  bool dense_enabled_ = false;
};

} // namespace xlang3::gc_detail
