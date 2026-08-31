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

#include <cstdint>
#include <string>
#include <string_view>

namespace xlang3 {

inline constexpr uint16_t kCp437HighTable[128] = {
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0,
};

XLANG3_HOT_INLINE bool cp437_append_utf8(uint32_t codepoint, std::string& out) {
  if (codepoint <= 0x7f) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else if (codepoint <= 0x10ffff) {
    out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
  } else {
    return false;
  }
  return true;
}

XLANG3_HOT_INLINE uint32_t cp437_decode_utf8_codepoint(std::string_view text, size_t width) {
  if (width == 1) {
    return static_cast<unsigned char>(text[0]);
  }
  uint32_t codepoint = static_cast<unsigned char>(text[0]) & ((1u << (7 - width)) - 1u);
  for (size_t i = 1; i < width; ++i) {
    codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i]) & 0x3fu);
  }
  return codepoint;
}

XLANG3_HOT_INLINE void cp437_append_backslash_escape(uint32_t codepoint, std::string& out) {
  static constexpr char digits[] = "0123456789abcdef";
  if (codepoint <= 0xff) {
    out += "\\x";
    out.push_back(digits[(codepoint >> 4) & 0x0f]);
    out.push_back(digits[codepoint & 0x0f]);
  } else if (codepoint <= 0xffff) {
    out += "\\u";
    for (int shift = 12; shift >= 0; shift -= 4) {
      out.push_back(digits[(codepoint >> shift) & 0x0f]);
    }
  } else {
    out += "\\U";
    for (int shift = 28; shift >= 0; shift -= 4) {
      out.push_back(digits[(codepoint >> shift) & 0x0f]);
    }
  }
}

XLANG3_HOT_INLINE bool cp437_encode_text(std::string_view text, std::string_view errors, std::string& out, std::string& error) {
  out.clear();
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    const size_t width = utf8_codepoint_width(ch);
    const uint32_t codepoint = width == 0 || i + width > text.size() ? ch : cp437_decode_utf8_codepoint(text.substr(i), width);
    const size_t advance = width == 0 ? 1 : width;
    if (codepoint <= 0x7f) {
      out.push_back(static_cast<char>(codepoint));
      i += advance;
      continue;
    }
    bool found = false;
    for (size_t index = 0; index < 128; ++index) {
      if (kCp437HighTable[index] == codepoint) {
        out.push_back(static_cast<char>(0x80 + index));
        found = true;
        break;
      }
    }
    if (found || errors == "ignore") {
      i += advance;
    } else if (errors == "replace") {
      out.push_back('?');
      i += advance;
    } else if (errors == "backslashreplace") {
      cp437_append_backslash_escape(codepoint, out);
      i += advance;
    } else {
      error = "charmap codec can't encode character";
      return false;
    }
  }
  return true;
}

XLANG3_HOT_INLINE std::string cp437_decode_bytes(std::string_view text) {
  std::string out;
  out.reserve(text.size() * 2);
  for (unsigned char ch : text) {
    if (ch < 0x80) {
      out.push_back(static_cast<char>(ch));
    } else {
      cp437_append_utf8(kCp437HighTable[ch - 0x80], out);
    }
  }
  return out;
}

} // namespace xlang3
