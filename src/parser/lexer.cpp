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
#include "xlang3/parser.h"

#include "xlang3/builtins.h"
#include "xlang3/source_cursor.h"

#include <cctype>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace xlang3 {

namespace {

std::string_view trim_left_ascii(std::string_view text) {
  size_t offset = 0;
  while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t')) {
    ++offset;
  }
  return text.substr(offset);
}

std::string_view trim_inline_comment_for_join(std::string_view line) {
  bool in_string = false;
  char quote = 0;
  bool escaped = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == quote) {
        in_string = false;
      }
      continue;
    }
    if (ch == '#') {
      return line.substr(0, i);
    }
    if (ch == '"' || ch == '\'') {
      in_string = true;
      quote = ch;
    }
  }
  return line;
}

bool update_line_join_state(std::string_view line, int& bracket_depth, bool& explicit_continue) {
  explicit_continue = false;
  bool in_string = false;
  char quote = 0;
  bool escaped = false;
  size_t last_non_space = std::string_view::npos;

  for (size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (ch != ' ' && ch != '\t') {
      last_non_space = i;
    }

    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == quote) {
        in_string = false;
      }
      continue;
    }

    if (ch == '#') {
      break;
    }
    if (ch == '"' || ch == '\'') {
      if (i + 2 < line.size() && line[i + 1] == ch && line[i + 2] == ch) {
        const size_t close = line.find(std::string_view(line.data() + i, 3), i + 3);
        if (close == std::string_view::npos) {
          break;
        }
        i = close + 2;
        continue;
      }
      in_string = true;
      quote = ch;
      continue;
    }
    if (ch == '(' || ch == '[' || ch == '{') {
      ++bracket_depth;
    } else if ((ch == ')' || ch == ']' || ch == '}') && bracket_depth > 0) {
      --bracket_depth;
    }
  }

  explicit_continue = last_non_space != std::string_view::npos && line[last_non_space] == '\\';
  return bracket_depth > 0 || explicit_continue;
}

bool is_name_char(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' ||
      static_cast<unsigned char>(ch) >= 0x80u;
}

bool decode_identifier_codepoint(std::string_view text, size_t offset, uint32_t& codepoint, size_t& width) {
  if (offset >= text.size()) return false;
  const unsigned char lead = static_cast<unsigned char>(text[offset]);
  if (lead < 0x80u) {
    codepoint = lead;
    width = 1;
    return true;
  }
  width = utf8_codepoint_width(lead);
  if (width < 2 || offset + width > text.size()) return false;
  codepoint = lead & ((1u << (7u - static_cast<unsigned>(width))) - 1u);
  for (size_t i = 1; i < width; ++i) {
    const unsigned char continuation = static_cast<unsigned char>(text[offset + i]);
    if ((continuation & 0xc0u) != 0x80u) return false;
    codepoint = (codepoint << 6u) | (continuation & 0x3fu);
  }
  if ((width == 2 && codepoint < 0x80u) ||
      (width == 3 && codepoint < 0x800u) ||
      (width == 4 && codepoint < 0x10000u) ||
      codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
    return false;
  }
  return true;
}

bool unicode_identifier_codepoint(uint32_t codepoint, bool first) {
  if (codepoint == '_') return true;
  if (codepoint < 0x80u) {
    return std::isalpha(static_cast<unsigned char>(codepoint)) != 0 ||
        (!first && std::isdigit(static_cast<unsigned char>(codepoint)) != 0);
  }
#ifdef _WIN32
  wchar_t units[2]{};
  int count = 1;
  if (codepoint <= 0xffffu) {
    units[0] = static_cast<wchar_t>(codepoint);
  } else {
    const uint32_t adjusted = codepoint - 0x10000u;
    units[0] = static_cast<wchar_t>(0xd800u + (adjusted >> 10u));
    units[1] = static_cast<wchar_t>(0xdc00u + (adjusted & 0x3ffu));
    count = 2;
  }
  WORD type1[2]{};
  WORD type3[2]{};
  if (GetStringTypeW(CT_CTYPE1, units, count, type1) == 0 ||
      GetStringTypeW(CT_CTYPE3, units, count, type3) == 0) {
    return false;
  }
  const WORD combined_type1 = static_cast<WORD>(type1[0] | type1[1]);
  const WORD combined_type3 = static_cast<WORD>(type3[0] | type3[1]);
  const bool alphabetic = (combined_type1 & C1_ALPHA) != 0 ||
      (codepoint >= 0x1d400u && codepoint <= 0x1d7cbu);
  if (first) {
    return alphabetic || codepoint == 0x1885u || codepoint == 0x1886u ||
        codepoint == 0x2118u || codepoint == 0x212eu ||
        codepoint == 0x309bu || codepoint == 0x309cu;
  }
  return alphabetic || (combined_type1 & C1_DIGIT) != 0 ||
      (combined_type3 & (C3_NONSPACING | C3_DIACRITIC)) != 0 ||
      codepoint == 0x00b7u || codepoint == 0x0387u ||
      (codepoint >= 0x1369u && codepoint <= 0x1371u) || codepoint == 0x19dau;
#else
  // The runtime's supported source encoding is UTF-8. Platform builds without
  // Win32 character properties accept a well-formed non-ASCII code point here;
  // semantic identifier validation remains available through str.isidentifier.
  return true;
#endif
}

bool identifier_codepoint_at(std::string_view text, size_t offset, bool first, size_t& width) {
  uint32_t codepoint = 0;
  return decode_identifier_codepoint(text, offset, codepoint, width) &&
      unicode_identifier_codepoint(codepoint, first);
}

bool is_raw_string_prefix(char ch) {
  return ch == 'r' || ch == 'R';
}

bool is_string_prefix_char(char ch) {
  return ch == 'r' || ch == 'R' || ch == 'b' || ch == 'B' || ch == 'f' || ch == 'F' ||
         ch == 't' || ch == 'T' || ch == 'u' || ch == 'U';
}

int hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

void append_utf8(uint32_t codepoint, std::string& out) {
  if (codepoint <= 0x7f) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else if (codepoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else {
    out.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  }
}

bool append_hex_escape(std::string_view text, size_t& i, size_t digits, std::string& out) {
  if (i + digits > text.size()) {
    return false;
  }
  uint32_t value = 0;
  for (size_t n = 0; n < digits; ++n) {
    const int digit = hex_digit(text[i + n]);
    if (digit < 0) {
      return false;
    }
    value = (value << 4u) | static_cast<uint32_t>(digit);
  }
  i += digits;
  append_utf8(value, out);
  return true;
}

bool append_hex_byte_escape(std::string_view text, size_t& i, std::string& out) {
  if (i + 2 > text.size()) {
    return false;
  }
  const int hi = hex_digit(text[i]);
  const int lo = hex_digit(text[i + 1]);
  if (hi < 0 || lo < 0) {
    return false;
  }
  out.push_back(static_cast<char>((hi << 4) | lo));
  i += 2;
  return true;
}

std::string decode_string_content(std::string_view text, bool raw, bool bytes) {
  if (raw) {
    return std::string(text);
  }
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    if (text[i] != '\\' || i + 1 >= text.size()) {
      out.push_back(text[i++]);
      continue;
    }
    ++i;
    const char esc = text[i++];
    switch (esc) {
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'a': out.push_back('\a'); break;
      case 'v': out.push_back('\v'); break;
      case '\n': break;
      case '\r':
        if (i < text.size() && text[i] == '\n') {
          ++i;
        }
        break;
      case '\\':
      case '\'':
      case '"':
        out.push_back(esc);
        break;
      case 'x':
        if (bytes ? !append_hex_byte_escape(text, i, out) : !append_hex_escape(text, i, 2, out)) {
          out += "\\x";
        }
        break;
      case 'u':
        if (!append_hex_escape(text, i, 4, out)) {
          out += "\\u";
        }
        break;
      case 'U':
        if (!append_hex_escape(text, i, 8, out)) {
          out += "\\U";
        }
        break;
      case 'N': {
        if (bytes || i >= text.size() || text[i] != '{') {
          out += "\\N";
          break;
        }
        const size_t close = text.find('}', i + 1);
        uint32_t codepoint = 0;
        if (close == std::string_view::npos ||
            !unicodedata_lookup_codepoint(text.substr(i + 1, close - i - 1), codepoint)) {
          out += "\\N";
          break;
        }
        append_utf8(codepoint, out);
        i = close + 1;
        break;
      }
      default:
        if (esc >= '0' && esc <= '7') {
          uint32_t value = static_cast<uint32_t>(esc - '0');
          size_t digits = 1;
          while (digits < 3 && i < text.size() && text[i] >= '0' && text[i] <= '7') {
            value = (value << 3u) | static_cast<uint32_t>(text[i] - '0');
            ++i;
            ++digits;
          }
          if (bytes) {
            out.push_back(static_cast<char>(value & 0xffu));
          } else {
            append_utf8(value, out);
          }
        } else {
          out.push_back(esc);
        }
        break;
    }
  }
  return out;
}

struct StringPrefix {
  size_t start = 0;
  size_t quote = 0;
  bool raw = false;
  bool bytes = false;
  bool fstring = false;
  bool template_string = false;
  bool valid = false;
};

StringPrefix detect_string_prefix_for_quote(std::string_view line, size_t quote_pos) {
  StringPrefix prefix;
  prefix.start = quote_pos;
  prefix.quote = quote_pos;
  size_t start = quote_pos;
  while (start > 0 && quote_pos - start < 2 && is_string_prefix_char(line[start - 1])) {
    --start;
  }
  if (start > 0 && is_name_char(line[start - 1])) {
    start = quote_pos;
  }
  bool raw = false;
  bool bytes = false;
  bool fstring = false;
  bool template_string = false;
  bool valid = true;
  for (size_t i = start; i < quote_pos; ++i) {
    const char ch = line[i];
    if (ch == 'r' || ch == 'R') raw = true;
    else if (ch == 'b' || ch == 'B') bytes = true;
    else if (ch == 'f' || ch == 'F') fstring = true;
    else if (ch == 't' || ch == 'T') template_string = true;
    else if (ch == 'u' || ch == 'U') {}
    else valid = false;
  }
  if ((bytes && fstring) || (template_string && (bytes || fstring))) {
    valid = false;
  }
  prefix.start = valid ? start : quote_pos;
  prefix.raw = raw;
  prefix.bytes = bytes;
  prefix.fstring = fstring;
  prefix.template_string = template_string;
  prefix.valid = valid;
  return prefix;
}

StringPrefix detect_string_start(std::string_view line, size_t pos) {
  if (pos >= line.size()) {
    return {};
  }
  if (line[pos] == '"' || line[pos] == '\'') {
    auto prefix = detect_string_prefix_for_quote(line, pos);
    prefix.start = pos;
    prefix.valid = true;
    return prefix;
  }
  if (!is_string_prefix_char(line[pos])) {
    return {};
  }
  for (size_t quote = pos + 1; quote < line.size() && quote <= pos + 2; ++quote) {
    if (line[quote] == '"' || line[quote] == '\'') {
      auto prefix = detect_string_prefix_for_quote(line, quote);
      if (prefix.valid && prefix.start == pos) {
        return prefix;
      }
    }
  }
  return {};
}

struct TripleStringStart {
  StringPrefix prefix;
  std::string_view opener;
  bool found = false;
};

TripleStringStart find_first_triple_string_start(std::string_view line, size_t indent) {
  for (size_t i = indent; i < line.size();) {
    const char ch = line[i];
    if (ch == '#') {
      return {};
    }
    const auto prefix = detect_string_start(line, i);
    if (!prefix.valid) {
      ++i;
      continue;
    }

    const bool is_triple = prefix.quote + 2 < line.size() &&
                           line[prefix.quote] == line[prefix.quote + 1] &&
                           line[prefix.quote] == line[prefix.quote + 2];
    if (is_triple) {
      return TripleStringStart{
          prefix,
          line.substr(prefix.quote, 3),
          true,
      };
    }

    const char quote = line[prefix.quote];
    i = prefix.quote + 1;
    while (i < line.size()) {
      if (line[i] == '\\' && i + 1 < line.size()) {
        i += 2;
        continue;
      }
      if (line[i] == quote) {
        ++i;
        break;
      }
      ++i;
    }
  }
  return {};
}

void remove_trailing_backslash(std::string& line) {
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }
  if (line.empty() || line.back() != '\\') {
    return;
  }

  bool in_string = false;
  bool escaped = false;
  char quote = 0;
  for (size_t i = 0; i + 1 < line.size(); ++i) {
    const char ch = line[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == quote) {
        in_string = false;
      }
      continue;
    }
    if (ch == '#') {
      break;
    }
    if (ch == '"' || ch == '\'') {
      in_string = true;
      quote = ch;
    }
  }
  if (!in_string) {
    line.pop_back();
  }
}

void append_joined_line(std::string& logical_line, std::string_view line) {
  logical_line.push_back('\n');
  logical_line += std::string(trim_inline_comment_for_join(line));
}

std::string_view append_triple_string_tail(std::string& logical_line,
                                           const std::vector<SourceLine>& lines,
                                           size_t& line_index,
                                           uint32_t& logical_end_line,
                                           const TripleStringStart& triple_start) {
  const auto opener = triple_start.opener;
  std::string_view current = lines[line_index].text;
  size_t content = triple_start.prefix.quote + 3;
  size_t close = current.find(opener, content);
  while (close == std::string_view::npos && line_index + 1 < lines.size()) {
    const auto next_line = lines[++line_index];
    logical_end_line = next_line.line;
    current = next_line.text;
    logical_line.push_back('\n');
    logical_line.append(current);
    close = current.find(opener);
  }
  if (close == std::string_view::npos) {
    return {};
  }
  return current.substr(close + 3);
}

} // namespace

Lexer::Lexer(std::string_view source) : source_(source) {}

void Lexer::emit(TokenKind kind, std::string_view text, uint32_t line, uint32_t column, bool is_triple_string, bool is_raw_string) {
  tokens_.push_back(Token{kind, text, line, column, is_triple_string, is_raw_string});
}

void Lexer::emit_owned(TokenKind kind, std::string text, uint32_t line, uint32_t column, bool is_triple_string, bool is_raw_string) {
  auto owned = std::make_unique<std::string>(std::move(text));
  const std::string_view view(*owned);
  owned_text_.push_back(std::move(owned));
  tokens_.push_back(Token{kind, view, line, column, is_triple_string, is_raw_string});
}

LexResult Lexer::tokenize() {
  std::vector<SourceLine> lines;
  SourceLines source_lines(source_);
  SourceLine source_line;
  while (source_lines.next(source_line)) {
    lines.push_back(source_line);
  }

  uint32_t line_no = 1;
  for (size_t line_index = 0; line_index < lines.size(); ++line_index) {
    std::string_view line = lines[line_index].text;
    line_no = lines[line_index].line;
    uint32_t indent = 0;
    while (indent < line.size() && line[indent] == ' ') {
      ++indent;
    }
    const auto first = line.find_first_not_of(" \t\f\r");
    if (first == std::string_view::npos || line[first] == '#') {
      continue;
    }
    if (indent > indent_stack_.back()) {
      indent_stack_.push_back(indent);
      emit(TokenKind::Indent, "", line_no, 1);
    } else {
      while (indent < indent_stack_.back()) {
        indent_stack_.pop_back();
        emit(TokenKind::Dedent, "", line_no, 1);
      }
      if (indent != indent_stack_.back()) {
        errors_.push_back("line " + std::to_string(line_no) + ": inconsistent indentation");
      }
    }

    const auto triple_start = find_first_triple_string_start(line, indent);
    if (triple_start.found) {
      const uint32_t start_line_no = line_no;
      const auto prefix = triple_start.prefix;
      const size_t triple_pos = prefix.quote;
      const auto opener = triple_start.opener;
      const size_t prefix_start = prefix.start;
      if (prefix_start > indent) {
        tokenize_line(line.substr(0, prefix_start), line_no, indent);
      }
      std::string value;
      std::string suffix;
      size_t content_start = triple_pos + 3;
      size_t close = line.find(opener, content_start);
      if (close != std::string_view::npos) {
        value = std::string(line.substr(content_start, close - content_start));
        suffix = std::string(line.substr(close + 3));
      } else {
        value = std::string(line.substr(content_start));
        bool closed = false;
        while (++line_index < lines.size()) {
          line_no = lines[line_index].line;
          const auto block_line = lines[line_index].text;
          close = block_line.find(opener);
          value.push_back('\n');
          if (close != std::string_view::npos) {
            value.append(block_line.substr(0, close));
            suffix = std::string(block_line.substr(close + 3));
            closed = true;
            break;
          }
          value.append(block_line);
        }
        if (!closed) {
          errors_.push_back("line " + std::to_string(line_no) + ": unterminated triple-quoted string");
          break;
        }
      }
      const TokenKind kind = prefix.bytes ? TokenKind::Bytes :
          (prefix.fstring ? TokenKind::FString :
           (prefix.template_string ? TokenKind::TemplateString : TokenKind::String));
      if (!prefix.fstring && !prefix.template_string) {
        value = decode_string_content(value, prefix.raw, prefix.bytes);
      }
      emit_owned(kind, std::move(value), start_line_no, static_cast<uint32_t>(prefix_start + 1), true, prefix.raw);
      const auto suffix_triple = find_first_triple_string_start(suffix, 0);
      if (suffix_triple.found) {
        if (suffix_triple.prefix.start > 0) {
          auto prefix_line = std::make_unique<std::string>(
              std::string(indent, ' ') + suffix.substr(0, suffix_triple.prefix.start));
          const std::string_view prefix_view(*prefix_line);
          owned_text_.push_back(std::move(prefix_line));
          tokenize_line(prefix_view, line_no, indent);
        }
        const char suffix_quote = suffix[suffix_triple.prefix.quote];
        const std::string suffix_opener(3, suffix_quote);
        const size_t suffix_content_start = suffix_triple.prefix.quote + 3;
        size_t suffix_close = suffix.find(suffix_opener, suffix_content_start);
        std::string suffix_value;
        if (suffix_close != std::string::npos) {
          suffix_value = suffix.substr(suffix_content_start, suffix_close - suffix_content_start);
          suffix = suffix.substr(suffix_close + 3);
        } else {
          suffix_value = suffix.substr(suffix_content_start);
          bool suffix_closed = false;
          while (++line_index < lines.size()) {
            line_no = lines[line_index].line;
            const auto block_line = lines[line_index].text;
            suffix_close = block_line.find(suffix_opener);
            suffix_value.push_back('\n');
            if (suffix_close != std::string_view::npos) {
              suffix_value.append(block_line.substr(0, suffix_close));
              suffix = std::string(block_line.substr(suffix_close + 3));
              suffix_closed = true;
              break;
            }
            suffix_value.append(block_line);
          }
          if (!suffix_closed) {
            errors_.push_back("line " + std::to_string(line_no) + ": unterminated triple-quoted string");
            break;
          }
        }
        const TokenKind suffix_kind = suffix_triple.prefix.bytes ? TokenKind::Bytes :
            (suffix_triple.prefix.fstring ? TokenKind::FString :
             (suffix_triple.prefix.template_string ? TokenKind::TemplateString : TokenKind::String));
        if (!suffix_triple.prefix.fstring && !suffix_triple.prefix.template_string) {
          suffix_value = decode_string_content(
              suffix_value, suffix_triple.prefix.raw, suffix_triple.prefix.bytes);
        }
        emit_owned(
            suffix_kind,
            std::move(suffix_value),
            start_line_no,
            static_cast<uint32_t>(suffix_triple.prefix.start + 1),
            true,
            suffix_triple.prefix.raw);
      }
      if (suffix.find_first_not_of(" \t") != std::string::npos) {
        std::string logical_line(std::string(indent, ' ') + suffix);
        uint32_t logical_end_line = line_no;
        int bracket_depth = 0;
        bool explicit_continue = false;
        if (prefix_start > indent) {
          (void)update_line_join_state(line.substr(0, prefix_start), bracket_depth, explicit_continue);
          explicit_continue = false;
        }
        bool should_join = update_line_join_state(logical_line, bracket_depth, explicit_continue);
        while (should_join && line_index + 1 < lines.size()) {
          if (explicit_continue) {
            remove_trailing_backslash(logical_line);
          }
          const auto next_line = lines[++line_index];
          logical_end_line = next_line.line;
          append_joined_line(logical_line, next_line.text);
          std::string_view join_state_line = next_line.text;
          const auto continued_triple = find_first_triple_string_start(next_line.text, 0);
          if (continued_triple.found) {
            bool prefix_continue = false;
            (void)update_line_join_state(
                next_line.text.substr(0, continued_triple.prefix.start), bracket_depth, prefix_continue);
            join_state_line = append_triple_string_tail(logical_line, lines, line_index, logical_end_line, continued_triple);
          }
          should_join = update_line_join_state(join_state_line, bracket_depth, explicit_continue);
        }
        auto owned = std::make_unique<std::string>(std::move(logical_line));
        const std::string_view logical_view(*owned);
        owned_text_.push_back(std::move(owned));
        tokenize_line(logical_view, line_no, indent);
        emit(TokenKind::Newline, "", logical_end_line, static_cast<uint32_t>(logical_view.size() + 1));
      } else {
        emit(TokenKind::Newline, "", line_no, static_cast<uint32_t>(line.size() + 1));
      }
      continue;
    }

    std::string logical_line(trim_inline_comment_for_join(line));
    uint32_t logical_end_line = line_no;
    int bracket_depth = 0;
    bool explicit_continue = false;
    bool should_join = update_line_join_state(line, bracket_depth, explicit_continue);
    while (should_join && line_index + 1 < lines.size()) {
      if (explicit_continue) {
        remove_trailing_backslash(logical_line);
      }
      const auto next_line = lines[++line_index];
      logical_end_line = next_line.line;
      append_joined_line(logical_line, next_line.text);
      std::string_view join_state_line = next_line.text;
      const auto continued_triple = find_first_triple_string_start(next_line.text, 0);
      if (continued_triple.found) {
        bool prefix_continue = false;
        (void)update_line_join_state(
            next_line.text.substr(0, continued_triple.prefix.start), bracket_depth, prefix_continue);
        join_state_line = append_triple_string_tail(logical_line, lines, line_index, logical_end_line, continued_triple);
      }
      should_join = update_line_join_state(join_state_line, bracket_depth, explicit_continue);
    }

    if (logical_end_line == line_no) {
      tokenize_line(line, line_no, indent);
      emit(TokenKind::Newline, "", line_no, static_cast<uint32_t>(line.size() + 1));
    } else {
      auto owned = std::make_unique<std::string>(std::move(logical_line));
      const std::string_view logical_view(*owned);
      owned_text_.push_back(std::move(owned));
      tokenize_line(logical_view, line_no, indent);
      emit(TokenKind::Newline, "", logical_end_line, static_cast<uint32_t>(logical_view.size() + 1));
    }
  }
  while (indent_stack_.size() > 1) {
    indent_stack_.pop_back();
    emit(TokenKind::Dedent, "", line_no, 1);
  }
  emit(TokenKind::End, "", line_no, 1);
  return LexResult{std::move(tokens_), std::move(owned_text_), std::move(errors_)};
}

void Lexer::tokenize_line(std::string_view line_text, uint32_t line_no, uint32_t indent) {
  size_t i = indent;
  size_t physical_line_start = 0;
  uint32_t physical_line = line_no;
  while (i < line_text.size()) {
    const char ch = line_text[i];
    if (ch == '\n') {
      ++physical_line;
      physical_line_start = ++i;
      continue;
    }
    if (ch == '\r') {
      ++i;
      continue;
    }
    const uint32_t col = static_cast<uint32_t>(i - physical_line_start + 1);
    if (ch == ' ' || ch == '\t' || ch == '\f') {
      ++i;
      continue;
    }
    if (ch == '#') {
      return;
    }
    const auto prefix = detect_string_start(line_text, i);
    if (prefix.valid) {
      const char quote = line_text[prefix.quote];
      const bool is_triple = prefix.quote + 2 < line_text.size() &&
                             line_text[prefix.quote + 1] == quote &&
                             line_text[prefix.quote + 2] == quote;
      i = prefix.quote + (is_triple ? 3 : 1);
      std::string value;
      bool closed = false;
      if (is_triple) {
        const std::string_view opener(line_text.data() + prefix.quote, 3);
        const size_t close = line_text.find(opener, i);
        if (close != std::string_view::npos) {
          value = std::string(line_text.substr(i, close - i));
          i = close + 3;
          closed = true;
        }
      } else {
        int fstring_brace_depth = 0;
        char fstring_expr_quote = '\0';
        while (i < line_text.size()) {
          const char current = line_text[i];
          if (prefix.fstring && fstring_expr_quote != '\0') {
            value.push_back(current);
            ++i;
            if (current == '\\' && i < line_text.size()) {
              value.push_back(line_text[i++]);
              continue;
            }
            if (current == fstring_expr_quote) {
              fstring_expr_quote = '\0';
            }
            continue;
          }
          if (prefix.fstring && fstring_brace_depth > 0 && (current == '\'' || current == '"')) {
            fstring_expr_quote = current;
            value.push_back(line_text[i++]);
            continue;
          }
          if (prefix.fstring && current == '{') {
            if (i + 1 < line_text.size() && line_text[i + 1] == '{' && fstring_brace_depth == 0) {
              value.push_back(line_text[i++]);
              value.push_back(line_text[i++]);
              continue;
            }
            ++fstring_brace_depth;
            value.push_back(line_text[i++]);
            continue;
          }
          if (prefix.fstring && current == '}' && fstring_brace_depth > 0) {
            --fstring_brace_depth;
            value.push_back(line_text[i++]);
            continue;
          }
          if (current == quote && (!prefix.fstring || fstring_brace_depth == 0)) {
            closed = true;
            break;
          }
          if (!prefix.raw && current == '\\' && i + 1 < line_text.size()) {
            value.push_back(line_text[i++]);
            value.push_back(line_text[i++]);
            continue;
          }
          if (prefix.raw && current == '\\' && i + 1 < line_text.size()) {
            value.push_back(line_text[i++]);
            value.push_back(line_text[i++]);
            continue;
          }
          value.push_back(line_text[i++]);
        }
      }
      if (!closed) {
        errors_.push_back("line " + std::to_string(physical_line) + ": unterminated string");
        return;
      }
      if (!is_triple) {
        ++i;
      }
      const TokenKind kind = prefix.bytes ? TokenKind::Bytes :
          (prefix.fstring ? TokenKind::FString :
           (prefix.template_string ? TokenKind::TemplateString : TokenKind::String));
      if (!prefix.fstring && !prefix.template_string) {
        value = decode_string_content(value, prefix.raw, prefix.bytes);
      }
      emit_owned(kind, std::move(value), physical_line, col, is_triple, prefix.raw);
      continue;
    }
    size_t identifier_width = 0;
    if (identifier_codepoint_at(line_text, i, true, identifier_width)) {
      const size_t start = i;
      i += identifier_width;
      while (i < line_text.size()) {
        size_t continuation_width = 0;
        if (!identifier_codepoint_at(line_text, i, false, continuation_width)) break;
        i += continuation_width;
      }
      std::string_view text = line_text.substr(start, i - start);
      TokenKind kind = TokenKind::Identifier;
      if (text == "def") kind = TokenKind::KwDef;
      else if (text == "class") kind = TokenKind::KwClass;
      else if (text == "return") kind = TokenKind::KwReturn;
      else if (text == "if") kind = TokenKind::KwIf;
      else if (text == "elif") kind = TokenKind::KwElif;
      else if (text == "else") kind = TokenKind::KwElse;
      else if (text == "try") kind = TokenKind::KwTry;
      else if (text == "except") kind = TokenKind::KwExcept;
      else if (text == "case") kind = TokenKind::KwCase;
      else if (text == "finally") kind = TokenKind::KwFinally;
      else if (text == "raise") kind = TokenKind::KwRaise;
      else if (text == "with") kind = TokenKind::KwWith;
      else if (text == "while") kind = TokenKind::KwWhile;
      else if (text == "for") kind = TokenKind::KwFor;
      else if (text == "in") kind = TokenKind::KwIn;
      else if (text == "import") kind = TokenKind::KwImport;
      else if (text == "thru") kind = TokenKind::KwThru;
      else if (text == "from") kind = TokenKind::KwFrom;
      else if (text == "as") kind = TokenKind::KwAs;
      else if (text == "global") kind = TokenKind::KwGlobal;
      else if (text == "nonlocal") kind = TokenKind::KwNonlocal;
      else if (text == "break") kind = TokenKind::KwBreak;
      else if (text == "continue") kind = TokenKind::KwContinue;
      else if (text == "pass") kind = TokenKind::KwPass;
      else if (text == "del") kind = TokenKind::KwDel;
      else if (text == "assert") kind = TokenKind::KwAssert;
      else if (text == "match") kind = TokenKind::KwMatch;
      else if (text == "True") kind = TokenKind::KwTrue;
      else if (text == "False") kind = TokenKind::KwFalse;
      else if (text == "None") kind = TokenKind::KwNone;
      else if (text == "and") kind = TokenKind::KwAnd;
      else if (text == "or") kind = TokenKind::KwOr;
      else if (text == "not") kind = TokenKind::KwNot;
      else if (text == "is") kind = TokenKind::KwIs;
      else if (text == "async") kind = TokenKind::KwAsync;
      else if (text == "await") kind = TokenKind::KwAwait;
      else if (text == "lambda") kind = TokenKind::KwLambda;
      else if (text == "yield") kind = TokenKind::KwYield;
      emit(kind, text, physical_line, col);
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      size_t start = i++;
      bool is_double = false;
      if (ch == '0' && i < line_text.size() &&
          (line_text[i] == 'x' || line_text[i] == 'X' || line_text[i] == 'b' || line_text[i] == 'B' ||
           line_text[i] == 'o' || line_text[i] == 'O')) {
        ++i;
        while (i < line_text.size() &&
               (std::isalnum(static_cast<unsigned char>(line_text[i])) || line_text[i] == '_')) {
          ++i;
        }
        emit(TokenKind::Integer, line_text.substr(start, i - start), physical_line, col);
        continue;
      }
      while (i < line_text.size() &&
             (std::isdigit(static_cast<unsigned char>(line_text[i])) || line_text[i] == '_')) {
        ++i;
      }
      if (i < line_text.size() && line_text[i] == '.') {
        is_double = true;
        ++i;
        while (i < line_text.size() &&
               (std::isdigit(static_cast<unsigned char>(line_text[i])) || line_text[i] == '_')) {
          ++i;
        }
      }
      if (i < line_text.size() && (line_text[i] == 'e' || line_text[i] == 'E')) {
        is_double = true;
        ++i;
        if (i < line_text.size() && (line_text[i] == '+' || line_text[i] == '-')) {
          ++i;
        }
        while (i < line_text.size() &&
               (std::isdigit(static_cast<unsigned char>(line_text[i])) || line_text[i] == '_')) {
          ++i;
        }
      }
      const bool is_complex = i < line_text.size() && (line_text[i] == 'j' || line_text[i] == 'J');
      if (is_complex) {
        ++i;
      }
      emit(is_complex ? TokenKind::Complex : (is_double ? TokenKind::Double : TokenKind::Integer),
           line_text.substr(start, i - start), physical_line, col);
      continue;
    }
    if (ch == '.' && i + 1 < line_text.size() && std::isdigit(static_cast<unsigned char>(line_text[i + 1]))) {
      size_t start = i++;
      while (i < line_text.size() &&
             (std::isdigit(static_cast<unsigned char>(line_text[i])) || line_text[i] == '_')) {
        ++i;
      }
      if (i < line_text.size() && (line_text[i] == 'e' || line_text[i] == 'E')) {
        ++i;
        if (i < line_text.size() && (line_text[i] == '+' || line_text[i] == '-')) {
          ++i;
        }
        while (i < line_text.size() &&
               (std::isdigit(static_cast<unsigned char>(line_text[i])) || line_text[i] == '_')) {
          ++i;
        }
      }
      const bool is_complex = i < line_text.size() && (line_text[i] == 'j' || line_text[i] == 'J');
      if (is_complex) {
        ++i;
      }
      emit(is_complex ? TokenKind::Complex : TokenKind::Double, line_text.substr(start, i - start), physical_line, col);
      continue;
    }
    auto three = i + 2 < line_text.size() ? line_text.substr(i, 3) : std::string_view{};
    if (three == "...") { emit(TokenKind::Ellipsis, three, physical_line, col); i += 3; continue; }
    if (three == "**=") { emit(TokenKind::DoubleStarAssign, three, physical_line, col); i += 3; continue; }
    if (three == "//=") { emit(TokenKind::DoubleSlashAssign, three, physical_line, col); i += 3; continue; }
    if (three == "<<=") { emit(TokenKind::LeftShiftAssign, three, physical_line, col); i += 3; continue; }
    if (three == ">>=") { emit(TokenKind::RightShiftAssign, three, physical_line, col); i += 3; continue; }
    auto two = i + 1 < line_text.size() ? line_text.substr(i, 2) : std::string_view{};
    if (two == "==") { emit(TokenKind::EqualEqual, two, physical_line, col); i += 2; continue; }
    if (two == ":=") { emit(TokenKind::ColonEqual, two, physical_line, col); i += 2; continue; }
    if (two == "!=") { emit(TokenKind::NotEqual, two, physical_line, col); i += 2; continue; }
    if (two == "<=") { emit(TokenKind::LessEqual, two, physical_line, col); i += 2; continue; }
    if (two == ">=") { emit(TokenKind::GreaterEqual, two, physical_line, col); i += 2; continue; }
    if (two == "->") { emit(TokenKind::Arrow, two, physical_line, col); i += 2; continue; }
    if (two == "+=") { emit(TokenKind::PlusAssign, two, physical_line, col); i += 2; continue; }
    if (two == "-=") { emit(TokenKind::MinusAssign, two, physical_line, col); i += 2; continue; }
    if (two == "*=") { emit(TokenKind::StarAssign, two, physical_line, col); i += 2; continue; }
    if (two == "@=") { emit(TokenKind::AtAssign, two, physical_line, col); i += 2; continue; }
    if (two == "/=") { emit(TokenKind::SlashAssign, two, physical_line, col); i += 2; continue; }
    if (two == "%=") { emit(TokenKind::PercentAssign, two, physical_line, col); i += 2; continue; }
    if (two == "&=") { emit(TokenKind::AmpAssign, two, physical_line, col); i += 2; continue; }
    if (two == "|=") { emit(TokenKind::PipeAssign, two, physical_line, col); i += 2; continue; }
    if (two == "^=") { emit(TokenKind::CaretAssign, two, physical_line, col); i += 2; continue; }
    if (two == "**") { emit(TokenKind::DoubleStar, two, physical_line, col); i += 2; continue; }
    if (two == "//") { emit(TokenKind::DoubleSlash, two, physical_line, col); i += 2; continue; }
    if (two == "<<") { emit(TokenKind::LeftShift, two, physical_line, col); i += 2; continue; }
    if (two == ">>") { emit(TokenKind::RightShift, two, physical_line, col); i += 2; continue; }
    switch (ch) {
      case '(': emit(TokenKind::LParen, "(", physical_line, col); break;
      case ')': emit(TokenKind::RParen, ")", physical_line, col); break;
      case '[': emit(TokenKind::LBracket, "[", physical_line, col); break;
      case ']': emit(TokenKind::RBracket, "]", physical_line, col); break;
      case '{': emit(TokenKind::LBrace, "{", physical_line, col); break;
      case '}': emit(TokenKind::RBrace, "}", physical_line, col); break;
      case '.': emit(TokenKind::Dot, ".", physical_line, col); break;
      case ',': emit(TokenKind::Comma, ",", physical_line, col); break;
      case ';': emit(TokenKind::Semicolon, ";", physical_line, col); break;
      case ':': emit(TokenKind::Colon, ":", physical_line, col); break;
      case '@': emit(TokenKind::At, "@", physical_line, col); break;
      case '=': emit(TokenKind::Assign, "=", physical_line, col); break;
      case '+': emit(TokenKind::Plus, "+", physical_line, col); break;
      case '-': emit(TokenKind::Minus, "-", physical_line, col); break;
      case '*': emit(TokenKind::Star, "*", physical_line, col); break;
      case '/': emit(TokenKind::Slash, "/", physical_line, col); break;
      case '%': emit(TokenKind::Percent, "%", physical_line, col); break;
      case '&': emit(TokenKind::Amp, "&", physical_line, col); break;
      case '|': emit(TokenKind::Pipe, "|", physical_line, col); break;
      case '^': emit(TokenKind::Caret, "^", physical_line, col); break;
      case '~': emit(TokenKind::Tilde, "~", physical_line, col); break;
      case '<': emit(TokenKind::Less, "<", physical_line, col); break;
      case '>': emit(TokenKind::Greater, ">", physical_line, col); break;
      default:
        errors_.push_back("line " + std::to_string(physical_line) + ": unexpected character '" + ch + "'");
        break;
    }
    ++i;
  }
}

} // namespace xlang3
