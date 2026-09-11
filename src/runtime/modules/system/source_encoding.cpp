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
#include "source_encoding.h"
#include <cstdint>

#include <cctype>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace xlang3 {

namespace {

bool has_utf8_bom(std::string_view text) {
  return text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
         static_cast<unsigned char>(text[1]) == 0xbb && static_cast<unsigned char>(text[2]) == 0xbf;
}

std::string_view first_physical_line(std::string_view text, size_t& next) {
  if (next >= text.size()) {
    return {};
  }
  const size_t start = next;
  const size_t end = text.find('\n', start);
  if (end == std::string_view::npos) {
    next = text.size();
    return text.substr(start);
  }
  next = end + 1;
  return text.substr(start, end - start + 1);
}

std::string find_coding_cookie(std::string_view line) {
  const size_t comment = line.find('#');
  if (comment == std::string_view::npos) {
    return {};
  }
  std::string_view rest = line.substr(comment + 1);
  const size_t coding = rest.find("coding");
  if (coding == std::string_view::npos) {
    return {};
  }
  size_t i = coding + 6;
  while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) {
    ++i;
  }
  if (i >= rest.size() || (rest[i] != ':' && rest[i] != '=')) {
    return {};
  }
  ++i;
  while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) {
    ++i;
  }
  const size_t start = i;
  while (i < rest.size()) {
    const unsigned char ch = static_cast<unsigned char>(rest[i]);
    if (!(std::isalnum(ch) || rest[i] == '-' || rest[i] == '_' || rest[i] == '.')) {
      break;
    }
    ++i;
  }
  return std::string(rest.substr(start, i - start));
}

std::string detect_source_encoding(std::string_view bytes, bool bom, std::string& error) {
  size_t next = bom ? 3 : 0;
  const std::string_view first_line = first_physical_line(bytes, next);
  const std::string first_cookie = find_coding_cookie(first_line);
  bool first_line_allows_second_cookie = true;
  for (char ch : first_line) {
    if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\r' || ch == '\n') {
      continue;
    }
    first_line_allows_second_cookie = ch == '#';
    break;
  }
  const std::string second_cookie = first_cookie.empty() && first_line_allows_second_cookie
      ? find_coding_cookie(first_physical_line(bytes, next))
      : "";
  std::string encoding = !first_cookie.empty() ? first_cookie : second_cookie;
  if (encoding.empty()) {
    return bom ? "utf-8-sig" : "utf-8";
  }
  encoding = canonical_python_source_encoding(std::move(encoding));
  if (bom && encoding != "utf-8" && encoding != "utf-8-sig") {
    error = "encoding problem: utf-8 BOM with non-utf-8 coding cookie";
    return {};
  }
  return encoding;
}

bool append_utf8(uint32_t codepoint, std::string& out) {
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

std::string latin1_decode(std::string_view bytes) {
  std::string text;
  text.reserve(bytes.size() * 2);
  for (unsigned char ch : bytes) {
    append_utf8(ch, text);
  }
  return text;
}

bool valid_utf8(std::string_view text) {
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    if (lead < 0x80) {
      ++i;
      continue;
    }
    size_t count = 0;
    uint32_t codepoint = 0;
    uint32_t minimum = 0;
    if ((lead & 0xe0) == 0xc0) {
      count = 2;
      codepoint = lead & 0x1f;
      minimum = 0x80;
    } else if ((lead & 0xf0) == 0xe0) {
      count = 3;
      codepoint = lead & 0x0f;
      minimum = 0x800;
    } else if ((lead & 0xf8) == 0xf0) {
      count = 4;
      codepoint = lead & 0x07;
      minimum = 0x10000;
    } else {
      return false;
    }
    if (i + count > text.size()) return false;
    for (size_t j = 1; j < count; ++j) {
      const unsigned char continuation = static_cast<unsigned char>(text[i + j]);
      if ((continuation & 0xc0) != 0x80) return false;
      codepoint = (codepoint << 6) | (continuation & 0x3f);
    }
    if (codepoint < minimum || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
      return false;
    }
    i += count;
  }
  return true;
}

bool utf16_decode(std::string_view bytes, std::string& text, std::string& error) {
  bool little_endian = true;
  size_t offset = 0;
  if (bytes.size() >= 2) {
    const auto first = static_cast<unsigned char>(bytes[0]);
    const auto second = static_cast<unsigned char>(bytes[1]);
    if (first == 0xff && second == 0xfe) {
      offset = 2;
    } else if (first == 0xfe && second == 0xff) {
      little_endian = false;
      offset = 2;
    }
  }
  if (((bytes.size() - offset) & 1u) != 0) {
    error = "utf-16 codec can't decode truncated data";
    return false;
  }
  text.clear();
  auto unit_at = [&](size_t pos) -> uint16_t {
    const uint16_t a = static_cast<unsigned char>(bytes[pos]);
    const uint16_t b = static_cast<unsigned char>(bytes[pos + 1]);
    return little_endian ? static_cast<uint16_t>(a | (b << 8)) :
                           static_cast<uint16_t>((a << 8) | b);
  };
  while (offset < bytes.size()) {
    uint32_t codepoint = unit_at(offset);
    offset += 2;
    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
      if (offset >= bytes.size()) {
        error = "utf-16 codec can't decode truncated surrogate";
        return false;
      }
      const uint32_t low = unit_at(offset);
      if (low < 0xdc00 || low > 0xdfff) {
        error = "utf-16 codec can't decode invalid surrogate";
        return false;
      }
      offset += 2;
      codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
      error = "utf-16 codec can't decode invalid surrogate";
      return false;
    }
    append_utf8(codepoint, text);
  }
  return true;
}

bool ascii_decode(std::string_view bytes, std::string& text, std::string& error) {
  for (unsigned char ch : bytes) {
    if (ch > 0x7f) {
      error = "ascii codec can't decode byte";
      return false;
    }
  }
  text.assign(bytes);
  return true;
}

bool mbcs_decode(std::string_view bytes, std::string& text, std::string& error) {
#if defined(_WIN32)
  if (bytes.empty()) {
    text.clear();
    return true;
  }
  const int wide_size = MultiByteToWideChar(
      CP_ACP,
      MB_ERR_INVALID_CHARS,
      bytes.data(),
      static_cast<int>(bytes.size()),
      nullptr,
      0);
  if (wide_size <= 0) {
    error = "mbcs codec can't decode byte";
    return false;
  }
  std::wstring wide(static_cast<size_t>(wide_size), L'\0');
  MultiByteToWideChar(
      CP_ACP,
      MB_ERR_INVALID_CHARS,
      bytes.data(),
      static_cast<int>(bytes.size()),
      wide.data(),
      wide_size);
  const int utf8_size = WideCharToMultiByte(
      CP_UTF8,
      0,
      wide.data(),
      wide_size,
      nullptr,
      0,
      nullptr,
      nullptr);
  if (utf8_size <= 0) {
    error = "mbcs codec can't decode byte";
    return false;
  }
  text.assign(static_cast<size_t>(utf8_size), '\0');
  WideCharToMultiByte(
      CP_UTF8,
      0,
      wide.data(),
      wide_size,
      text.data(),
      utf8_size,
      nullptr,
      nullptr);
  return true;
#else
  text.assign(bytes);
  return true;
#endif
}

} // namespace

bool decode_mbcs_bytes(std::string_view bytes, std::string& text, std::string& error) {
  return mbcs_decode(bytes, text, error);
}

bool encode_gbk_text(std::string_view text, std::string& bytes, std::string& error) {
#if defined(_WIN32)
  if (text.empty()) {
    bytes.clear();
    return true;
  }
  const int wide_size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (wide_size <= 0) {
    error = "utf-8 codec can't decode byte";
    return false;
  }
  std::wstring wide(static_cast<size_t>(wide_size), L'\0');
  MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wide.data(), wide_size);
  BOOL used_default = FALSE;
  const int byte_size = WideCharToMultiByte(
      936, WC_NO_BEST_FIT_CHARS, wide.data(), wide_size, nullptr, 0, nullptr, &used_default);
  if (byte_size <= 0 || used_default) {
    error = "gbk codec can't encode character";
    return false;
  }
  bytes.assign(static_cast<size_t>(byte_size), '\0');
  used_default = FALSE;
  if (WideCharToMultiByte(
          936, WC_NO_BEST_FIT_CHARS, wide.data(), wide_size,
          bytes.data(), byte_size, nullptr, &used_default) <= 0 || used_default) {
    error = "gbk codec can't encode character";
    return false;
  }
  return true;
#else
  error = "gbk text encoding is unavailable on this platform";
  return false;
#endif
}

bool decode_gbk_bytes(std::string_view bytes, std::string& text, std::string& error) {
#if defined(_WIN32)
  if (bytes.empty()) {
    text.clear();
    return true;
  }
  const int wide_size = MultiByteToWideChar(
      936, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
  if (wide_size <= 0) {
    error = "gbk codec can't decode byte";
    return false;
  }
  std::wstring wide(static_cast<size_t>(wide_size), L'\0');
  MultiByteToWideChar(
      936, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), wide.data(), wide_size);
  const int utf8_size = WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), wide_size, nullptr, 0, nullptr, nullptr);
  if (utf8_size <= 0) {
    error = "gbk codec can't decode byte";
    return false;
  }
  text.assign(static_cast<size_t>(utf8_size), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), wide_size, text.data(), utf8_size, nullptr, nullptr);
  return true;
#else
  error = "gbk source decoding is unavailable on this platform";
  return false;
#endif
}

std::string canonical_python_source_encoding(std::string name) {
  for (char& ch : name) {
    if (ch == '-' || ch == ' ' || ch == '.') {
      ch = '_';
    } else {
      ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
  }
  if (name == "utf8" || name == "u8" || name == "cp65001") {
    return "utf-8";
  }
  if (name == "locale") {
    return "utf-8";
  }
  if (name == "utf_8" || name == "utf_8_sig") {
    return name == "utf_8_sig" ? "utf-8-sig" : "utf-8";
  }
  if (name == "utf16" || name == "utf_16") {
    return "utf-16";
  }
  if (name == "latin1" || name == "latin_1" || name == "iso8859_1" || name == "iso_8859_1" || name == "8859") {
    return "iso-8859-1";
  }
  if (name == "mbcs" || name == "ansi") {
    return "mbcs";
  }
  if (name == "gbk" || name == "cp936" || name == "ms936") {
    return "gbk";
  }
  if (name == "us_ascii" || name == "646") {
    return "ascii";
  }
  return name;
}

bool decode_python_source_bytes(std::string_view bytes, PythonSourceText& out, std::string& error) {
  error.clear();
  const bool bom = has_utf8_bom(bytes);
  const std::string encoding = detect_source_encoding(bytes, bom, error);
  if (!error.empty()) {
    return false;
  }
  std::string_view payload = bom ? bytes.substr(3) : bytes;
  out.encoding = encoding == "utf-8-sig" ? "utf-8" : encoding;
  if (encoding == "utf-8" || encoding == "utf-8-sig") {
    if (!valid_utf8(payload)) {
      error = "utf-8 codec can't decode byte";
      return false;
    }
    out.text.assign(payload);
    return true;
  }
  if (encoding == "iso-8859-1") {
    out.text = latin1_decode(payload);
    return true;
  }
  if (encoding == "utf-16") {
    return utf16_decode(payload, out.text, error);
  }
  if (encoding == "ascii") {
    return ascii_decode(payload, out.text, error);
  }
  if (encoding == "mbcs") {
    return mbcs_decode(payload, out.text, error);
  }
  if (encoding == "gbk") {
    return decode_gbk_bytes(payload, out.text, error);
  }
  error = "unknown source encoding: " + encoding;
  return false;
}

bool decode_python_source_bytes_as(
    std::string_view bytes,
    std::string encoding,
    PythonSourceText& out,
    std::string& error) {
  error.clear();
  encoding = canonical_python_source_encoding(std::move(encoding));
  const bool bom = has_utf8_bom(bytes);
  std::string_view payload = bom ? bytes.substr(3) : bytes;
  out.encoding = encoding == "utf-8-sig" ? "utf-8" : encoding;
  if (encoding == "utf-8" || encoding == "utf-8-sig") {
    if (!valid_utf8(payload)) {
      error = "utf-8 codec can't decode byte";
      return false;
    }
    out.text.assign(payload);
    return true;
  }
  if (encoding == "iso-8859-1") {
    out.text = latin1_decode(payload);
    return true;
  }
  if (encoding == "utf-16") {
    return utf16_decode(bytes, out.text, error);
  }
  if (encoding == "ascii") {
    return ascii_decode(payload, out.text, error);
  }
  if (encoding == "mbcs") {
    return mbcs_decode(payload, out.text, error);
  }
  if (encoding == "gbk") {
    return decode_gbk_bytes(payload, out.text, error);
  }
  error = "unknown source encoding: " + encoding;
  return false;
}

} // namespace xlang3
