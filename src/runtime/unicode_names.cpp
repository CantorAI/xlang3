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
#include "xlang3/builtins.h"
#include "xlang3/unicode_data.h"
#include "xlang3/value.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace xlang3 {

bool unicodedata_lookup_codepoint(std::string_view name, uint32_t& codepoint) {
  std::string value;
  if (!unicode_data_lookup(name, value) || utf8_codepoint_count(value) != 1) return false;
  const size_t width = utf8_codepoint_width(static_cast<unsigned char>(value[0]));
  codepoint = width == 1 ? static_cast<unsigned char>(value[0]) :
      static_cast<unsigned char>(value[0]) & ((1u << (7 - width)) - 1u);
  for (size_t index = 1; index < width; ++index) {
    codepoint = (codepoint << 6) | (static_cast<unsigned char>(value[index]) & 0x3fu);
  }
  return true;
}

} // namespace xlang3
