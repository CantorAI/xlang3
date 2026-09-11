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

#include "xlang3/cp437_codec.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/mapping.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "source_encoding.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif
#include <string_view>

namespace xlang3 {

namespace {

std::string normalize_encoding(std::string name) {
  std::string normalized;
  bool separator = false;
  for (unsigned char ch : name) {
    if (std::isalnum(ch)) {
      if (separator && !normalized.empty()) normalized.push_back('_');
      normalized.push_back(static_cast<char>(std::tolower(ch)));
      separator = false;
    } else if (ch == '.') {
      normalized.push_back('.');
      separator = false;
    } else {
      separator = true;
    }
  }
  return normalized;
}

std::string canonical_encoding(std::string name) {
  name = normalize_encoding(std::move(name));
  if (name == "utf8" || name == "u8" || name == "cp65001") {
    return "utf_8";
  }
  if (name == "utf_8_sig") {
    return "utf_8_sig";
  }
  if (name == "latin1" || name == "latin_1" || name == "iso8859_1" || name == "iso_8859_1" || name == "8859") {
    return "latin_1";
  }
  if (name == "gbk" || name == "cp936" || name == "ms936") {
    return "gbk";
  }
  if (name == "us_ascii" || name == "646") {
    return "ascii";
  }
  if (name == "437" || name == "cp437" || name == "ibm437") {
    return "cp437";
  }
  if (name == "idna") {
    return "idna";
  }
  if (name == "hex_codec") {
    return "hex";
  }
  return name;
}

bool value_text(const Value& value, std::string& out) {
  if (auto* str = value_as_string(value)) {
    out = string_object_to_string(*str);
    return true;
  }
  return false;
}

bool value_bytes_text(const Value& value, std::string& out) {
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    return true;
  }
  return false;
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

uint32_t decode_utf8_codepoint(std::string_view text, size_t width) {
  if (width == 1) {
    return static_cast<unsigned char>(text[0]);
  }
  uint32_t codepoint = static_cast<unsigned char>(text[0]) & ((1u << (7 - width)) - 1u);
  for (size_t i = 1; i < width; ++i) {
    codepoint = (codepoint << 6) | (static_cast<unsigned char>(text[i]) & 0x3fu);
  }
  return codepoint;
}

void append_ascii_backslash_escape(uint32_t codepoint, std::string& out) {
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

std::string latin1_encode_text(Runtime& runtime, std::string_view text, const std::string& errors, std::string& error) {
  std::string encoded;
  encoded.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    const size_t width = utf8_codepoint_width(ch);
    const uint32_t codepoint = width == 0 || i + width > text.size() ? ch : decode_utf8_codepoint(text.substr(i), width);
    const size_t advance = width == 0 ? 1 : width;
    if (codepoint <= 0xff) {
      encoded.push_back(static_cast<char>(codepoint));
      i += advance;
    } else if (errors == "ignore") {
      i += advance;
    } else if (errors == "replace") {
      encoded.push_back('?');
      i += advance;
    } else if (errors == "backslashreplace") {
      append_ascii_backslash_escape(codepoint, encoded);
      i += advance;
    } else if (errors == "xmlcharrefreplace") {
      encoded += "&#" + std::to_string(codepoint) + ";";
      i += advance;
    } else if (errors == "surrogateescape" && codepoint >= 0xdc80 && codepoint <= 0xdcff) {
      encoded.push_back(static_cast<char>(codepoint - 0xdc00));
      i += advance;
    } else {
      error = "latin-1 codec can't encode character";
      runtime.raise_class_error("UnicodeEncodeError", error);
      return {};
    }
  }
  return encoded;
}

std::string latin1_decode_text(std::string_view text) {
  std::string decoded;
  decoded.reserve(text.size() * 2);
  for (unsigned char ch : text) {
    append_utf8(ch, decoded);
  }
  return decoded;
}

std::string ascii_encode_text(Runtime& runtime, std::string_view text, const std::string& errors, std::string& error) {
  std::string encoded;
  encoded.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch < 128) {
      encoded.push_back(static_cast<char>(ch));
      ++i;
      continue;
    }
    const size_t width = utf8_codepoint_width(ch);
    const uint32_t codepoint = width == 0 || i + width > text.size() ? ch : decode_utf8_codepoint(text.substr(i), width);
    const size_t advance = width == 0 ? 1 : width;
    if (errors == "surrogateescape" && codepoint >= 0xdc80 && codepoint <= 0xdcff) {
      encoded.push_back(static_cast<char>(codepoint - 0xdc00));
      i += advance;
    } else if (errors == "ignore") {
      i += advance;
    } else if (errors == "replace") {
      encoded.push_back('?');
      i += advance;
    } else if (errors == "backslashreplace") {
      append_ascii_backslash_escape(codepoint, encoded);
      i += advance;
    } else if (errors == "xmlcharrefreplace") {
      encoded += "&#" + std::to_string(codepoint) + ";";
      i += advance;
    } else {
      std::string escaped;
      append_ascii_backslash_escape(codepoint, escaped);
      error = "'ascii' codec can't encode character '" + escaped + "' in position " +
          std::to_string(i) + ": ordinal not in range(128)";
      runtime.raise_class_error("UnicodeEncodeError", error);
      return {};
    }
  }
  return encoded;
}

std::string ascii_decode_text(Runtime& runtime, std::string_view text, const std::string& errors, std::string& error) {
  std::string decoded;
  decoded.reserve(text.size());
  for (unsigned char ch : text) {
    if (ch < 128) {
      decoded.push_back(static_cast<char>(ch));
    } else if (errors == "ignore") {
      continue;
    } else if (errors == "replace") {
      decoded += "\xef\xbf\xbd";
    } else if (errors == "surrogateescape") {
      append_utf8(0xdc00u + ch, decoded);
    } else if (errors == "backslashreplace") {
      static constexpr char digits[] = "0123456789abcdef";
      decoded += "\\x";
      decoded.push_back(digits[ch >> 4]);
      decoded.push_back(digits[ch & 0x0f]);
    } else {
      error = "ascii codec can't decode byte";
      runtime.raise_class_error("UnicodeDecodeError", error);
      return {};
    }
  }
  return decoded;
}

std::string normalized_errors(const Value* args, uint32_t argc, uint32_t index) {
  if (index >= argc) {
    return "strict";
  }
  auto* errors_value = value_as_string(args[index]);
  if (errors_value == nullptr) {
    return "strict";
  }
  return normalize_encoding(string_object_to_string(*errors_value));
}

int hex_value(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + ch - 'a';
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + ch - 'A';
  }
  return -1;
}

bool hex_encode(const std::string& data, Value& out) {
  static constexpr char digits[] = "0123456789abcdef";
  std::string hex;
  hex.reserve(data.size() * 2);
  for (unsigned char ch : data) {
    hex.push_back(digits[(ch >> 4) & 0x0f]);
    hex.push_back(digits[ch & 0x0f]);
  }
  out = Value::bytes(std::move(hex));
  return true;
}

bool hex_decode(const std::string& data, Value& out, std::string& error) {
  std::string bytes;
  bytes.reserve(data.size() / 2);
  int high = -1;
  for (char ch : data) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      continue;
    }
    const int digit = hex_value(ch);
    if (digit < 0) {
      error = "non-hexadecimal digit found";
      return false;
    }
    if (high < 0) {
      high = digit;
    } else {
      bytes.push_back(static_cast<char>((high << 4) | digit));
      high = -1;
    }
  }
  if (high >= 0) {
    error = "odd-length string";
    return false;
  }
  out = Value::bytes(std::move(bytes));
  return true;
}

void string_user_data_cleanup(void* data) {
  delete static_cast<std::string*>(data);
}

bool raise_codec_encode_error(
    Runtime& runtime,
    const std::string& encoding,
    const Value& object,
    size_t start,
    size_t end,
    const std::string& reason,
    std::string& error) {
  error = reason;
  Value exception = runtime.make_exception("UnicodeEncodeError", reason);
  std::string ignored;
  object_set_attr(exception, "encoding", Value::string(encoding), ignored);
  object_set_attr(exception, "object", object, ignored);
  object_set_attr(exception, "start", Value::int64(static_cast<int64_t>(start)), ignored);
  object_set_attr(exception, "end", Value::int64(static_cast<int64_t>(end)), ignored);
  object_set_attr(exception, "reason", Value::string(reason), ignored);
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool utf7_direct(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return true;
  if (cp >= 'a' && cp <= 'z') return true;
  if (cp >= '0' && cp <= '9') return true;
  if (cp == '\t' || cp == '\n' || cp == '\r') return true;
  static constexpr std::string_view punctuation = " '(),-./:?!\"#$%&*;<=>@[]^_`{|}";
  return cp < 0x80 && punctuation.find(static_cast<char>(cp)) != std::string_view::npos;
}

bool utf7_base64_char(unsigned char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
      (ch >= '0' && ch <= '9') || ch == '+' || ch == '/';
}

int utf7_base64_value(unsigned char ch) {
  if (ch >= 'A' && ch <= 'Z') return ch - 'A';
  if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
  if (ch >= '0' && ch <= '9') return ch - '0' + 52;
  return ch == '+' ? 62 : 63;
}

void utf7_append_shift(const std::vector<uint16_t>& units, std::string& encoded) {
  static constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t bits = 0;
  int bit_count = 0;
  encoded.push_back('+');
  for (uint16_t unit : units) {
    bits = (bits << 16) | unit;
    bit_count += 16;
    while (bit_count >= 6) {
      bit_count -= 6;
      encoded.push_back(alphabet[(bits >> bit_count) & 0x3f]);
    }
  }
  if (bit_count > 0) encoded.push_back(alphabet[(bits << (6 - bit_count)) & 0x3f]);
  encoded.push_back('-');
}

bool codecs_warn_invalid_escape(Runtime& runtime, const std::string& message, std::string& error);

bool encode_with_codec(Runtime& runtime, const Value& value, const std::string& encoding, const std::string& errors, Value& out, std::string& error) {
  if (encoding == "hex") {
    std::string data;
    if (!value_bytes_text(value, data)) {
      error = "codecs.encode(..., 'hex') expected bytes";
      return false;
    }
    return hex_encode(data, out);
  }
  if (encoding == "utf_8" || encoding == "utf_8_sig" || encoding == "ascii" || encoding == "latin_1" || encoding == "cp437" || encoding == "cp424" || encoding == "idna" ||
      encoding == "raw_unicode_escape" || encoding == "unicode_escape" || encoding == "utf_7" ||
      encoding == "utf_16" || encoding == "utf_16_le" || encoding == "utf_16_be" ||
      encoding == "utf_32" || encoding == "utf_32_le" || encoding == "utf_32_be") {
    std::string text;
    if (!value_text(value, text)) {
      error = "codecs.encode expected str";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (encoding == "raw_unicode_escape" || encoding == "unicode_escape") {
      std::string encoded;
      for (size_t offset = 0; offset < text.size();) {
        const unsigned char lead = static_cast<unsigned char>(text[offset]);
        const size_t width = utf8_codepoint_width(lead);
        const uint32_t cp = width == 0 || offset + width > text.size()
            ? lead : decode_utf8_codepoint(std::string_view(text).substr(offset), width);
        if (encoding == "raw_unicode_escape" && cp <= 0xff) {
          encoded.push_back(static_cast<char>(cp));
        } else if (encoding == "raw_unicode_escape") {
          append_ascii_backslash_escape(cp, encoded);
        } else if (cp == '\t') {
          encoded.append("\\t");
        } else if (cp == '\n') {
          encoded.append("\\n");
        } else if (cp == '\r') {
          encoded.append("\\r");
        } else if (cp == '\\') {
          encoded.append("\\\\");
        } else if (cp >= 0x20 && cp < 0x7f) {
          encoded.push_back(static_cast<char>(cp));
        } else {
          append_ascii_backslash_escape(cp, encoded);
        }
        offset += width == 0 ? 1 : width;
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "utf_7") {
      std::string encoded;
      std::vector<uint16_t> shifted;
      auto flush_shift = [&]() {
        if (!shifted.empty()) {
          utf7_append_shift(shifted, encoded);
          shifted.clear();
        }
      };
      for (size_t offset = 0; offset < text.size();) {
        size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[offset]));
        if (width == 0 || offset + width > text.size()) width = 1;
        uint32_t cp = decode_utf8_codepoint(std::string_view(text).substr(offset), width);
        offset += width;
        if (cp == '+') {
          flush_shift();
          encoded.append("+-");
        } else if (utf7_direct(cp)) {
          flush_shift();
          encoded.push_back(static_cast<char>(cp));
        } else if (cp <= 0xffff) {
          shifted.push_back(static_cast<uint16_t>(cp));
        } else {
          cp -= 0x10000;
          shifted.push_back(static_cast<uint16_t>(0xd800 + (cp >> 10)));
          shifted.push_back(static_cast<uint16_t>(0xdc00 + (cp & 0x3ff)));
        }
      }
      flush_shift();
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "utf_8") {
      std::string encoded;
      size_t character_index = 0;
      for (size_t offset = 0; offset < text.size(); ++character_index) {
        size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[offset]));
        if (width == 0 || offset + width > text.size()) width = 1;
        const uint32_t cp = decode_utf8_codepoint(std::string_view(text).substr(offset), width);
        if (cp >= 0xd800 && cp <= 0xdfff) {
          if (errors == "surrogatepass") {
            encoded.append(text, offset, width);
          } else if (errors == "surrogateescape" && cp >= 0xdc80 && cp <= 0xdcff) {
            encoded.push_back(static_cast<char>(cp - 0xdc00));
          } else if (errors == "ignore") {
          } else if (errors == "replace") {
            encoded.push_back('?');
          } else if (errors == "backslashreplace" || errors == "namereplace") {
            append_ascii_backslash_escape(cp, encoded);
          } else if (errors == "xmlcharrefreplace") {
            encoded += "&#" + std::to_string(cp) + ";";
          } else {
            size_t error_end = character_index + 1;
            for (size_t next = offset + width; next < text.size();) {
              size_t next_width = utf8_codepoint_width(static_cast<unsigned char>(text[next]));
              if (next_width == 0 || next + next_width > text.size()) next_width = 1;
              const uint32_t next_cp = decode_utf8_codepoint(
                  std::string_view(text).substr(next), next_width);
              if (next_cp < 0xd800 || next_cp > 0xdfff ||
                  (errors == "surrogateescape" && next_cp >= 0xdc80 && next_cp <= 0xdcff)) break;
              ++error_end;
              next += next_width;
            }
            return raise_codec_encode_error(runtime, "utf-8", value, character_index,
                                            error_end, "surrogates not allowed", error);
          }
        } else {
          encoded.append(text, offset, width);
        }
        offset += width;
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "utf_32" || encoding == "utf_32_le" || encoding == "utf_32_be") {
      const bool big_endian = encoding == "utf_32_be";
      std::string encoded;
      if (encoding == "utf_32") encoded.append("\xff\xfe\x00\x00", 4);
      auto append_unit = [&](uint32_t unit) {
        for (int byte = 0; byte < 4; ++byte) {
          const int shift = big_endian ? (3 - byte) * 8 : byte * 8;
          encoded.push_back(static_cast<char>((unit >> shift) & 0xff));
        }
      };
      size_t character_index = 0;
      for (size_t offset = 0; offset < text.size(); ++character_index) {
        size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[offset]));
        if (width == 0 || offset + width > text.size()) width = 1;
        const uint32_t cp = decode_utf8_codepoint(std::string_view(text).substr(offset), width);
        offset += width;
        if (cp >= 0xd800 && cp <= 0xdfff && errors != "surrogatepass") {
          if (errors == "ignore") continue;
          if (errors == "replace") {
            append_unit('?');
            continue;
          }
          std::string escaped;
          if (errors == "backslashreplace" || errors == "namereplace") {
            static constexpr char hex[] = "0123456789abcdef";
            escaped = {'\\', 'u', hex[(cp >> 12) & 0xf], hex[(cp >> 8) & 0xf],
                       hex[(cp >> 4) & 0xf], hex[cp & 0xf]};
          } else if (errors == "xmlcharrefreplace") {
            escaped = "&#" + std::to_string(cp) + ";";
          } else {
            return raise_codec_encode_error(
                runtime, encoding == "utf_32" ? "utf-32" :
                    encoding == "utf_32_le" ? "utf-32-le" : "utf-32-be",
                value, character_index, character_index + 1, "surrogates not allowed", error);
          }
          for (char ch : escaped) append_unit(static_cast<unsigned char>(ch));
          continue;
        }
        append_unit(cp);
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "utf_16" || encoding == "utf_16_le" || encoding == "utf_16_be") {
      const bool big_endian = encoding == "utf_16_be";
      std::string encoded;
      if (encoding == "utf_16") encoded.append("\xff\xfe", 2);
      auto append_unit = [&](uint16_t unit) {
        encoded.push_back(static_cast<char>(big_endian ? unit >> 8 : unit & 0xff));
        encoded.push_back(static_cast<char>(big_endian ? unit & 0xff : unit >> 8));
      };
      size_t character_index = 0;
      for (size_t offset = 0; offset < text.size(); ++character_index) {
        size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[offset]));
        if (width == 0 || offset + width > text.size()) width = 1;
        uint32_t cp = decode_utf8_codepoint(std::string_view(text).substr(offset), width);
        offset += width;
        if (cp >= 0xd800 && cp <= 0xdfff && errors != "surrogatepass") {
          if (errors == "ignore") continue;
          if (errors == "replace") {
            append_unit('?');
            continue;
          }
          if (errors == "backslashreplace" || errors == "namereplace") {
            static constexpr char hex[] = "0123456789abcdef";
            const char escaped[] = {'\\', 'u', hex[(cp >> 12) & 0xf], hex[(cp >> 8) & 0xf],
                                    hex[(cp >> 4) & 0xf], hex[cp & 0xf]};
            for (char ch : escaped) append_unit(static_cast<uint16_t>(ch));
            continue;
          }
          if (errors == "xmlcharrefreplace") {
            const std::string escaped = "&#" + std::to_string(cp) + ";";
            for (char ch : escaped) append_unit(static_cast<uint16_t>(ch));
            continue;
          }
          return raise_codec_encode_error(
              runtime, encoding == "utf_16" ? "utf-16" :
                  encoding == "utf_16_le" ? "utf-16-le" : "utf-16-be",
              value, character_index, character_index + 1, "surrogates not allowed", error);
        }
        if (cp <= 0xffff) {
          append_unit(static_cast<uint16_t>(cp));
        } else {
          cp -= 0x10000;
          append_unit(static_cast<uint16_t>(0xd800 + (cp >> 10)));
          append_unit(static_cast<uint16_t>(0xdc00 + (cp & 0x3ff)));
        }
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "ascii") {
      std::string encoded = ascii_encode_text(runtime, text, errors, error);
      if (!error.empty()) {
        return false;
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "latin_1") {
      std::string encoded = latin1_encode_text(runtime, text, errors, error);
      if (!error.empty()) {
        return false;
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "cp424") {
      std::string encoded;
      for (size_t i = 0; i < text.size();) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        const size_t width = utf8_codepoint_width(ch);
        const uint32_t codepoint = width == 0 || i + width > text.size()
            ? ch : decode_utf8_codepoint(text.substr(i), width);
        const size_t advance = width == 0 ? 1 : width;
        if (codepoint == 0x00a2) encoded.push_back('J');
        else if (codepoint == '\r') encoded.push_back('\r');
        else if (codepoint == '\n') encoded.push_back('%');
        else if (errors == "ignore") {}
        else if (errors == "replace") encoded.push_back('?');
        else {
          error = "'charmap' codec can't encode character";
          runtime.raise_class_error("UnicodeEncodeError", error);
          return false;
        }
        i += advance;
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "cp437") {
      std::string encoded;
      if (!cp437_encode_text(text, errors, encoded, error)) {
        runtime.raise_class_error("UnicodeEncodeError", error);
        return false;
      }
      out = Value::bytes(std::move(encoded));
    } else if (encoding == "utf_8_sig") {
      out = Value::bytes(std::string("\xef\xbb\xbf", 3) + text);
    } else if (encoding == "idna") {
      std::string encoded = ascii_encode_text(runtime, text, errors, error);
      if (!error.empty()) {
        return false;
      }
      out = Value::bytes(std::move(encoded));
    } else {
      if (errors == "backslashreplace" || errors == "surrogateescape") {
        std::string encoded;
        encoded.reserve(text.size());
        for (size_t index = 0; index < text.size();) {
          const auto first = static_cast<unsigned char>(text[index]);
          if (index + 2 < text.size() && first == 0xed) {
            const auto second = static_cast<unsigned char>(text[index + 1]);
            const auto third = static_cast<unsigned char>(text[index + 2]);
            if (second >= 0xa0 && second <= 0xbf && (third & 0xc0) == 0x80) {
              const uint32_t codepoint =
                  ((first & 0x0f) << 12) | ((second & 0x3f) << 6) | (third & 0x3f);
              if (errors == "surrogateescape" && codepoint >= 0xdc80 && codepoint <= 0xdcff) {
                encoded.push_back(static_cast<char>(codepoint - 0xdc00));
              } else {
                static constexpr char hex[] = "0123456789abcdef";
                encoded += "\\u";
                encoded.push_back(hex[(codepoint >> 12) & 0x0f]);
                encoded.push_back(hex[(codepoint >> 8) & 0x0f]);
                encoded.push_back(hex[(codepoint >> 4) & 0x0f]);
                encoded.push_back(hex[codepoint & 0x0f]);
              }
              index += 3;
              continue;
            }
          }
          encoded.push_back(text[index++]);
        }
        out = Value::bytes(std::move(encoded));
      } else {
        out = Value::bytes(std::move(text));
      }
    }
    return true;
  }
  if (encoding == "gbk") {
    std::string text;
    if (!value_text(value, text)) {
      error = "codecs.encode expected str";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    std::string encoded;
    if (!encode_gbk_text(text, encoded, error)) {
      runtime.raise_class_error("UnicodeEncodeError", error);
      return false;
    }
    out = Value::bytes(std::move(encoded));
    return true;
  }
  error = "unknown encoding: " + encoding;
  return false;
}

bool raise_codec_decode_error(
    Runtime& runtime,
    const std::string& encoding,
    const Value& object,
    size_t start,
    size_t end,
    const std::string& reason,
    std::string& error) {
  error = reason;
  Value exception = runtime.make_exception("UnicodeDecodeError", reason);
  std::string ignored;
  object_set_attr(exception, "encoding", Value::string(encoding), ignored);
  object_set_attr(exception, "object", object, ignored);
  object_set_attr(exception, "start", Value::int64(static_cast<int64_t>(start)), ignored);
  object_set_attr(exception, "end", Value::int64(static_cast<int64_t>(end)), ignored);
  object_set_attr(exception, "reason", Value::string(reason), ignored);
  runtime.set_pending_exception(std::move(exception));
  return false;
}

bool decode_with_codec(Runtime& runtime, const Value& value, const std::string& encoding, const std::string& errors, Value& out, std::string& error) {
  if (encoding == "hex") {
    std::string data;
    if (!value_bytes_text(value, data) && !value_text(value, data)) {
      error = "codecs.decode(..., 'hex') expected bytes-like or str";
      return false;
    }
    return hex_decode(data, out, error);
  }
  if (encoding == "utf_8" || encoding == "utf_8_sig" || encoding == "ascii" || encoding == "latin_1" || encoding == "cp437" || encoding == "idna" ||
      encoding == "raw_unicode_escape" || encoding == "unicode_escape" || encoding == "utf_7" ||
      encoding == "utf_16" || encoding == "utf_16_le" || encoding == "utf_16_be" ||
      encoding == "utf_32" || encoding == "utf_32_le" || encoding == "utf_32_be") {
    std::string data;
    if (!value_bytes_text(value, data)) {
      error = "codecs.decode expected bytes-like";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (encoding == "unicode_escape") {
      std::string decoded;
      for (size_t i = 0; i < data.size();) {
        const unsigned char raw = static_cast<unsigned char>(data[i]);
        if (raw != '\\') {
          append_utf8(raw, decoded);
          ++i;
          continue;
        }
        const size_t escape_start = i++;
        if (i >= data.size()) {
          return raise_codec_decode_error(runtime, "unicodeescape", value, escape_start, i,
                                          "trailing \\ in string", error);
        }
        const unsigned char escaped = static_cast<unsigned char>(data[i++]);
        if (escaped == '\n') continue;
        if (escaped == '\r') {
          if (i < data.size() && data[i] == '\n') ++i;
          continue;
        }
        const char* simple = "\\'\"abtnvfr";
        const char* replacements = "\\'\"\a\b\t\n\v\f\r";
        if (const char* found = std::strchr(simple, static_cast<char>(escaped))) {
          decoded.push_back(replacements[found - simple]);
          continue;
        }
        if (escaped >= '0' && escaped <= '7') {
          uint32_t cp = escaped - '0';
          size_t digits = 1;
          while (digits < 3 && i < data.size() && data[i] >= '0' && data[i] <= '7') {
            cp = (cp << 3) | static_cast<uint32_t>(data[i++] - '0');
            ++digits;
          }
          if (cp > 0xff && !codecs_warn_invalid_escape(
                  runtime, "\"" + data.substr(escape_start, i - escape_start) +
                      "\" is an invalid octal escape sequence", error)) return false;
          append_utf8(cp, decoded);
          continue;
        }
        if (escaped == 'x' || escaped == 'u' || escaped == 'U') {
          const size_t required = escaped == 'x' ? 2 : escaped == 'u' ? 4 : 8;
          uint32_t cp = 0;
          size_t parsed = 0;
          while (parsed < required && i < data.size()) {
            const int digit = hex_value(data[i]);
            if (digit < 0) break;
            cp = (cp << 4) | static_cast<uint32_t>(digit);
            ++i;
            ++parsed;
          }
          if (parsed == required && cp <= 0x10ffff) {
            append_utf8(cp, decoded);
            continue;
          }
          if (errors == "ignore" || errors == "replace" || errors == "backslashreplace") {
            if (errors == "replace") append_utf8(0xfffd, decoded);
            else if (errors == "backslashreplace") {
              static constexpr char hex[] = "0123456789abcdef";
              for (size_t index = escape_start; index < i; ++index) {
                const unsigned char byte = static_cast<unsigned char>(data[index]);
                decoded.append("\\x");
                decoded.push_back(hex[byte >> 4]);
                decoded.push_back(hex[byte & 0xf]);
              }
            }
            continue;
          }
          return raise_codec_decode_error(
              runtime, "unicodeescape", value, escape_start, i,
              parsed == required ? "illegal Unicode character" :
                  escaped == 'x' ? "truncated \\xXX escape" : "truncated \\uXXXX escape",
              error);
        }
        std::string display("\\");
        if (escaped >= 0x20 && escaped < 0x7f) display.push_back(static_cast<char>(escaped));
        else append_utf8(escaped, display);
        if (!codecs_warn_invalid_escape(
                runtime, "\"" + display + "\" is an invalid escape sequence", error)) return false;
        decoded.push_back('\\');
        append_utf8(escaped, decoded);
      }
      out = Value::string(std::move(decoded));
    } else if (encoding == "raw_unicode_escape") {
      std::string decoded;
      for (size_t i = 0; i < data.size();) {
        if (data[i] == '\\' && i + 1 < data.size() && (data[i + 1] == 'u' || data[i + 1] == 'U')) {
          const size_t digits = data[i + 1] == 'u' ? 4 : 8;
          uint32_t cp = 0;
          size_t parsed = 0;
          while (parsed < digits && i + 2 + parsed < data.size()) {
            const int digit = hex_value(data[i + 2 + parsed]);
            if (digit < 0) break;
            cp = (cp << 4) | static_cast<uint32_t>(digit);
            ++parsed;
          }
          if (parsed == digits && cp <= 0x10ffff) {
            append_utf8(cp, decoded);
            i += 2 + digits;
            continue;
          }
          const size_t invalid_end = i + 2 + parsed;
          if (errors == "ignore" || errors == "replace" || errors == "backslashreplace") {
            if (errors == "replace") append_utf8(0xfffd, decoded);
            else if (errors == "backslashreplace") {
              static constexpr char hex[] = "0123456789abcdef";
              for (size_t index = i; index < invalid_end; ++index) {
                const unsigned char byte = static_cast<unsigned char>(data[index]);
                decoded.append("\\x");
                decoded.push_back(hex[byte >> 4]);
                decoded.push_back(hex[byte & 0xf]);
              }
            }
            i = invalid_end;
            continue;
          }
          return raise_codec_decode_error(
              runtime, "rawunicodeescape", value, i, invalid_end,
              parsed == digits ? "illegal Unicode character" : "truncated \\uXXXX escape", error);
        }
        append_utf8(static_cast<unsigned char>(data[i++]), decoded);
      }
      out = Value::string(std::move(decoded));
    } else if (encoding == "utf_7") {
      std::string decoded;
      for (size_t i = 0; i < data.size();) {
        const unsigned char raw = static_cast<unsigned char>(data[i]);
        if (raw != '+') {
          if (raw < 0x80) {
            decoded.push_back(static_cast<char>(raw));
            ++i;
            continue;
          }
          if (errors == "ignore") {
            ++i;
            continue;
          }
          if (errors == "replace") {
            append_utf8(0xfffd, decoded);
            ++i;
            continue;
          }
          return raise_codec_decode_error(runtime, "utf7", value, i, i + 1,
                                          "unexpected special character", error);
        }
        const size_t shift_start = i++;
        if (i < data.size() && data[i] == '-') {
          decoded.push_back('+');
          ++i;
          continue;
        }
        const size_t digits_start = i;
        while (i < data.size() && utf7_base64_char(static_cast<unsigned char>(data[i]))) ++i;
        const size_t digits_end = i;
        const bool has_terminator = i < data.size();
        const bool explicit_dash = has_terminator && data[i] == '-';
        if (has_terminator) ++i;

        uint32_t bits = 0;
        int bit_count = 0;
        std::vector<uint16_t> units;
        for (size_t index = digits_start; index < digits_end; ++index) {
          bits = (bits << 6) | static_cast<uint32_t>(
              utf7_base64_value(static_cast<unsigned char>(data[index])));
          bit_count += 6;
          if (bit_count >= 16) {
            bit_count -= 16;
            units.push_back(static_cast<uint16_t>((bits >> bit_count) & 0xffff));
            bits &= bit_count == 0 ? 0 : ((1u << bit_count) - 1u);
          }
        }
        bool invalid = digits_start == digits_end ||
            !((bit_count == 0 || bit_count == 2 || bit_count == 4) && bits == 0) ||
            (has_terminator && !explicit_dash);
        for (size_t index = 0; index < units.size();) {
          const uint16_t unit = units[index++];
          if (unit >= 0xd800 && unit <= 0xdbff) {
            if (index < units.size() && units[index] >= 0xdc00 && units[index] <= 0xdfff) {
              const uint16_t low = units[index++];
              append_utf8(0x10000 + ((static_cast<uint32_t>(unit) - 0xd800) << 10) +
                              (low - 0xdc00), decoded);
            } else if (!invalid && explicit_dash) {
              append_utf8(unit, decoded);
            } else {
              invalid = true;
            }
          } else if (unit >= 0xdc00 && unit <= 0xdfff) {
            if (!invalid && explicit_dash) append_utf8(unit, decoded);
            else invalid = true;
          } else {
            append_utf8(unit, decoded);
          }
        }
        if (invalid) {
          if (errors == "replace") {
            append_utf8(0xfffd, decoded);
          } else if (errors != "ignore") {
            return raise_codec_decode_error(runtime, "utf7", value, shift_start, i,
                                            "ill-formed sequence", error);
          }
        }
      }
      out = Value::string(std::move(decoded));
    } else if (encoding == "utf_8") {
      std::string decoded;
      for (size_t i = 0; i < data.size();) {
        const unsigned char lead = static_cast<unsigned char>(data[i]);
        if (lead < 0x80) {
          decoded.push_back(static_cast<char>(lead));
          ++i;
          continue;
        }
        const size_t width = utf8_codepoint_width(lead);
        bool valid = width >= 2 && i + width <= data.size();
        if (valid) {
          for (size_t index = 1; index < width; ++index) {
            if ((static_cast<unsigned char>(data[i + index]) & 0xc0) != 0x80) {
              valid = false;
              break;
            }
          }
        }
        uint32_t cp = 0;
        if (valid) {
          cp = decode_utf8_codepoint(std::string_view(data).substr(i), width);
          const uint32_t minimum = width == 2 ? 0x80 : width == 3 ? 0x800 : 0x10000;
          valid = cp >= minimum && cp <= 0x10ffff &&
              (!(cp >= 0xd800 && cp <= 0xdfff) || errors == "surrogatepass");
        }
        if (valid) {
          decoded.append(data, i, width);
          i += width;
          continue;
        }
        if (errors == "ignore") {
          ++i;
          continue;
        }
        if (errors == "replace") {
          append_utf8(0xfffd, decoded);
          ++i;
          continue;
        }
        if (errors == "surrogateescape") {
          append_utf8(0xdc00 + lead, decoded);
          ++i;
          continue;
        }
        if (errors == "backslashreplace") {
          static constexpr char hex[] = "0123456789abcdef";
          decoded.append("\\x");
          decoded.push_back(hex[lead >> 4]);
          decoded.push_back(hex[lead & 0xf]);
          ++i;
          continue;
        }
        return raise_codec_decode_error(runtime, "utf-8", value, i, i + 1,
                                        "invalid start byte", error);
      }
      out = Value::string(std::move(decoded));
    } else if (encoding == "utf_32" || encoding == "utf_32_le" || encoding == "utf_32_be") {
      size_t start = 0;
      bool big_endian = encoding == "utf_32_be";
      if (encoding == "utf_32" && data.size() >= 4) {
        if (static_cast<unsigned char>(data[0]) == 0xff && static_cast<unsigned char>(data[1]) == 0xfe) start = 4;
        else if (static_cast<unsigned char>(data[0]) == 0x00 && static_cast<unsigned char>(data[1]) == 0x00 &&
                 static_cast<unsigned char>(data[2]) == 0xfe && static_cast<unsigned char>(data[3]) == 0xff) {
          start = 4;
          big_endian = true;
        }
      }
      std::string decoded;
      auto append_bad_bytes = [&](size_t bad_start, size_t bad_end) {
        if (errors == "ignore") return;
        if (errors == "replace") {
          append_utf8(0xfffd, decoded);
          return;
        }
        if (errors == "backslashreplace") {
          static constexpr char hex[] = "0123456789abcdef";
          for (size_t index = bad_start; index < bad_end; ++index) {
            const unsigned char byte = static_cast<unsigned char>(data[index]);
            decoded.append("\\x");
            decoded.push_back(hex[byte >> 4]);
            decoded.push_back(hex[byte & 0xf]);
          }
        }
      };
      size_t i = start;
      for (; i + 3 < data.size(); i += 4) {
        uint32_t cp = 0;
        for (int byte = 0; byte < 4; ++byte) {
          const int shift = big_endian ? (3 - byte) * 8 : byte * 8;
          cp |= static_cast<uint32_t>(static_cast<unsigned char>(data[i + byte])) << shift;
        }
        if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
          if (errors == "surrogatepass" && cp >= 0xd800 && cp <= 0xdfff) {
            append_utf8(cp, decoded);
          } else if (errors == "ignore" || errors == "replace" || errors == "backslashreplace") {
            append_bad_bytes(i, i + 4);
          } else {
            return raise_codec_decode_error(runtime, encoding, value, i, i + 4,
                                            "code point not in range(0x110000)", error);
          }
          continue;
        }
        append_utf8(cp, decoded);
      }
      if (i < data.size()) {
        if (errors == "ignore" || errors == "replace" || errors == "backslashreplace") {
          append_bad_bytes(i, data.size());
        } else {
          return raise_codec_decode_error(runtime, encoding, value, i, data.size(),
                                          "truncated data", error);
        }
      }
      out = Value::string(std::move(decoded));
    } else if (encoding == "utf_16" || encoding == "utf_16_le" || encoding == "utf_16_be") {
      size_t start = 0;
      bool big_endian = encoding == "utf_16_be";
      if (encoding == "utf_16") {
        if (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0xff && static_cast<unsigned char>(data[1]) == 0xfe) {
          start = 2;
        } else if (data.size() >= 2 && static_cast<unsigned char>(data[0]) == 0xfe && static_cast<unsigned char>(data[1]) == 0xff) {
          start = 2;
          big_endian = true;
        }
      }
      std::string decoded;
      auto append_bad_bytes = [&](size_t bad_start, size_t bad_end) {
        if (errors == "ignore") return;
        if (errors == "replace") {
          append_utf8(0xfffd, decoded);
          return;
        }
        if (errors == "backslashreplace") {
          static constexpr char hex[] = "0123456789abcdef";
          for (size_t index = bad_start; index < bad_end; ++index) {
            const unsigned char byte = static_cast<unsigned char>(data[index]);
            decoded.append("\\x");
            decoded.push_back(hex[byte >> 4]);
            decoded.push_back(hex[byte & 0xf]);
          }
        }
      };
      size_t i = start;
      while (i + 1 < data.size()) {
        const size_t unit_start = i;
        const uint16_t unit = big_endian
            ? (static_cast<unsigned char>(data[i]) << 8) | static_cast<unsigned char>(data[i + 1])
            : static_cast<unsigned char>(data[i]) | (static_cast<unsigned char>(data[i + 1]) << 8);
        i += 2;
        if (unit >= 0xd800 && unit <= 0xdbff) {
          const bool has_complete_following_unit = i + 1 < data.size();
          if (i + 1 < data.size()) {
            const uint16_t low = big_endian
                ? (static_cast<unsigned char>(data[i]) << 8) | static_cast<unsigned char>(data[i + 1])
                : static_cast<unsigned char>(data[i]) | (static_cast<unsigned char>(data[i + 1]) << 8);
            if (low >= 0xdc00 && low <= 0xdfff) {
              append_utf8(0x10000 + ((static_cast<uint32_t>(unit) - 0xd800) << 10) + (low - 0xdc00), decoded);
              i += 2;
              continue;
            }
          }
          if (errors == "surrogatepass") append_utf8(unit, decoded);
          else if (errors == "ignore" || errors == "replace" || errors == "backslashreplace") {
            const size_t bad_end = has_complete_following_unit ? unit_start + 2 : data.size();
            append_bad_bytes(unit_start, bad_end);
            i = bad_end;
          } else {
            const size_t bad_end = has_complete_following_unit ? unit_start + 2 : data.size();
            return raise_codec_decode_error(runtime, encoding, value, unit_start, bad_end,
                                            "illegal UTF-16 surrogate", error);
          }
          continue;
        }
        if (unit >= 0xdc00 && unit <= 0xdfff) {
          if (errors == "surrogatepass") append_utf8(unit, decoded);
          else if (errors == "ignore" || errors == "replace" || errors == "backslashreplace") {
            append_bad_bytes(unit_start, unit_start + 2);
          } else {
            return raise_codec_decode_error(runtime, encoding, value, unit_start, unit_start + 2,
                                            "illegal encoding", error);
          }
          continue;
        }
        append_utf8(unit, decoded);
      }
      if (i < data.size()) {
        if (errors == "ignore" || errors == "replace" || errors == "backslashreplace") {
          append_bad_bytes(i, data.size());
        } else {
          return raise_codec_decode_error(runtime, encoding, value, i, data.size(),
                                          "truncated data", error);
        }
      }
      out = Value::string(std::move(decoded));
    } else if (encoding == "ascii") {
      std::string decoded = ascii_decode_text(runtime, data, errors, error);
      if (!error.empty()) {
        return false;
      }
      out = Value::string(std::move(decoded));
    } else if (encoding == "latin_1") {
      out = Value::string(latin1_decode_text(data));
    } else if (encoding == "cp437") {
      out = Value::string(cp437_decode_bytes(data));
    } else if (encoding == "idna") {
      std::string decoded = ascii_decode_text(runtime, data, errors, error);
      if (!error.empty()) {
        return false;
      }
      out = Value::string(std::move(decoded));
    } else if (encoding == "utf_8_sig") {
      if (data.size() >= 3 &&
          static_cast<unsigned char>(data[0]) == 0xef &&
          static_cast<unsigned char>(data[1]) == 0xbb &&
          static_cast<unsigned char>(data[2]) == 0xbf) {
        data.erase(0, 3);
      }
      out = Value::string(std::move(data));
    } else {
      out = Value::string(std::move(data));
    }
    return true;
  }
  if (encoding == "gbk") {
    std::string data;
    if (!value_bytes_text(value, data)) {
      error = "codecs.decode expected bytes-like";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    std::string decoded;
    if (!decode_gbk_bytes(data, decoded, error)) {
      runtime.raise_class_error("UnicodeDecodeError", error);
      return false;
    }
    out = Value::string(std::move(decoded));
    return true;
  }
  error = "unknown encoding: " + encoding;
  return false;
}

bool codecs_lookup(Runtime&, const Value*, uint32_t, Value&, std::string&, void*);

bool call_codec_with_note(
    Runtime& runtime,
    const Value& callable,
    const Value* args,
    uint32_t argc,
    const char* operation,
    const std::string& encoding,
    Value& out,
    std::string& error) {
  if (runtime_call_callable(runtime, callable, args, argc, out, error)) return true;
  Value exception;
  if (runtime.take_pending_exception(exception)) {
    Value notes;
    std::string ignored;
    if (!object_get_attr(exception, "__notes__", notes, ignored)) {
      notes = Value::list({});
      (void)object_set_attr(exception, "__notes__", notes, ignored);
    }
    if (auto* list = value_as_list(notes)) {
      list->items.push_back(Value::string(
          std::string(operation) + " with '" + encoding + "' codec failed"));
    }
    runtime.set_pending_exception(std::move(exception));
  }
  return false;
}

bool codecs_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "codecs.encode expected object and optional encoding/errors";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string encoding = "utf_8";
  std::string lookup_encoding = encoding;
  if (argc >= 2) {
    auto* enc = value_as_string(args[1]);
    if (enc != nullptr) {
      encoding = string_object_to_string(*enc);
      lookup_encoding = canonical_encoding(encoding);
    }
  }
  Value info;
  Value lookup_arg = Value::string(lookup_encoding);
  if (!codecs_lookup(runtime, &lookup_arg, 1, info, error, nullptr)) return false;
  Value encoder;
  if (!object_get_attr(info, "encode", encoder, error)) return false;
  Value call_args[2] = {args[0], Value::string(normalized_errors(args, argc, 2))};
  Value result;
  if (!call_codec_with_note(
          runtime, encoder, call_args, 2, "encoding", encoding, result, error)) return false;
  auto* tuple = value_as_tuple(result);
  if (tuple == nullptr || tuple->items.empty()) {
    error = "encoder must return a tuple";
    return false;
  }
  out = tuple->items[0];
  return true;
}

bool codecs_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "codecs.decode expected object and optional encoding/errors";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string encoding = "utf_8";
  std::string lookup_encoding = encoding;
  if (argc >= 2) {
    auto* enc = value_as_string(args[1]);
    if (enc != nullptr) {
      encoding = string_object_to_string(*enc);
      lookup_encoding = canonical_encoding(encoding);
    }
  }
  Value info;
  Value lookup_arg = Value::string(lookup_encoding);
  if (!codecs_lookup(runtime, &lookup_arg, 1, info, error, nullptr)) return false;
  Value decoder;
  if (!object_get_attr(info, "decode", decoder, error)) return false;
  Value call_args[2] = {args[0], Value::string(normalized_errors(args, argc, 2))};
  Value result;
  if (!call_codec_with_note(
          runtime, decoder, call_args, 2, "decoding", encoding, result, error)) return false;
  auto* tuple = value_as_tuple(result);
  if (tuple == nullptr || tuple->items.empty()) {
    error = "decoder must return a tuple";
    return false;
  }
  out = tuple->items[0];
  return true;
}

#if defined(_WIN32)
bool utf8_to_wide(std::string_view text, std::wstring& out, std::string& error) {
  if (text.empty()) {
    out.clear();
    return true;
  }
  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0);
  if (required <= 0) {
    error = "invalid UTF-8 text";
    return false;
  }
  out.resize(static_cast<size_t>(required));
  return MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      out.data(), required) == required;
}

bool codecs_code_page_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3 || args[0].tag != ValueTag::Int64 || value_as_string(args[1]) == nullptr) {
    error = "code_page_encode() expected code page, str, and optional errors";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const UINT code_page = static_cast<UINT>(args[0].as.i64);
  const std::string errors = normalized_errors(args, argc, 2);
  const std::string text = string_object_to_string(*value_as_string(args[1]));
  std::wstring wide;
  if (!utf8_to_wide(text, wide, error)) {
    runtime.raise_class_error("UnicodeEncodeError", error);
    return false;
  }
  std::string encoded;
  for (size_t i = 0; i < wide.size();) {
    const size_t units = i + 1 < wide.size() && wide[i] >= 0xd800 && wide[i] <= 0xdbff &&
        wide[i + 1] >= 0xdc00 && wide[i + 1] <= 0xdfff ? 2 : 1;
    BOOL used_default = FALSE;
    char buffer[16];
    const int count = WideCharToMultiByte(
        code_page, WC_NO_BEST_FIT_CHARS, wide.data() + i, static_cast<int>(units),
        buffer, static_cast<int>(sizeof(buffer)), nullptr, &used_default);
    if (count <= 0 || used_default) {
      if (errors == "ignore") {
        i += units;
        continue;
      }
      if (errors != "replace") {
        error = "character maps to <undefined>";
        runtime.raise_class_error("UnicodeEncodeError", error);
        return false;
      }
      encoded.push_back('?');
    } else {
      encoded.append(buffer, static_cast<size_t>(count));
    }
    i += units;
  }
  out = Value::tuple({
      Value::bytes(std::move(encoded)),
      Value::int64(static_cast<int64_t>(utf8_codepoint_count(text)))});
  return true;
}

bool codecs_code_page_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4 || args[0].tag != ValueTag::Int64) {
    error = "code_page_decode() expected code page, bytes-like object, and optional errors/final";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string bytes;
  if (!value_bytes_text(args[1], bytes)) {
    error = "code_page_decode() argument 2 must be bytes-like";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (bytes.empty()) {
    out = Value::tuple({Value::string(""), Value::int64(0)});
    return true;
  }
  const UINT code_page = static_cast<UINT>(args[0].as.i64);
  const int wide_size = MultiByteToWideChar(
      code_page, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
  if (wide_size <= 0) {
    error = "invalid character sequence";
    runtime.raise_class_error("UnicodeDecodeError", error);
    return false;
  }
  std::wstring wide(static_cast<size_t>(wide_size), L'\0');
  if (MultiByteToWideChar(
          code_page, 0, bytes.data(), static_cast<int>(bytes.size()),
          wide.data(), wide_size) != wide_size) {
    error = "invalid character sequence";
    runtime.raise_class_error("UnicodeDecodeError", error);
    return false;
  }
  const int utf8_size = WideCharToMultiByte(
      CP_UTF8, 0, wide.data(), wide_size, nullptr, 0, nullptr, nullptr);
  std::string decoded(static_cast<size_t>(utf8_size), '\0');
  if (utf8_size > 0) {
    WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wide_size, decoded.data(), utf8_size,
        nullptr, nullptr);
  }
  out = Value::tuple({
      Value::string(std::move(decoded)),
      Value::int64(static_cast<int64_t>(bytes.size()))});
  return true;
}
#endif

std::vector<Value>& codec_search_registry() {
  static std::vector<Value> registry;
  return registry;
}

std::unordered_map<std::string, Value>& codec_lookup_cache() {
  static std::unordered_map<std::string, Value> cache;
  return cache;
}

bool codec_lookup_via_registry(Runtime& runtime, const std::string& name, Value& out, std::string& error) {
  Value search_arg = Value::string(name);
  for (const Value& search_function : codec_search_registry()) {
    Value search_result;
    if (!runtime_call_callable(runtime, search_function, &search_arg, 1, search_result, error)) {
      return false;
    }
    if (search_result.tag != ValueTag::None) {
      const TupleObject* result_tuple = value_as_tuple(search_result);
      Value tuple_storage;
      std::string ignored;
      if (result_tuple == nullptr &&
          object_get_attr(search_result, "_tuple", tuple_storage, ignored)) {
        result_tuple = value_as_tuple(tuple_storage);
      }
      if (result_tuple == nullptr || result_tuple->items.size() != 4) {
        error = "codec search functions must return 4-tuples";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      out = std::move(search_result);
      return true;
    }
  }
  return false;
}

bool codec_info_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  const uint32_t offset = argc > 0 && value_as_string(args[0]) == nullptr ? 1 : 0;
  if (argc < offset + 1 || argc > offset + 2) {
    error = "CodecInfo.encode expected object and optional errors";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (value_as_string(args[offset]) == nullptr) {
    error = "encoder argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto* encoding = static_cast<const std::string*>(user_data);
  Value encoded;
  if (!encode_with_codec(runtime, args[offset], encoding == nullptr ? "utf_8" : *encoding,
                         normalized_errors(args + offset, argc - offset, 1), encoded, error)) {
    return false;
  }
  auto* text = value_as_string(args[offset]);
  out = Value::tuple(
      {encoded, Value::int64(text == nullptr ? 0 : static_cast<int64_t>(utf8_codepoint_count(string_object_view(*text))))});
  return true;
}

bool codec_info_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  const auto* encoding = static_cast<const std::string*>(user_data);
  const bool escape_codec = encoding != nullptr &&
      (*encoding == "raw_unicode_escape" || *encoding == "unicode_escape");
  std::string first_bytes;
  const bool first_is_input = argc > 0 &&
      (value_bytes_text(args[0], first_bytes) || (escape_codec && value_text(args[0], first_bytes)));
  const uint32_t offset = argc > 0 && !first_is_input ? 1 : 0;
  if (argc < offset + 1 || argc > offset + 4) {
    error = "CodecInfo.decode expected object and optional errors";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string input_bytes;
  if (!value_bytes_text(args[offset], input_bytes) &&
      !(escape_codec && value_text(args[offset], input_bytes))) {
    error = "decoder argument must be bytes-like";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const uint32_t real_argc = argc - offset;
  const bool utf16_codec = encoding != nullptr &&
      (*encoding == "utf_16" || *encoding == "utf_16_le" || *encoding == "utf_16_be");
  const bool utf32_codec = encoding != nullptr &&
      (*encoding == "utf_32" || *encoding == "utf_32_le" || *encoding == "utf_32_be");
  const bool utf8_codec = encoding != nullptr && *encoding == "utf_8";
  const bool utf7_codec = encoding != nullptr && *encoding == "utf_7";
  const bool has_final_arg = real_argc >= 3 && args[argc - 1].tag == ValueTag::Bool;
  const bool final = has_final_arg && args[argc - 1].as.b;
  if (escape_codec && has_final_arg && !final) {
    const size_t slash = input_bytes.rfind('\\');
    size_t pending_start = std::string::npos;
    if (slash != std::string::npos) {
      size_t slash_run_start = slash;
      while (slash_run_start > 0 && input_bytes[slash_run_start - 1] == '\\') --slash_run_start;
      const bool slash_is_unpaired = (slash - slash_run_start + 1) % 2 != 0;
      if (slash_is_unpaired) {
        if (slash + 1 == input_bytes.size()) {
          pending_start = slash;
        } else if (input_bytes[slash + 1] == 'x' ||
                   input_bytes[slash + 1] == 'u' || input_bytes[slash + 1] == 'U') {
          const size_t digits = input_bytes[slash + 1] == 'x' ? 2 :
              input_bytes[slash + 1] == 'u' ? 4 : 8;
          const size_t available = input_bytes.size() - slash - 2;
          bool all_hex = available < digits;
          for (size_t index = 0; all_hex && index < available; ++index) {
            all_hex = hex_value(input_bytes[slash + 2 + index]) >= 0;
          }
          if (all_hex) pending_start = slash;
        }
      }
    }
    if (pending_start != std::string::npos) {
      Value decoded;
      const Value prefix = Value::bytes(input_bytes.substr(0, pending_start));
      if (!decode_with_codec(
              runtime, prefix, *encoding,
              normalized_errors(args + offset, argc - offset, 1), decoded, error)) {
        return false;
      }
      out = Value::tuple({decoded, Value::int64(static_cast<int64_t>(pending_start))});
      return true;
    }
  }
  if (utf8_codec && !final) {
    const std::string error_mode = normalized_errors(args + offset, argc - offset, 1);
    size_t decodable_size = input_bytes.size();
    for (size_t i = 0; i < input_bytes.size();) {
      const unsigned char lead = static_cast<unsigned char>(input_bytes[i]);
      if (lead < 0x80) {
        ++i;
        continue;
      }
      const size_t width = utf8_codepoint_width(lead);
      if (width < 2 || width > 4 ||
          (width == 2 && lead < 0xc2) || (width == 4 && lead > 0xf4)) {
        ++i;
        continue;
      }
      if (i + width <= input_bytes.size()) {
        i += width;
        continue;
      }
      bool plausible = true;
      for (size_t index = i + 1; index < input_bytes.size(); ++index) {
        if ((static_cast<unsigned char>(input_bytes[index]) & 0xc0) != 0x80) plausible = false;
      }
      if (plausible && i + 1 < input_bytes.size()) {
        const unsigned char second = static_cast<unsigned char>(input_bytes[i + 1]);
        if ((lead == 0xe0 && second < 0xa0) ||
            (lead == 0xed && second >= 0xa0 && error_mode != "surrogatepass") ||
            (lead == 0xf0 && second < 0x90) ||
            (lead == 0xf4 && second >= 0x90)) plausible = false;
      }
      if (plausible) {
        decodable_size = i;
        break;
      }
      ++i;
    }
    if (decodable_size != input_bytes.size()) {
      Value decoded;
      const Value prefix = Value::bytes(input_bytes.substr(0, decodable_size));
      if (!decode_with_codec(runtime, prefix, *encoding, error_mode, decoded, error)) return false;
      out = Value::tuple({decoded, Value::int64(static_cast<int64_t>(decodable_size))});
      return true;
    }
  }
  if (utf7_codec && !final) {
    size_t pending_start = std::string::npos;
    for (size_t i = 0; i < input_bytes.size();) {
      if (input_bytes[i] != '+') {
        ++i;
        continue;
      }
      const size_t shift_start = i++;
      if (i < input_bytes.size() && input_bytes[i] == '-') {
        ++i;
        continue;
      }
      while (i < input_bytes.size() &&
             utf7_base64_char(static_cast<unsigned char>(input_bytes[i]))) ++i;
      if (i == input_bytes.size()) {
        pending_start = shift_start;
        break;
      }
      ++i;
    }
    if (pending_start != std::string::npos) {
      Value decoded;
      const Value prefix = Value::bytes(input_bytes.substr(0, pending_start));
      if (!decode_with_codec(runtime, prefix, *encoding,
                             normalized_errors(args + offset, argc - offset, 1), decoded, error)) {
        return false;
      }
      out = Value::tuple({decoded, Value::int64(static_cast<int64_t>(pending_start))});
      return true;
    }
  }
  if ((utf16_codec || utf32_codec) && !final) {
    size_t decodable_size = input_bytes.size();
    if (utf16_codec) {
      decodable_size -= decodable_size % 2;
      bool big_endian = *encoding == "utf_16_be";
      if (*encoding == "utf_16" && decodable_size >= 2) {
        if (static_cast<unsigned char>(input_bytes[0]) == 0xfe &&
            static_cast<unsigned char>(input_bytes[1]) == 0xff) big_endian = true;
      }
      if (decodable_size >= 2) {
        const size_t last = decodable_size - 2;
        const uint16_t unit = big_endian
            ? (static_cast<unsigned char>(input_bytes[last]) << 8) |
                  static_cast<unsigned char>(input_bytes[last + 1])
            : static_cast<unsigned char>(input_bytes[last]) |
                  (static_cast<unsigned char>(input_bytes[last + 1]) << 8);
        if (unit >= 0xd800 && unit <= 0xdbff) decodable_size -= 2;
      }
    } else {
      decodable_size -= decodable_size % 4;
    }
    if (decodable_size != input_bytes.size()) {
      Value decoded;
      const Value prefix = Value::bytes(input_bytes.substr(0, decodable_size));
      if (!decode_with_codec(runtime, prefix, *encoding,
                             normalized_errors(args + offset, argc - offset, 1), decoded, error)) {
        return false;
      }
      if ((*encoding == "utf_16" || *encoding == "utf_32") && real_argc >= 4) {
        int64_t byteorder = args[offset + 2].tag == ValueTag::Int64 ? args[offset + 2].as.i64 : 0;
        if (byteorder == 0 && *encoding == "utf_16" && input_bytes.size() >= 2) {
          if (static_cast<unsigned char>(input_bytes[0]) == 0xff &&
              static_cast<unsigned char>(input_bytes[1]) == 0xfe) byteorder = -1;
          else if (static_cast<unsigned char>(input_bytes[0]) == 0xfe &&
                   static_cast<unsigned char>(input_bytes[1]) == 0xff) byteorder = 1;
        } else if (byteorder == 0 && *encoding == "utf_32" && input_bytes.size() >= 4) {
          if (static_cast<unsigned char>(input_bytes[0]) == 0xff &&
              static_cast<unsigned char>(input_bytes[1]) == 0xfe &&
              static_cast<unsigned char>(input_bytes[2]) == 0x00 &&
              static_cast<unsigned char>(input_bytes[3]) == 0x00) byteorder = -1;
          else if (static_cast<unsigned char>(input_bytes[0]) == 0x00 &&
                   static_cast<unsigned char>(input_bytes[1]) == 0x00 &&
                   static_cast<unsigned char>(input_bytes[2]) == 0xfe &&
                   static_cast<unsigned char>(input_bytes[3]) == 0xff) byteorder = 1;
        }
        out = Value::tuple({decoded, Value::int64(static_cast<int64_t>(decodable_size)),
                            Value::int64(byteorder)});
      } else {
        out = Value::tuple({decoded, Value::int64(static_cast<int64_t>(decodable_size))});
      }
      return true;
    }
  }
  Value decoded;
  const Value decode_input = escape_codec ? Value::bytes(input_bytes) : args[offset];
  if (!decode_with_codec(runtime, decode_input, encoding == nullptr ? "utf_8" : *encoding,
                         normalized_errors(args + offset, argc - offset, 1), decoded, error)) {
    return false;
  }
  if (encoding != nullptr && (*encoding == "utf_16" || *encoding == "utf_32") && argc - offset >= 4) {
    int64_t byteorder = args[offset + 2].tag == ValueTag::Int64 ? args[offset + 2].as.i64 : 0;
    if (byteorder == 0 && *encoding == "utf_16" && input_bytes.size() >= 2) {
      if (static_cast<unsigned char>(input_bytes[0]) == 0xff && static_cast<unsigned char>(input_bytes[1]) == 0xfe) byteorder = -1;
      else if (static_cast<unsigned char>(input_bytes[0]) == 0xfe && static_cast<unsigned char>(input_bytes[1]) == 0xff) byteorder = 1;
    } else if (byteorder == 0 && *encoding == "utf_32" && input_bytes.size() >= 4) {
      if (static_cast<unsigned char>(input_bytes[0]) == 0xff && static_cast<unsigned char>(input_bytes[1]) == 0xfe &&
          static_cast<unsigned char>(input_bytes[2]) == 0x00 && static_cast<unsigned char>(input_bytes[3]) == 0x00) byteorder = -1;
      else if (static_cast<unsigned char>(input_bytes[0]) == 0x00 && static_cast<unsigned char>(input_bytes[1]) == 0x00 &&
               static_cast<unsigned char>(input_bytes[2]) == 0xfe && static_cast<unsigned char>(input_bytes[3]) == 0xff) byteorder = 1;
    }
    out = Value::tuple({decoded, Value::int64(static_cast<int64_t>(input_bytes.size())), Value::int64(byteorder)});
    return true;
  }
  out = Value::tuple({decoded, Value::int64(static_cast<int64_t>(input_bytes.size()))});
  return true;
}

bool codecs_lookup(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "codecs.lookup() expected encoding name";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string name = canonical_encoding(string_object_to_string(*value_as_string(args[0])));
  if (codec_search_registry().empty()) {
    Value encodings_module;
    std::string ignored;
    if (mapping_get_item(runtime.module_registry_dict(), Value::string("encodings"), encodings_module, ignored)) {
      Value search_function;
      if (object_get_attr(encodings_module, "search_function", search_function, ignored)) {
        codec_search_registry().push_back(std::move(search_function));
      }
    }
    if (codec_search_registry().empty()) {
      if (const Value* import_function = runtime.find_builtin("__import__")) {
        Value import_arg = Value::string("encodings");
        if (runtime_call_callable(runtime, *import_function, &import_arg, 1, encodings_module, ignored)) {
          Value search_function;
          if (object_get_attr(encodings_module, "search_function", search_function, ignored)) {
            codec_search_registry().push_back(std::move(search_function));
          }
        }
      }
    }
  }
  if (auto cached = codec_lookup_cache().find(name); cached != codec_lookup_cache().end()) {
    out = cached->second;
    return true;
  }
  if (!codec_search_registry().empty()) {
    if (codec_lookup_via_registry(runtime, name, out, error)) {
      codec_lookup_cache()[name] = out;
      return true;
    }
    if (!error.empty()) return false;
  }
  if (name != "utf_8" && name != "utf_8_sig" && name != "ascii" && name != "latin_1" &&
      name != "cp437" && name != "idna" && name != "hex" && name != "gbk") {
    error = "unknown encoding: " + name;
    runtime.raise_class_error("LookupError", error);
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("codecs")});
  Value klass = Value::class_object("CodecInfo", std::move(attrs));
  out = Value::instance(klass);
  object_set_attr(out, "name", Value::string(name), error);
  object_set_attr(
      out,
      "encode",
      runtime.make_native_function("codecs.CodecInfo.encode", codec_info_encode, new std::string(name), string_user_data_cleanup),
      error);
  object_set_attr(
      out,
      "decode",
      runtime.make_native_function("codecs.CodecInfo.decode", codec_info_decode, new std::string(name), string_user_data_cleanup),
      error);
  codec_lookup_cache()[name] = out;
  return true;
}

bool codecs_transform_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data,
    NativeFunctionCallback callback,
    const char* function_name) {
  if (argc > 3) {
    error = std::string(function_name) + "() expected at most 3 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::vector<Value> values = {Value::none(), Value::string("utf-8"), Value::string("strict")};
  bool supplied[3] = {false, false, false};
  for (uint32_t index = 0; index < argc; ++index) {
    values[index] = args[index];
    supplied[index] = true;
  }
  static const char* names[] = {"obj", "encoding", "errors"};
  for (uint32_t index = 0; index < kwargc; ++index) {
    const std::string name = kwargs[index].name == nullptr ? std::string() : kwargs[index].name;
    size_t destination = 3;
    for (size_t candidate = 0; candidate < 3; ++candidate) {
      if (name == names[candidate]) {
        destination = candidate;
        break;
      }
    }
    if (destination == 3) {
      error = std::string(function_name) + "() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (supplied[destination]) {
      error = std::string(function_name) + "() got multiple values for argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (kwargs[index].value == nullptr) {
      error = std::string(function_name) + "() received an invalid keyword value";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    values[destination] = *kwargs[index].value;
    supplied[destination] = true;
  }
  if (!supplied[0]) {
    error = std::string(function_name) + "() missing required argument 'obj'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return callback(runtime, values.data(), 3, out, error, user_data);
}

bool codecs_encode_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc,
    Value& out, std::string& error, void* user_data) {
  return codecs_transform_kw(
      runtime, args, argc, kwargs, kwargc, out, error, user_data, codecs_encode, "encode");
}

bool codecs_decode_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc,
    Value& out, std::string& error, void* user_data) {
  return codecs_transform_kw(
      runtime, args, argc, kwargs, kwargc, out, error, user_data, codecs_decode, "decode");
}

bool codecs_warn_invalid_escape(Runtime& runtime, const std::string& message, std::string& error) {
  Value warnings_module;
  if (!runtime.import_module("warnings", warnings_module, error)) return false;
  Value warn;
  if (!module_get_attr(warnings_module, "warn", warn, error)) return false;
  const Value* warning_class = runtime.find_builtin("DeprecationWarning");
  Value args[3] = {
      Value::string(message),
      warning_class == nullptr ? Value::none() : *warning_class,
      Value::int64(2)};
  Value ignored;
  return runtime_call_callable(runtime, warn, args, 3, ignored, error);
}

bool codecs_escape_decode(
    Runtime& runtime, const Value* args, uint32_t argc,
    Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "escape_decode() expected a bytes-like object and optional errors";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string input;
  if (!value_bytes_text(args[0], input)) {
    error = "escape_decode() argument must be a bytes-like object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string errors = normalized_errors(args, argc, 1);
  std::string decoded;
  decoded.reserve(input.size());
  const auto hex_digit = [](unsigned char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  };
  for (size_t index = 0; index < input.size();) {
    const unsigned char ch = static_cast<unsigned char>(input[index]);
    if (ch == '\\' && index + 1 >= input.size()) {
      error = "Trailing \\ in string";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    if (ch != '\\') {
      decoded.push_back(static_cast<char>(ch));
      ++index;
      continue;
    }
    const unsigned char escaped = static_cast<unsigned char>(input[index + 1]);
    if (escaped == '\n') {
      index += 2;
      continue;
    }
    const char* simple = "\\'\"abtnvfr";
    const char* replacements = "\\'\"\a\b\t\n\v\f\r";
    const char* found = std::strchr(simple, static_cast<char>(escaped));
    if (found != nullptr) {
      decoded.push_back(replacements[found - simple]);
      index += 2;
      continue;
    }
    if (escaped >= '0' && escaped <= '7') {
      size_t end = index + 1;
      unsigned int value = 0;
      while (end < input.size() && end < index + 4 && input[end] >= '0' && input[end] <= '7') {
        value = (value << 3) | static_cast<unsigned int>(input[end] - '0');
        ++end;
      }
      if (value > 0xff) {
        const std::string sequence = input.substr(index, end - index);
        if (!codecs_warn_invalid_escape(
                runtime, "\"" + sequence + "\" is an invalid octal escape sequence", error)) {
          return false;
        }
      }
      decoded.push_back(static_cast<char>(value & 0xff));
      index = end;
      continue;
    }
    if (escaped == 'x') {
      const bool valid = index + 3 < input.size() &&
          hex_digit(static_cast<unsigned char>(input[index + 2])) >= 0 &&
          hex_digit(static_cast<unsigned char>(input[index + 3])) >= 0;
      if (valid) {
        decoded.push_back(static_cast<char>(
            (hex_digit(static_cast<unsigned char>(input[index + 2])) << 4) |
            hex_digit(static_cast<unsigned char>(input[index + 3]))));
        index += 4;
        continue;
      }
      size_t end = index + 2;
      if (end < input.size() && hex_digit(static_cast<unsigned char>(input[end])) >= 0) ++end;
      if (errors == "ignore" || errors == "replace") {
        if (errors == "replace") decoded.push_back('?');
        index = end;
        continue;
      }
      error = "invalid \\x escape";
      runtime.raise_class_error("ValueError", error);
      return false;
    }
    std::string display;
    display.push_back('\\');
    if (escaped >= 0x20 && escaped < 0x7f) {
      display.push_back(static_cast<char>(escaped));
    } else {
      append_utf8(escaped, display);
    }
    if (!codecs_warn_invalid_escape(
            runtime, "\"" + display + "\" is an invalid escape sequence", error)) {
      return false;
    }
    decoded.push_back('\\');
    decoded.push_back(static_cast<char>(escaped));
    index += 2;
  }
  out = Value::tuple({Value::bytes(std::move(decoded)), Value::int64(static_cast<int64_t>(input.size()))});
  return true;
}

bool codecs_escape_encode(
    Runtime& runtime, const Value* args, uint32_t argc,
    Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "escape_encode() expected a bytes-like object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string input;
  const auto* bytes = value_as_bytes(args[0]);
  if (bytes == nullptr) {
    error = "escape_encode() argument must be bytes";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  input = std::string(bytes_object_view(*bytes));
  static const char digits[] = "0123456789abcdef";
  std::string encoded;
  encoded.reserve(input.size());
  for (unsigned char ch : input) {
    if (ch == '\\' || ch == '\'') {
      encoded.push_back('\\');
      encoded.push_back(static_cast<char>(ch));
    } else if (ch == '\t') {
      encoded += "\\t";
    } else if (ch == '\n') {
      encoded += "\\n";
    } else if (ch == '\r') {
      encoded += "\\r";
    } else if (ch < 0x20 || ch >= 0x7f) {
      encoded += "\\x";
      encoded.push_back(digits[ch >> 4]);
      encoded.push_back(digits[ch & 0xf]);
    } else {
      encoded.push_back(static_cast<char>(ch));
    }
  }
  out = Value::tuple({Value::bytes(std::move(encoded)), Value::int64(static_cast<int64_t>(input.size()))});
  return true;
}

bool codecs_readbuffer_encode(
    Runtime& runtime, const Value* args, uint32_t argc,
    Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "readbuffer_encode() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string data;
  if (value_text(args[0], data) || value_bytes_text(args[0], data)) {
    out = Value::tuple({Value::bytes(data), Value::int64(static_cast<int64_t>(data.size()))});
    return true;
  }
  if (value_as_memoryview(args[0]) == nullptr && value_as_instance(args[0]) == nullptr) {
    error = "readbuffer_encode() argument must support the buffer protocol";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const Value* bytes_constructor = runtime.find_builtin("bytes");
  Value converted;
  if (bytes_constructor == nullptr ||
      !runtime_call_callable(runtime, *bytes_constructor, args, 1, converted, error) ||
      !value_bytes_text(converted, data)) {
    if (error.empty()) error = "readbuffer_encode() argument must support the buffer protocol";
    return false;
  }
  out = Value::tuple({std::move(converted), Value::int64(static_cast<int64_t>(data.size()))});
  return true;
}

bool codecs_register(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "codecs.register() expected a search function";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value call_method;
  std::string ignored;
  const bool callable = value_as_function(args[0]) != nullptr ||
      value_as_native_function(args[0]) != nullptr ||
      value_as_bound_method(args[0]) != nullptr ||
      value_as_class(args[0]) != nullptr ||
      object_get_attr(args[0], "__call__", call_method, ignored);
  if (!callable) {
    error = "argument must be callable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  codec_search_registry().push_back(args[0]);
  out = Value::none();
  return true;
}

bool codecs_getencoder(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value info;
  if (!codecs_lookup(runtime, args, argc, info, error, nullptr)) {
    return false;
  }
  return object_get_attr(info, "encode", out, error);
}

bool codecs_getdecoder(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value info;
  if (!codecs_lookup(runtime, args, argc, info, error, nullptr)) {
    return false;
  }
  return object_get_attr(info, "decode", out, error);
}

std::unordered_map<std::string, Value>& error_handler_registry() {
  static std::unordered_map<std::string, Value> handlers;
  return handlers;
}

bool codecs_strict_errors(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "strict error handler re-raises codec exceptions";
  runtime.raise_class_error("UnicodeError", error);
  return false;
}

bool codecs_ignore_errors(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::tuple({Value::string(""), Value::int64(0)});
  return true;
}

bool codecs_replace_errors(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::tuple({Value::string("\xef\xbf\xbd"), Value::int64(0)});
  return true;
}

bool codecs_text_replace_errors(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::tuple({Value::string("?"), Value::int64(0)});
  return true;
}

bool codecs_lookup_error(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "codecs.lookup_error() expected error handler name";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string name = normalize_encoding(string_object_to_string(*value_as_string(args[0])));
  if (name == "strict") {
    out = runtime.make_native_function("codecs.strict_errors", codecs_strict_errors);
    return true;
  }
  if (name == "ignore") {
    out = runtime.make_native_function("codecs.ignore_errors", codecs_ignore_errors);
    return true;
  }
  if (name == "replace") {
    out = runtime.make_native_function("codecs.replace_errors", codecs_replace_errors);
    return true;
  }
  if (name == "xmlcharrefreplace" || name == "backslashreplace" || name == "namereplace" ||
      name == "surrogatepass" || name == "surrogateescape") {
    out = runtime.make_native_function("codecs." + name + "_errors", codecs_text_replace_errors);
    return true;
  }
  auto it = error_handler_registry().find(name);
  if (it != error_handler_registry().end()) {
    out = it->second;
    return true;
  }
  error = "unknown error handler name '" + name + "'";
  runtime.raise_class_error("LookupError", error);
  return false;
}

bool codecs_register_error(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || value_as_string(args[0]) == nullptr) {
    error = "codecs.register_error() expected name and handler";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string name = normalize_encoding(string_object_to_string(*value_as_string(args[0])));
  error_handler_registry()[name] = args[1];
  out = Value::none();
  return true;
}

bool codecs_unregister(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                       std::string& error, void*) {
  if (argc != 1) {
    error = "codecs.unregister() expected a search function";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto& registry = codec_search_registry();
  registry.erase(std::remove_if(registry.begin(), registry.end(), [&](const Value& candidate) {
    if (candidate.tag != args[0].tag) return false;
    if (candidate.tag == ValueTag::Object) return candidate.as.obj == args[0].as.obj;
    return false;
  }), registry.end());
  codec_lookup_cache().clear();
  out = Value::none();
  return true;
}

bool codecs_charmap_build(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                          std::string& error, void*) {
  if (argc != 1 || value_as_string(args[0]) == nullptr) {
    error = "charmap_build() argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string table = string_object_to_string(*value_as_string(args[0]));
  std::vector<std::pair<Value, Value>> entries;
  entries.reserve(256);
  size_t character_index = 0;
  for (size_t offset = 0; offset < table.size();) {
    const unsigned char lead = static_cast<unsigned char>(table[offset]);
    size_t width = utf8_codepoint_width(lead);
    if (width == 0 || offset + width > table.size()) width = 1;
    const uint32_t codepoint = decode_utf8_codepoint(std::string_view(table).substr(offset), width);
    if (codepoint != 0xfffe && character_index <= 255) {
      entries.push_back({Value::int64(static_cast<int64_t>(codepoint)),
                         Value::int64(static_cast<int64_t>(character_index))});
    }
    offset += width;
    ++character_index;
  }
  if (character_index != 256) {
    error = "charmap_build() argument must contain 256 characters";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  out = Value::dict(std::move(entries));
  return true;
}

bool unicode_table_codepoint(std::string_view table, size_t wanted, uint32_t& codepoint) {
  size_t index = 0;
  for (size_t offset = 0; offset < table.size();) {
    const size_t width = utf8_codepoint_width(static_cast<unsigned char>(table[offset]));
    if (width == 0 || offset + width > table.size()) return false;
    if (index++ == wanted) {
      codepoint = decode_utf8_codepoint(table.substr(offset), width);
      return true;
    }
    offset += width;
  }
  return false;
}

bool codecs_charmap_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                           std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "charmap_decode() expected input, optional errors and mapping";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string input;
  if (!value_bytes_text(args[0], input)) {
    error = "charmap_decode() argument 1 must be bytes-like";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string errors = normalized_errors(args, argc, 1);
  const Value* mapping = argc >= 3 ? &args[2] : nullptr;
  std::string decoded;
  for (size_t i = 0; i < input.size(); ++i) {
    const uint32_t byte = static_cast<unsigned char>(input[i]);
    Value mapped;
    bool defined = true;
    if (mapping == nullptr || mapping->tag == ValueTag::None) {
      mapped = Value::int64(byte);
    } else if (auto* table = value_as_string(*mapping)) {
      uint32_t cp = 0;
      defined = unicode_table_codepoint(string_object_view(*table), byte, cp) && cp != 0xfffe;
      if (defined) mapped = Value::int64(cp);
    } else if (!mapping_get_item(*mapping, Value::int64(byte), mapped, error)) {
      error.clear();
      defined = false;
    }
    if (defined && value_as_string(mapped) != nullptr &&
        string_object_view(*value_as_string(mapped)).find("\xef\xbf\xbe") != std::string_view::npos) {
      defined = false;
    }
    if (defined && mapped.tag == ValueTag::Int64 && (mapped.as.i64 < 0 || mapped.as.i64 > 0x10ffff)) {
      error = "character mapping must be in range(0x110000)";
      runtime.raise_class_error("TypeError", error);
      return false;
    } else if (defined && mapped.tag == ValueTag::Int64 && mapped.as.i64 != 0xfffe) {
      append_utf8(static_cast<uint32_t>(mapped.as.i64), decoded);
    } else if (defined && value_as_string(mapped) != nullptr) {
      decoded += string_object_to_string(*value_as_string(mapped));
    } else if (errors == "ignore") {
      continue;
    } else if (errors == "replace") {
      decoded += "\xef\xbf\xbd";
    } else if (errors == "backslashreplace") {
      static constexpr char digits[] = "0123456789abcdef";
      decoded += "\\x";
      decoded.push_back(digits[byte >> 4]);
      decoded.push_back(digits[byte & 0xf]);
    } else if (errors == "surrogateescape" && byte >= 0x80) {
      append_utf8(0xdc00u + byte, decoded);
    } else {
      error = "character maps to <undefined>";
      runtime.raise_class_error("UnicodeDecodeError", error);
      return false;
    }
  }
  out = Value::tuple({Value::string(std::move(decoded)), Value::int64(static_cast<int64_t>(input.size()))});
  return true;
}

bool codecs_charmap_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                           std::string& error, void*) {
  if (argc < 1 || argc > 3 || value_as_string(args[0]) == nullptr) {
    error = "charmap_encode() argument 1 must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const std::string input = string_object_to_string(*value_as_string(args[0]));
  const std::string errors = normalized_errors(args, argc, 1);
  const Value* mapping = argc >= 3 ? &args[2] : nullptr;
  std::string encoded;
  size_t consumed = 0;
  for (size_t offset = 0; offset < input.size();) {
    size_t width = utf8_codepoint_width(static_cast<unsigned char>(input[offset]));
    if (width == 0 || offset + width > input.size()) width = 1;
    const uint32_t cp = decode_utf8_codepoint(std::string_view(input).substr(offset), width);
    Value mapped;
    bool defined = true;
    if (mapping == nullptr || mapping->tag == ValueTag::None) {
      defined = cp <= 255;
      if (defined) mapped = Value::int64(cp);
    } else if (!mapping_get_item(*mapping, Value::int64(cp), mapped, error)) {
      error.clear();
      defined = false;
    }
    if (defined && mapped.tag == ValueTag::Int64 && mapped.as.i64 >= 0 && mapped.as.i64 <= 255) {
      encoded.push_back(static_cast<char>(mapped.as.i64));
    } else if (defined && value_as_bytes(mapped) != nullptr) {
      encoded += std::string(bytes_object_view(*value_as_bytes(mapped)));
    } else if (errors == "ignore") {
    } else if (errors == "replace") {
      encoded.push_back('?');
    } else if (errors == "backslashreplace") {
      append_ascii_backslash_escape(cp, encoded);
    } else if (errors == "xmlcharrefreplace") {
      encoded += "&#" + std::to_string(cp) + ";";
    } else if (errors == "surrogateescape" && cp >= 0xdc80 && cp <= 0xdcff) {
      encoded.push_back(static_cast<char>(cp - 0xdc00));
    } else {
      error = "character maps to <undefined>";
      runtime.raise_class_error("UnicodeEncodeError", error);
      return false;
    }
    offset += width;
    ++consumed;
  }
  out = Value::tuple({Value::bytes(std::move(encoded)), Value::int64(static_cast<int64_t>(consumed))});
  return true;
}

} // namespace

void register_codecs_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_codecs");
  auto codec_function = [&](const char* name, NativeFunctionCallback callback, const char* encoding) {
    Value function = runtime.make_native_function(
        name, callback, new std::string(encoding), string_user_data_cleanup,
        nullptr, false, nullptr, false);
    const std::string full_name(name);
    const size_t separator = full_name.rfind('.');
    std::string ignored;
    object_set_attr(
        function, "__qualname__",
        Value::string(separator == std::string::npos ? full_name : full_name.substr(separator + 1)),
        ignored);
    return function;
  };
  builder.function("lookup", codecs_lookup)
      .function("register", codecs_register)
      .function("unregister", codecs_unregister)
      .function("encode", codecs_encode, nullptr, false, codecs_encode_kw)
      .function("decode", codecs_decode, nullptr, false, codecs_decode_kw)
      .function("escape_encode", codecs_escape_encode)
      .function("escape_decode", codecs_escape_decode)
      .function("readbuffer_encode", codecs_readbuffer_encode)
      .function("getencoder", codecs_getencoder)
      .function("getdecoder", codecs_getdecoder)
      .function("lookup_error", codecs_lookup_error)
      .function("register_error", codecs_register_error)
      .function("charmap_build", codecs_charmap_build)
      .function("charmap_encode", codecs_charmap_encode)
      .function("charmap_decode", codecs_charmap_decode)
      .value("ascii_encode", codec_function("_codecs.ascii_encode", codec_info_encode, "ascii"))
      .value("ascii_decode", codec_function("_codecs.ascii_decode", codec_info_decode, "ascii"))
      .value("latin_1_encode", codec_function("_codecs.latin_1_encode", codec_info_encode, "latin_1"))
      .value("latin_1_decode", codec_function("_codecs.latin_1_decode", codec_info_decode, "latin_1"))
      .value("utf_8_encode", codec_function("_codecs.utf_8_encode", codec_info_encode, "utf_8"))
      .value("utf_8_decode", codec_function("_codecs.utf_8_decode", codec_info_decode, "utf_8"))
      .value("raw_unicode_escape_encode", codec_function("_codecs.raw_unicode_escape_encode", codec_info_encode, "raw_unicode_escape"))
      .value("raw_unicode_escape_decode", codec_function("_codecs.raw_unicode_escape_decode", codec_info_decode, "raw_unicode_escape"))
      .value("unicode_escape_encode", codec_function("_codecs.unicode_escape_encode", codec_info_encode, "unicode_escape"))
      .value("unicode_escape_decode", codec_function("_codecs.unicode_escape_decode", codec_info_decode, "unicode_escape"))
      .value("utf_7_encode", codec_function("_codecs.utf_7_encode", codec_info_encode, "utf_7"))
      .value("utf_7_decode", codec_function("_codecs.utf_7_decode", codec_info_decode, "utf_7"))
      .value("utf_16_encode", codec_function("_codecs.utf_16_encode", codec_info_encode, "utf_16"))
      .value("utf_16_decode", codec_function("_codecs.utf_16_decode", codec_info_decode, "utf_16"))
      .value("utf_16_le_encode", codec_function("_codecs.utf_16_le_encode", codec_info_encode, "utf_16_le"))
      .value("utf_16_le_decode", codec_function("_codecs.utf_16_le_decode", codec_info_decode, "utf_16_le"))
      .value("utf_16_be_encode", codec_function("_codecs.utf_16_be_encode", codec_info_encode, "utf_16_be"))
      .value("utf_16_be_decode", codec_function("_codecs.utf_16_be_decode", codec_info_decode, "utf_16_be"))
      .value("utf_16_ex_decode", codec_function("_codecs.utf_16_ex_decode", codec_info_decode, "utf_16"))
      .value("utf_32_encode", codec_function("_codecs.utf_32_encode", codec_info_encode, "utf_32"))
      .value("utf_32_decode", codec_function("_codecs.utf_32_decode", codec_info_decode, "utf_32"))
      .value("utf_32_le_encode", codec_function("_codecs.utf_32_le_encode", codec_info_encode, "utf_32_le"))
      .value("utf_32_le_decode", codec_function("_codecs.utf_32_le_decode", codec_info_decode, "utf_32_le"))
      .value("utf_32_be_encode", codec_function("_codecs.utf_32_be_encode", codec_info_encode, "utf_32_be"))
      .value("utf_32_be_decode", codec_function("_codecs.utf_32_be_decode", codec_info_decode, "utf_32_be"))
      .value("utf_32_ex_decode", codec_function("_codecs.utf_32_ex_decode", codec_info_decode, "utf_32"))
#if defined(_WIN32)
      .function("code_page_encode", codecs_code_page_encode)
      .function("code_page_decode", codecs_code_page_decode)
#endif
      .value("BOM_UTF8", Value::bytes(std::string("\xEF\xBB\xBF", 3)))
      .value("BOM", Value::bytes({}));
  runtime.register_module("_codecs", builder.finish());
}

} // namespace xlang3
