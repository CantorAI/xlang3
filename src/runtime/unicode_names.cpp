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

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

namespace xlang3 {

bool unicodedata_lookup_codepoint(std::string_view name, uint32_t& codepoint) {
  struct UnicodeName {
    std::string_view name;
    uint32_t codepoint;
  };
  static constexpr std::array<UnicodeName, 31> names{{
      {"SPACE", 0x0020},
      {"DIGIT ZERO", 0x0030},
      {"DIGIT ONE", 0x0031},
      {"DIGIT TWO", 0x0032},
      {"DIGIT THREE", 0x0033},
      {"DIGIT FOUR", 0x0034},
      {"DIGIT FIVE", 0x0035},
      {"DIGIT SIX", 0x0036},
      {"DIGIT SEVEN", 0x0037},
      {"DIGIT EIGHT", 0x0038},
      {"DIGIT NINE", 0x0039},
      {"LESS-THAN SIGN", 0x003c},
      {"GREATER-THAN SIGN", 0x003e},
      {"LATIN CAPITAL LETTER A", 0x0041},
      {"LATIN SMALL LETTER A", 0x0061},
      {"NO-BREAK SPACE", 0x00a0},
      {"SUPERSCRIPT TWO", 0x00b2},
      {"VULGAR FRACTION THREE QUARTERS", 0x00be},
      {"LATIN CAPITAL LETTER A WITH DIAERESIS", 0x00c4},
      {"LATIN CAPITAL LETTER A WITH RING ABOVE", 0x00c5},
      {"LATIN SMALL LETTER E WITH ACUTE", 0x00e9},
      {"COMBINING ACUTE ACCENT", 0x0301},
      {"COMBINING RING ABOVE", 0x030a},
      {"NARROW NO-BREAK SPACE", 0x202f},
      {"FRACTION SLASH", 0x2044},
      {"ANGSTROM SIGN", 0x212b},
      {"ROMAN NUMERAL FOUR", 0x2163},
      {"CJK UNIFIED IDEOGRAPH-4E2D", 0x4e2d},
      {"ARABIC LIGATURE UIGHUR KIRGHIZ YEH WITH HAMZA ABOVE WITH ALEF MAKSURA ISOLATED FORM", 0xfbf9},
      {"SNAKE", 0x1f40d},
      {"SLIGHTLY SMILING FACE", 0x1f642},
  }};
  std::string canonical(name);
  std::transform(canonical.begin(), canonical.end(), canonical.begin(), [](unsigned char ch) {
    return static_cast<char>(std::toupper(ch));
  });
  if (canonical == "LINE FEED" || canonical == "LF") {
    codepoint = 0x000a;
    return true;
  }
  const auto found = std::find_if(names.begin(), names.end(), [&](const UnicodeName& entry) {
    return entry.name == canonical;
  });
  if (found == names.end()) return false;
  codepoint = found->codepoint;
  return true;
}

} // namespace xlang3
