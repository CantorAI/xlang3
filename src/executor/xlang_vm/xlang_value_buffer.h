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

#include "xlang3/runtime.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <utility>
#include <vector>

/*
XlangVM design note
Author: Shawn Xiong

Purpose:
Inline-first storage for VM locals, registers, and cells.

Performance rule:
Small frames avoid heap allocation; large frames fall back to std::vector-backed
storage without changing the frame access path.

Execution-value rule:
IR names virtual registers. XlangVM allocates one physical register cell for
each function virtual register count, so desktop execution does not spill hot
expressions through heap objects. XlangVMRegister is intentionally separate
from public Value even while it currently stores Value-compatible payloads; the
next optimization layer can add frame-local temps/views behind this boundary
without changing the IR or public runtime ABI.
*/

namespace xlang3 {

static constexpr uint32_t kXlangVMRegisterTempFlag = 0x80000000u;

enum class XlangVMTempKind : uint8_t {
  Empty,
  NativeIntermediate,
};

/*
XlangVMTempValue is the generic placeholder for non-materialized intermediate
results. It deliberately does not encode string/list/etc. operation kinds.
Native/runtime operations can later attach compact op descriptors through
producer_id and payload slots, while the frame remains the lifetime owner.
*/
struct XlangVMTempValue {
  XlangVMTempKind kind = XlangVMTempKind::Empty;
  uint32_t producer_id = 0;
  Value source;
  Value arg0;
  Value arg1;
  uint32_t payload0 = 0;
  uint32_t payload1 = 0;
};

class XlangVMTempArena {
public:
  uint32_t allocate(XlangVMTempValue temp) {
    if (temps_.size() < kInlineCount) {
      temps_.push_back(std::move(temp));
      return static_cast<uint32_t>(temps_.size() - 1);
    }
    overflow_.push_back(std::move(temp));
    return static_cast<uint32_t>(kInlineCount + overflow_.size() - 1);
  }

  XLANG3_HOT_INLINE XlangVMTempValue* get(uint32_t index) {
    if (index < temps_.size()) {
      return &temps_[index];
    }
    index -= static_cast<uint32_t>(kInlineCount);
    if (index < overflow_.size()) {
      return &overflow_[index];
    }
    return nullptr;
  }

  XLANG3_HOT_INLINE const XlangVMTempValue* get(uint32_t index) const {
    if (index < temps_.size()) {
      return &temps_[index];
    }
    index -= static_cast<uint32_t>(kInlineCount);
    if (index < overflow_.size()) {
      return &overflow_[index];
    }
    return nullptr;
  }

  void clear() {
    temps_.clear();
    overflow_.clear();
  }

private:
  static constexpr size_t kInlineCount = 32;

  std::vector<XlangVMTempValue> temps_;
  std::vector<XlangVMTempValue> overflow_;
};

struct XlangVMRegister : Value {
  XlangVMRegister() = default;
  XlangVMRegister(const Value& value) : Value(value) {}
  XlangVMRegister(Value&& value) noexcept : Value(std::move(value)) {}

  XlangVMRegister& operator=(const Value& value) {
    Value::operator=(value);
    return *this;
  }

  XlangVMRegister& operator=(Value&& value) noexcept {
    Value::operator=(std::move(value));
    return *this;
  }

  XLANG3_HOT_INLINE Value& materialized_value() {
    return *this;
  }

  XLANG3_HOT_INLINE const Value& materialized_value() const {
    return *this;
  }

  XLANG3_HOT_INLINE bool is_temp() const {
    return tag == ValueTag::Invalid && (flags & kXlangVMRegisterTempFlag) != 0;
  }

  XLANG3_HOT_INLINE uint32_t temp_index() const {
    return static_cast<uint32_t>(as.i64);
  }

  XLANG3_HOT_INLINE void set_temp(uint32_t index) {
    value_release_if_object(*this);
    tag = ValueTag::Invalid;
    flags = kXlangVMRegisterTempFlag;
    as.i64 = static_cast<int64_t>(index);
  }
};

static_assert(sizeof(XlangVMRegister) == sizeof(Value), "VMRegister must stay Value-layout compatible");

template <typename Cell, size_t InlineCount>
class XlangVMSmallBuffer {
public:
  XlangVMSmallBuffer() = default;

  XlangVMSmallBuffer(size_t size, const Value& fill) : size_(size) {
    if (size_ <= InlineCount) {
      data_ = inline_data();
      uses_inline_ = true;
      for (size_t i = 0; i < size_; ++i) {
        new (data_ + i) Cell(fill);
      }
      return;
    }
    heap_.assign(size_, fill);
    data_ = heap_.data();
  }

  XlangVMSmallBuffer(const XlangVMSmallBuffer&) = delete;
  XlangVMSmallBuffer& operator=(const XlangVMSmallBuffer&) = delete;
  XlangVMSmallBuffer(XlangVMSmallBuffer&& other) noexcept {
    move_from(std::move(other));
  }
  XlangVMSmallBuffer& operator=(XlangVMSmallBuffer&& other) noexcept = delete;

  ~XlangVMSmallBuffer() {
    destroy_inline();
  }

  void reset(size_t size, const Value& fill) {
    if (size <= InlineCount) {
      if (!uses_inline_) {
        heap_.clear();
        data_ = inline_data();
        uses_inline_ = true;
        for (size_t i = 0; i < size; ++i) {
          new (data_ + i) Cell(fill);
        }
        size_ = size;
        return;
      }
      const size_t shared = size < size_ ? size : size_;
      for (size_t i = 0; i < shared; ++i) {
        value_assign_fast(data_[i], fill);
      }
      for (size_t i = shared; i < size; ++i) {
        new (data_ + i) Cell(fill);
      }
      for (size_t i = size; i < size_; ++i) {
        data_[i].~Cell();
      }
      size_ = size;
      return;
    }

    destroy_inline();
    uses_inline_ = false;
    heap_.assign(size, fill);
    data_ = heap_.data();
    size_ = size;
  }

  XLANG3_HOT_INLINE size_t size() const {
    return size_;
  }

  XLANG3_HOT_INLINE Cell* data() {
    return data_;
  }

  XLANG3_HOT_INLINE const Cell* data() const {
    return data_;
  }

  XLANG3_HOT_INLINE Value* value_data() {
    return reinterpret_cast<Value*>(data_);
  }

  XLANG3_HOT_INLINE const Value* value_data() const {
    return reinterpret_cast<const Value*>(data_);
  }

  XLANG3_HOT_INLINE Cell& operator[](size_t index) {
    return data_[index];
  }

  XLANG3_HOT_INLINE const Cell& operator[](size_t index) const {
    return data_[index];
  }

private:
  XLANG3_HOT_INLINE Cell* inline_data() {
    return reinterpret_cast<Cell*>(inline_storage_);
  }

  void move_from(XlangVMSmallBuffer&& other) {
    size_ = other.size_;
    uses_inline_ = other.uses_inline_;
    if (other.uses_inline_) {
      data_ = inline_data();
      for (size_t i = 0; i < size_; ++i) {
        new (data_ + i) Cell(std::move(other.data_[i]));
        other.data_[i].~Cell();
      }
      other.size_ = 0;
      other.data_ = nullptr;
      other.uses_inline_ = false;
      return;
    }

    heap_ = std::move(other.heap_);
    data_ = heap_.data();
    other.size_ = 0;
    other.data_ = nullptr;
  }

  void destroy_inline() {
    if (!uses_inline_) {
      return;
    }
    for (size_t i = 0; i < size_; ++i) {
      data_[i].~Cell();
    }
    size_ = 0;
    data_ = nullptr;
    uses_inline_ = false;
  }

  size_t size_ = 0;
  Cell* data_ = nullptr;
  bool uses_inline_ = false;
  alignas(Cell) std::byte inline_storage_[sizeof(Cell) * InlineCount];
  std::vector<Cell> heap_;
};

using XlangVMSmallValueBuffer = XlangVMSmallBuffer<Value, 64>;
using XlangVMSmallRegisterBuffer = XlangVMSmallBuffer<XlangVMRegister, 128>;
using SmallValueBuffer = XlangVMSmallValueBuffer;

XLANG3_HOT_INLINE bool xlang_vm_register_is_materialized(const XlangVMRegister& reg) {
  return !reg.is_temp();
}

inline bool xlang_vm_materialize_register(
    XlangVMRegister& reg,
    XlangVMTempArena& temps,
    Value& out,
    std::string& error) {
  if (!reg.is_temp()) {
    value_assign_fast(out, reg.materialized_value());
    return true;
  }

  const auto* temp = temps.get(reg.temp_index());
  if (temp == nullptr || temp->kind == XlangVMTempKind::Empty) {
    error = "invalid VM temporary";
    return false;
  }

  error = "VM temporary materializer is not installed";
  return false;
}

} // namespace xlang3
