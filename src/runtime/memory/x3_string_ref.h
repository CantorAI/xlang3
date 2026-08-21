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

#include "runtime/memory/x3_temp_arena.h"

#include <cstring>
#include <cstdint>
#include <string>
#include <string_view>

namespace xlang3::memory {

struct X3StringView {
  const char* data = nullptr;
  uint32_t size = 0;
};

struct X3StringBuffer {
  char* data = nullptr;
  uint32_t size = 0;
};

inline X3StringView x3_string_view(const char* data, uint32_t size) {
  return X3StringView{data, size};
}

inline X3StringView x3_string_view(std::string_view value) {
  return X3StringView{value.data(), static_cast<uint32_t>(value.size())};
}

inline std::string_view x3_to_string_view(X3StringView value) {
  return std::string_view(value.data == nullptr ? "" : value.data, value.size);
}

inline bool x3_is_ascii_space(char ch) {
  switch (ch) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\f':
    case '\v':
      return true;
    default:
      return false;
  }
}

inline X3StringView x3_trim_ascii(X3StringView value) {
  uint32_t begin = 0;
  uint32_t end = value.size;
  while (begin < end && x3_is_ascii_space(value.data[begin])) {
    ++begin;
  }
  while (end > begin && x3_is_ascii_space(value.data[end - 1])) {
    --end;
  }
  return X3StringView{value.data + begin, end - begin};
}

inline X3StringBuffer x3_copy_string(X3TempArena& arena, X3StringView value) {
  auto* data = static_cast<char*>(arena.allocate(value.size + 1, alignof(char)));
  if (value.size != 0) {
    std::memcpy(data, value.data, value.size);
  }
  data[value.size] = '\0';
  return X3StringBuffer{data, value.size};
}

} // namespace xlang3::memory
