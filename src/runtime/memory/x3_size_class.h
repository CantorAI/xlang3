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

#include <cstddef>
#include <cstdint>

namespace xlang3::memory {

static constexpr uint32_t kInvalidSizeClass = UINT32_MAX;

inline constexpr size_t kX3SizeClasses[] = {
    16, 32, 48, 64, 96, 128, 192, 256,
    384, 512, 768, 1024, 1536, 2048, 4096,
};

inline constexpr uint32_t x3_size_class_count() {
  return static_cast<uint32_t>(sizeof(kX3SizeClasses) / sizeof(kX3SizeClasses[0]));
}

inline size_t x3_size_class_bytes(uint32_t index) {
  return index < x3_size_class_count() ? kX3SizeClasses[index] : 0;
}

inline uint32_t x3_size_class_for(size_t size) {
  if (size == 0) {
    size = 1;
  }
  if (size <= 16) return 0;
  if (size <= 32) return 1;
  if (size <= 48) return 2;
  if (size <= 64) return 3;
  if (size <= 96) return 4;
  if (size <= 128) return 5;
  if (size <= 192) return 6;
  if (size <= 256) return 7;
  if (size <= 384) return 8;
  if (size <= 512) return 9;
  if (size <= 768) return 10;
  if (size <= 1024) return 11;
  if (size <= 1536) return 12;
  if (size <= 2048) return 13;
  if (size <= 4096) return 14;
  return kInvalidSizeClass;
}

} // namespace xlang3::memory
