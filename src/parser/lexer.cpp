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

#include "xlang3/source_cursor.h"

#include <cctype>

namespace xlang3 {

namespace {

std::string_view trim_left_ascii(std::string_view text) {
  size_t offset = 0;
  while (offset < text.size() && (text[offset] == ' ' || text[offset] == '\t')) {
    ++offset;
  }
  return text.substr(offset);
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
  return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

bool is_raw_string_prefix(char ch) {
  return ch == 'r' || ch == 'R';
}

bool is_string_prefix_char(char ch) {
  return ch == 'r' || ch == 'R' || ch == 'b' || ch == 'B' || ch == 'f' || ch == 'F' || ch == 'u' || ch == 'U';
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

std::string decode_string_content(std::string_view text, bool raw) {
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
      case '\\':
      case '\'':
      case '"':
        out.push_back(esc);
        break;
      case 'x':
        if (!append_hex_escape(text, i, 2, out)) {
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
      default:
        out.push_back(esc);
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
  bool valid = true;
  for (size_t i = start; i < quote_pos; ++i) {
    const char ch = line[i];
    if (ch == 'r' || ch == 'R') raw = true;
    else if (ch == 'b' || ch == 'B') bytes = true;
    else if (ch == 'f' || ch == 'F') fstring = true;
    else if (ch == 'u' || ch == 'U') {}
    else valid = false;
  }
  if (bytes && fstring) {
    valid = false;
  }
  prefix.start = valid ? start : quote_pos;
  prefix.raw = raw;
  prefix.bytes = bytes;
  prefix.fstring = fstring;
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

void remove_trailing_backslash(std::string& line) {
  while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
    line.pop_back();
  }
  if (!line.empty() && line.back() == '\\') {
    line.pop_back();
  }
}

} // namespace

Lexer::Lexer(std::string_view source) : source_(source) {}

void Lexer::emit(TokenKind kind, std::string_view text, uint32_t line, uint32_t column, bool is_triple_string) {
  tokens_.push_back(Token{kind, text, line, column, is_triple_string});
}

void Lexer::emit_owned(TokenKind kind, std::string text, uint32_t line, uint32_t column, bool is_triple_string) {
  auto owned = std::make_unique<std::string>(std::move(text));
  const std::string_view view(*owned);
  owned_text_.push_back(std::move(owned));
  tokens_.push_back(Token{kind, view, line, column, is_triple_string});
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
    const auto first = line.find_first_not_of(" \t");
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

    const size_t single_triple = line.find("'''", indent);
    const size_t double_triple = line.find("\"\"\"", indent);
    size_t triple_pos = std::string_view::npos;
    std::string_view opener;
    if (single_triple != std::string_view::npos &&
        (double_triple == std::string_view::npos || single_triple < double_triple)) {
      triple_pos = single_triple;
      opener = "'''";
    } else if (double_triple != std::string_view::npos) {
      triple_pos = double_triple;
      opener = "\"\"\"";
    }

    if (!opener.empty()) {
      const uint32_t start_line_no = line_no;
      const auto prefix = detect_string_prefix_for_quote(line, triple_pos);
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
      value = decode_string_content(value, prefix.raw);
      const TokenKind kind = prefix.bytes ? TokenKind::Bytes : (prefix.fstring ? TokenKind::FString : TokenKind::String);
      emit_owned(kind, std::move(value), start_line_no, static_cast<uint32_t>(prefix_start + 1), true);
      if (suffix.find_first_not_of(" \t") != std::string::npos) {
        tokenize_line(std::string(indent, ' ') + suffix, line_no, indent);
      }
      emit(TokenKind::Newline, "", line_no, static_cast<uint32_t>(line.size() + 1));
      continue;
    }

    std::string logical_line(line);
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
      logical_line.push_back(' ');
      logical_line += std::string(trim_left_ascii(next_line.text));
      should_join = update_line_join_state(next_line.text, bracket_depth, explicit_continue);
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
  while (i < line_text.size()) {
    const char ch = line_text[i];
    const uint32_t col = static_cast<uint32_t>(i + 1);
    if (ch == ' ' || ch == '\t') {
      ++i;
      continue;
    }
    if (ch == '#') {
      return;
    }
    const auto prefix = detect_string_start(line_text, i);
    if (prefix.valid) {
      const char quote = line_text[prefix.quote];
      i = prefix.quote + 1;
      std::string value;
      bool closed = false;
      while (i < line_text.size()) {
        if (line_text[i] == quote) {
          closed = true;
          break;
        }
        if (!prefix.raw && line_text[i] == '\\' && i + 1 < line_text.size()) {
          value.push_back(line_text[i++]);
          value.push_back(line_text[i++]);
          continue;
        }
        if (prefix.raw && line_text[i] == '\\' && i + 1 < line_text.size()) {
          value.push_back(line_text[i++]);
          value.push_back(line_text[i++]);
          continue;
        }
        value.push_back(line_text[i++]);
      }
      if (!closed) {
        errors_.push_back("line " + std::to_string(line_no) + ": unterminated string");
        return;
      }
      ++i;
      value = decode_string_content(value, prefix.raw);
      const TokenKind kind = prefix.bytes ? TokenKind::Bytes : (prefix.fstring ? TokenKind::FString : TokenKind::String);
      emit_owned(kind, std::move(value), line_no, col);
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) || ch == '_') {
      size_t start = i++;
      while (i < line_text.size() &&
             (std::isalnum(static_cast<unsigned char>(line_text[i])) || line_text[i] == '_')) {
        ++i;
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
      emit(kind, text, line_no, col);
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      size_t start = i++;
      bool is_double = false;
      while (i < line_text.size() && std::isdigit(static_cast<unsigned char>(line_text[i]))) {
        ++i;
      }
      if (i < line_text.size() && line_text[i] == '.') {
        is_double = true;
        ++i;
        while (i < line_text.size() && std::isdigit(static_cast<unsigned char>(line_text[i]))) {
          ++i;
        }
      }
      emit(is_double ? TokenKind::Double : TokenKind::Integer, line_text.substr(start, i - start), line_no, col);
      continue;
    }
    auto three = i + 2 < line_text.size() ? line_text.substr(i, 3) : std::string_view{};
    if (three == "**=") { emit(TokenKind::DoubleStarAssign, three, line_no, col); i += 3; continue; }
    if (three == "//=") { emit(TokenKind::DoubleSlashAssign, three, line_no, col); i += 3; continue; }
    if (three == "<<=") { emit(TokenKind::LeftShiftAssign, three, line_no, col); i += 3; continue; }
    if (three == ">>=") { emit(TokenKind::RightShiftAssign, three, line_no, col); i += 3; continue; }
    auto two = i + 1 < line_text.size() ? line_text.substr(i, 2) : std::string_view{};
    if (two == "==") { emit(TokenKind::EqualEqual, two, line_no, col); i += 2; continue; }
    if (two == ":=") { emit(TokenKind::ColonEqual, two, line_no, col); i += 2; continue; }
    if (two == "!=") { emit(TokenKind::NotEqual, two, line_no, col); i += 2; continue; }
    if (two == "<=") { emit(TokenKind::LessEqual, two, line_no, col); i += 2; continue; }
    if (two == ">=") { emit(TokenKind::GreaterEqual, two, line_no, col); i += 2; continue; }
    if (two == "->") { emit(TokenKind::Arrow, two, line_no, col); i += 2; continue; }
    if (two == "+=") { emit(TokenKind::PlusAssign, two, line_no, col); i += 2; continue; }
    if (two == "-=") { emit(TokenKind::MinusAssign, two, line_no, col); i += 2; continue; }
    if (two == "*=") { emit(TokenKind::StarAssign, two, line_no, col); i += 2; continue; }
    if (two == "/=") { emit(TokenKind::SlashAssign, two, line_no, col); i += 2; continue; }
    if (two == "%=") { emit(TokenKind::PercentAssign, two, line_no, col); i += 2; continue; }
    if (two == "&=") { emit(TokenKind::AmpAssign, two, line_no, col); i += 2; continue; }
    if (two == "|=") { emit(TokenKind::PipeAssign, two, line_no, col); i += 2; continue; }
    if (two == "^=") { emit(TokenKind::CaretAssign, two, line_no, col); i += 2; continue; }
    if (two == "**") { emit(TokenKind::DoubleStar, two, line_no, col); i += 2; continue; }
    if (two == "//") { emit(TokenKind::DoubleSlash, two, line_no, col); i += 2; continue; }
    if (two == "<<") { emit(TokenKind::LeftShift, two, line_no, col); i += 2; continue; }
    if (two == ">>") { emit(TokenKind::RightShift, two, line_no, col); i += 2; continue; }
    switch (ch) {
      case '(': emit(TokenKind::LParen, "(", line_no, col); break;
      case ')': emit(TokenKind::RParen, ")", line_no, col); break;
      case '[': emit(TokenKind::LBracket, "[", line_no, col); break;
      case ']': emit(TokenKind::RBracket, "]", line_no, col); break;
      case '{': emit(TokenKind::LBrace, "{", line_no, col); break;
      case '}': emit(TokenKind::RBrace, "}", line_no, col); break;
      case '.': emit(TokenKind::Dot, ".", line_no, col); break;
      case ',': emit(TokenKind::Comma, ",", line_no, col); break;
      case ';': emit(TokenKind::Semicolon, ";", line_no, col); break;
      case ':': emit(TokenKind::Colon, ":", line_no, col); break;
      case '@': emit(TokenKind::At, "@", line_no, col); break;
      case '=': emit(TokenKind::Assign, "=", line_no, col); break;
      case '+': emit(TokenKind::Plus, "+", line_no, col); break;
      case '-': emit(TokenKind::Minus, "-", line_no, col); break;
      case '*': emit(TokenKind::Star, "*", line_no, col); break;
      case '/': emit(TokenKind::Slash, "/", line_no, col); break;
      case '%': emit(TokenKind::Percent, "%", line_no, col); break;
      case '&': emit(TokenKind::Amp, "&", line_no, col); break;
      case '|': emit(TokenKind::Pipe, "|", line_no, col); break;
      case '^': emit(TokenKind::Caret, "^", line_no, col); break;
      case '~': emit(TokenKind::Tilde, "~", line_no, col); break;
      case '<': emit(TokenKind::Less, "<", line_no, col); break;
      case '>': emit(TokenKind::Greater, ">", line_no, col); break;
      default:
        errors_.push_back("line " + std::to_string(line_no) + ": unexpected character '" + ch + "'");
        break;
    }
    ++i;
  }
}

} // namespace xlang3
