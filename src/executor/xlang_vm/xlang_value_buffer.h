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
#include <new>
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
*/

namespace xlang3 {

class XlangVMSmallValueBuffer {
public:
  XlangVMSmallValueBuffer() = default;

  XlangVMSmallValueBuffer(size_t size, const Value& fill) : size_(size) {
    if (size_ <= kInlineCount) {
      data_ = inline_data();
      uses_inline_ = true;
      for (size_t i = 0; i < size_; ++i) {
        new (data_ + i) Value(fill);
      }
      return;
    }
    heap_.assign(size_, fill);
    data_ = heap_.data();
  }

  XlangVMSmallValueBuffer(const XlangVMSmallValueBuffer&) = delete;
  XlangVMSmallValueBuffer& operator=(const XlangVMSmallValueBuffer&) = delete;
  XlangVMSmallValueBuffer(XlangVMSmallValueBuffer&& other) noexcept {
    move_from(std::move(other));
  }
  XlangVMSmallValueBuffer& operator=(XlangVMSmallValueBuffer&& other) noexcept = delete;

  ~XlangVMSmallValueBuffer() {
    destroy_inline();
  }

  void reset(size_t size, const Value& fill) {
    if (size <= kInlineCount) {
      if (!uses_inline_) {
        heap_.clear();
        data_ = inline_data();
        uses_inline_ = true;
        for (size_t i = 0; i < size; ++i) {
          new (data_ + i) Value(fill);
        }
        size_ = size;
        return;
      }
      const size_t shared = size < size_ ? size : size_;
      for (size_t i = 0; i < shared; ++i) {
        value_assign_fast(data_[i], fill);
      }
      for (size_t i = shared; i < size; ++i) {
        new (data_ + i) Value(fill);
      }
      for (size_t i = size; i < size_; ++i) {
        data_[i].~Value();
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

  XLANG3_HOT_INLINE Value* data() {
    return data_;
  }

  XLANG3_HOT_INLINE const Value* data() const {
    return data_;
  }

  XLANG3_HOT_INLINE Value& operator[](size_t index) {
    return data_[index];
  }

  XLANG3_HOT_INLINE const Value& operator[](size_t index) const {
    return data_[index];
  }

private:
  static constexpr size_t kInlineCount = 64;

  XLANG3_HOT_INLINE Value* inline_data() {
    return reinterpret_cast<Value*>(inline_storage_);
  }

  void move_from(XlangVMSmallValueBuffer&& other) {
    size_ = other.size_;
    uses_inline_ = other.uses_inline_;
    if (other.uses_inline_) {
      data_ = inline_data();
      for (size_t i = 0; i < size_; ++i) {
        new (data_ + i) Value(std::move(other.data_[i]));
        other.data_[i].~Value();
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
      data_[i].~Value();
    }
    size_ = 0;
    data_ = nullptr;
    uses_inline_ = false;
  }

  size_t size_ = 0;
  Value* data_ = nullptr;
  bool uses_inline_ = false;
  alignas(Value) std::byte inline_storage_[sizeof(Value) * kInlineCount];
  std::vector<Value> heap_;
};

using SmallValueBuffer = XlangVMSmallValueBuffer;

} // namespace xlang3
