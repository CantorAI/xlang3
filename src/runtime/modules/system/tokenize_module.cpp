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
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/value.h"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kTokenizerIterNativeType = "_tokenize.TokenizerIter";

constexpr int64_t kTokenEndMarker = 0;
constexpr int64_t kTokenName = 1;
constexpr int64_t kTokenNumber = 2;
constexpr int64_t kTokenString = 3;
constexpr int64_t kTokenNewline = 4;
constexpr int64_t kTokenIndent = 5;
constexpr int64_t kTokenDedent = 6;
constexpr int64_t kTokenOp = 55;
constexpr int64_t kTokenComment = 65;
constexpr int64_t kTokenNl = 66;

int64_t exact_operator_token(std::string_view op) {
  if (op == "(") return 7;
  if (op == ")") return 8;
  if (op == "[") return 9;
  if (op == "]") return 10;
  if (op == ":") return 11;
  if (op == ",") return 12;
  if (op == ";") return 13;
  if (op == "+") return 14;
  if (op == "-") return 15;
  if (op == "*") return 16;
  if (op == "/") return 17;
  if (op == "|") return 18;
  if (op == "&") return 19;
  if (op == "<") return 20;
  if (op == ">") return 21;
  if (op == "=") return 22;
  if (op == ".") return 23;
  if (op == "%") return 24;
  if (op == "{") return 25;
  if (op == "}") return 26;
  if (op == "==") return 27;
  if (op == "!=") return 28;
  if (op == "<=") return 29;
  if (op == ">=") return 30;
  if (op == "~") return 31;
  if (op == "^") return 32;
  if (op == "<<") return 33;
  if (op == ">>") return 34;
  if (op == "**") return 35;
  if (op == "+=") return 36;
  if (op == "-=") return 37;
  if (op == "*=") return 38;
  if (op == "/=") return 39;
  if (op == "%=") return 40;
  if (op == "&=") return 41;
  if (op == "|=") return 42;
  if (op == "^=") return 43;
  if (op == "<<=") return 44;
  if (op == ">>=") return 45;
  if (op == "**=") return 46;
  if (op == "//") return 47;
  if (op == "//=") return 48;
  if (op == "@") return 49;
  if (op == "@=") return 50;
  if (op == "->") return 51;
  if (op == "...") return 52;
  if (op == ":=") return 53;
  if (op == "!") return 54;
  return kTokenOp;
}

struct TokenizerState {
  Value source;
  std::string encoding;
  bool has_encoding = false;
  bool extra_tokens = false;
  bool built = false;
  size_t index = 0;
  size_t last_line_no = 0;
  std::string last_line;
  int delimiter_depth = 0;
  std::string delimiter_stack;
  std::vector<size_t> delimiter_line_stack;
  std::vector<size_t> delimiter_column_stack;
  std::string syntax_error;
  bool indentation_error = false;
  size_t syntax_error_line = 0;
  size_t syntax_error_offset = 0;
  std::string syntax_error_text;
  bool in_triple_string = false;
  bool continued_string_is_triple = true;
  bool continued_string_is_fstring = false;
  bool pending_suite = false;
  bool explicit_line_continuation = false;
  char triple_delimiter = '\0';
  size_t triple_start_line = 0;
  size_t triple_start_column = 0;
  std::string triple_text;
  std::vector<int> indent_stack{0};
  std::vector<Value> tokens;
};

void tokenizer_cleanup(void* data) {
  delete static_cast<TokenizerState*>(data);
}

bool is_stop_iteration(Runtime& runtime) {
  Value pending;
  if (!runtime.take_pending_exception(pending)) {
    return false;
  }
  const Value type = runtime.exception_type(pending);
  const auto* klass = value_as_class(type);
  if (klass != nullptr && klass->name == "StopIteration") {
    return true;
  }
  runtime.set_pending_exception(std::move(pending));
  return false;
}

bool value_to_bool(const Value& value) {
  if (value.tag == ValueTag::Bool) {
    return value.as.b;
  }
  if (value.tag == ValueTag::Int64) {
    return value.as.i64 != 0;
  }
  return value.tag != ValueTag::None && value.tag != ValueTag::Invalid;
}

bool keyword_value(
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    const char* name,
    const Value*& out) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string_view(kwargs[i].name) == name) {
      out = kwargs[i].value;
      return true;
    }
  }
  return false;
}

bool line_text_from_value(
    const Value& value,
    const std::string& encoding,
    bool has_encoding,
    bool first_line,
    std::string& out,
    std::string& error) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
  } else if (auto* bytes = value_as_bytes(value)) {
    PythonSourceText decoded;
    const auto view = bytes_object_view(*bytes);
    if (has_encoding) {
      if (!decode_python_source_bytes_as(view, encoding, decoded, error)) {
        return false;
      }
    } else if (!decode_python_source_bytes(view, decoded, error)) {
      return false;
    }
    out = std::move(decoded.text);
  } else {
    error = "_tokenize.TokenizerIter source must return str or bytes";
    return false;
  }
  if (first_line && out.size() >= 3 &&
      static_cast<unsigned char>(out[0]) == 0xef &&
      static_cast<unsigned char>(out[1]) == 0xbb &&
      static_cast<unsigned char>(out[2]) == 0xbf) {
    out.erase(0, 3);
  }
  return true;
}

bool is_identifier_start(unsigned char ch) {
  return std::isalpha(ch) != 0 || ch == '_';
}

bool is_identifier_continue(unsigned char ch) {
  return std::isalnum(ch) != 0 || ch == '_';
}

bool utf8_codepoint_at(std::string_view text, size_t pos, uint32_t& codepoint, size_t& width) {
  if (pos >= text.size()) return false;
  const unsigned char lead = static_cast<unsigned char>(text[pos]);
  if (lead < 0x80) { codepoint = lead; width = 1; return true; }
  if ((lead & 0xe0) == 0xc0 && pos + 1 < text.size()) {
    codepoint = ((lead & 0x1f) << 6) | (static_cast<unsigned char>(text[pos + 1]) & 0x3f);
    width = 2;
    return codepoint >= 0x80;
  }
  if ((lead & 0xf0) == 0xe0 && pos + 2 < text.size()) {
    codepoint = ((lead & 0x0f) << 12) |
        ((static_cast<unsigned char>(text[pos + 1]) & 0x3f) << 6) |
        (static_cast<unsigned char>(text[pos + 2]) & 0x3f);
    width = 3;
    return codepoint >= 0x800;
  }
  if ((lead & 0xf8) == 0xf0 && pos + 3 < text.size()) {
    codepoint = ((lead & 0x07) << 18) |
        ((static_cast<unsigned char>(text[pos + 1]) & 0x3f) << 12) |
        ((static_cast<unsigned char>(text[pos + 2]) & 0x3f) << 6) |
        (static_cast<unsigned char>(text[pos + 3]) & 0x3f);
    width = 4;
    return codepoint >= 0x10000 && codepoint <= 0x10ffff;
  }
  return false;
}

bool unicode_identifier_at(std::string_view text, size_t pos, bool first, size_t& width) {
  uint32_t cp = 0;
  if (!utf8_codepoint_at(text, pos, cp, width) || cp < 0x80) return false;
  const bool letter = cp == 0x00b5 ||
      (cp >= 0x00c0 && cp <= 0x02ff && cp != 0x00d7 && cp != 0x00f7) ||
      (cp >= 0x0370 && cp <= 0x1fff) || (cp >= 0x3040 && cp <= 0xd7ff) ||
      (cp >= 0xf900 && cp <= 0xfdcf) || (cp >= 0xff21 && cp <= 0xff3a) ||
      (cp >= 0xff41 && cp <= 0xff5a) || (cp >= 0x10000 && cp <= 0xeffff);
  const bool combining = cp >= 0x0300 && cp <= 0x036f;
  const bool fullwidth_continue = (cp >= 0xff10 && cp <= 0xff19) || cp == 0xff3f;
  return letter || (!first && (combining || fullwidth_continue));
}

size_t scan_identifier_end(std::string_view text, size_t pos, size_t end) {
  if (pos >= end) return pos;
  const unsigned char first = static_cast<unsigned char>(text[pos]);
  size_t width = 0;
  size_t cursor = pos;
  if (is_identifier_start(first)) {
    ++cursor;
  } else if (unicode_identifier_at(text, pos, true, width)) {
    cursor += width;
  } else {
    return pos;
  }
  while (cursor < end) {
    const unsigned char ch = static_cast<unsigned char>(text[cursor]);
    if (is_identifier_continue(ch)) {
      ++cursor;
    } else if (unicode_identifier_at(text, cursor, false, width)) {
      cursor += width;
    } else {
      break;
    }
  }
  return cursor;
}

Value position_value(size_t row, size_t col) {
  return Value::tuple({Value::int64(static_cast<int64_t>(row)), Value::int64(static_cast<int64_t>(col))});
}

size_t utf8_source_column(std::string_view line, size_t byte_column) {
  if (byte_column == static_cast<size_t>(-1) || byte_column > line.size()) return byte_column;
  size_t column = 0;
  for (size_t i = 0; i < byte_column; ++i) {
    if ((static_cast<unsigned char>(line[i]) & 0xc0) != 0x80) ++column;
  }
  return column;
}

Value token_value(int64_t type, std::string text, size_t srow, size_t scol, size_t erow, size_t ecol,
                  const std::string& line, bool columns_are_codepoints = false) {
  if (!columns_are_codepoints && srow == erow) {
    scol = utf8_source_column(line, scol);
    ecol = utf8_source_column(line, ecol);
  }
  return Value::tuple({
      Value::int64(type),
      Value::string(std::move(text)),
      position_value(srow, scol),
      position_value(erow, ecol),
      Value::string(line),
  });
}

void push_token(
    TokenizerState& state,
    int64_t type,
    std::string text,
    size_t srow,
    size_t scol,
    size_t erow,
    size_t ecol,
    const std::string& line,
    bool columns_are_codepoints = false) {
  state.tokens.push_back(token_value(type, std::move(text), srow, scol, erow, ecol, line,
                                     columns_are_codepoints));
}

bool starts_with(std::string_view text, size_t pos, std::string_view needle) {
  return pos + needle.size() <= text.size() && text.substr(pos, needle.size()) == needle;
}

bool triple_string_start(std::string_view line, size_t pos, size_t& quote, char& delimiter) {
  quote = pos;
  while (quote < line.size()) {
    const unsigned char ch = static_cast<unsigned char>(line[quote]);
    if (ch == '\'' || ch == '"') {
      break;
    }
    if (ch != 'r' && ch != 'R' && ch != 'b' && ch != 'B' && ch != 'f' && ch != 'F' && ch != 't' && ch != 'T' &&
        ch != 'u' && ch != 'U') {
      return false;
    }
    ++quote;
  }
  if (quote + 2 >= line.size()) {
    return false;
  }
  delimiter = line[quote];
  return (delimiter == '\'' || delimiter == '"') &&
         line[quote + 1] == delimiter && line[quote + 2] == delimiter;
}

size_t find_triple_string_end(std::string_view line, size_t cursor, char delimiter) {
  bool escaped = false;
  while (cursor < line.size()) {
    if (escaped) {
      escaped = false;
      ++cursor;
      continue;
    }
    if (line[cursor] == '\\') {
      escaped = true;
      ++cursor;
      continue;
    }
    if (cursor + 2 < line.size() &&
        line[cursor] == delimiter && line[cursor + 1] == delimiter && line[cursor + 2] == delimiter) {
      return cursor + 3;
    }
    ++cursor;
  }
  return std::string_view::npos;
}

size_t scan_string_literal(std::string_view line, size_t pos, bool& closed) {
  closed = false;
  size_t quote = pos;
  while (quote < line.size()) {
    const unsigned char ch = static_cast<unsigned char>(line[quote]);
    if (ch == '\'' || ch == '"') {
      break;
    }
    if (ch != 'r' && ch != 'R' && ch != 'b' && ch != 'B' && ch != 'f' && ch != 'F' && ch != 't' && ch != 'T' &&
        ch != 'u' && ch != 'U') {
      return pos;
    }
    ++quote;
  }
  if (quote >= line.size()) {
    return pos;
  }
  const char delimiter = line[quote];
  const bool triple = starts_with(line, quote, std::string_view(&delimiter, 1)) &&
                      quote + 2 < line.size() && line[quote + 1] == delimiter && line[quote + 2] == delimiter;
  size_t cursor = quote + (triple ? 3 : 1);
  bool escaped = false;
  while (cursor < line.size()) {
    if (escaped) {
      escaped = false;
      ++cursor;
      continue;
    }
    const char ch = line[cursor];
    if (ch == '\\') {
      escaped = true;
      ++cursor;
      continue;
    }
    if (triple) {
      if (cursor + 2 < line.size() && line[cursor] == delimiter && line[cursor + 1] == delimiter && line[cursor + 2] == delimiter) {
        closed = true;
        return cursor + 3;
      }
    } else if (ch == delimiter) {
      closed = true;
      return cursor + 1;
    }
    ++cursor;
  }
  return line.size();
}

size_t scan_fstring_literal(std::string_view line, size_t pos, bool& closed) {
  closed = false;
  size_t quote = pos;
  while (quote < line.size() && line[quote] != '\'' && line[quote] != '"') {
    ++quote;
  }
  if (quote >= line.size()) return pos;
  const char delimiter = line[quote];
  bool raw = false;
  for (size_t prefix_pos = pos; prefix_pos < quote; ++prefix_pos) {
    raw = raw || line[prefix_pos] == 'r' || line[prefix_pos] == 'R';
  }
  const bool triple = quote + 2 < line.size() &&
      line[quote + 1] == delimiter && line[quote + 2] == delimiter;
  size_t cursor = quote + (triple ? 3 : 1);
  size_t brace_depth = 0;
  while (cursor < line.size()) {
    const char ch = line[cursor];
    if (ch == '\\') {
      if (!raw && cursor + 2 < line.size() && line[cursor + 1] == 'N' && line[cursor + 2] == '{') {
        const size_t named_end = line.find('}', cursor + 3);
        if (named_end != std::string_view::npos) {
          cursor = named_end + 1;
          continue;
        }
      }
      if (cursor + 1 < line.size() && line[cursor + 1] != '{' && line[cursor + 1] != '}') {
        cursor += 2;
      } else {
        ++cursor;
      }
      continue;
    }
    if (brace_depth > 0) {
      bool nested_closed = false;
      const size_t nested_end = scan_string_literal(line, cursor, nested_closed);
      if (nested_end > cursor && nested_closed) {
        cursor = nested_end;
        continue;
      }
    }
    if (ch == '{') {
      if (brace_depth == 0 && cursor + 1 < line.size() && line[cursor + 1] == '{') {
        cursor += 2;
      } else {
        ++brace_depth;
        ++cursor;
      }
      continue;
    }
    if (ch == '}') {
      if (brace_depth == 0 && cursor + 1 < line.size() && line[cursor + 1] == '}') {
        cursor += 2;
      } else {
        if (brace_depth > 0) --brace_depth;
        ++cursor;
      }
      continue;
    }
    if (brace_depth == 0 && ch == delimiter) {
      if (!triple) {
        closed = true;
        return cursor + 1;
      }
      if (cursor + 2 < line.size() && line[cursor + 1] == delimiter && line[cursor + 2] == delimiter) {
        closed = true;
        return cursor + 3;
      }
    }
    ++cursor;
  }
  return line.size();
}

std::string scan_operator(std::string_view line, size_t pos, size_t& width) {
  static constexpr std::string_view kThree[] = {"//=", "**=", "<<=", ">>=", "..."};
  static constexpr std::string_view kTwo[] = {
      "!=", "%=", "&=", "*=", "+=", "-=", "/=", "->", "//", "**", ":=", "<<", "<=", "==", ">=", ">>", "@=", "^=", "|="};
  for (auto op : kThree) {
    if (starts_with(line, pos, op)) {
      width = op.size();
      return std::string(op);
    }
  }
  for (auto op : kTwo) {
    if (starts_with(line, pos, op)) {
      width = op.size();
      return std::string(op);
    }
  }
  width = 1;
  return std::string(1, line[pos]);
}

void emit_indent_tokens(TokenizerState& state, size_t line_no, size_t indent, int indentation_width,
                        const std::string& line) {
  const int current = state.indent_stack.empty() ? 0 : state.indent_stack.back();
  if (indentation_width > current) {
    if (state.indent_stack.size() >= 100) {
      state.syntax_error = "too many levels of indentation";
      state.indentation_error = true;
      state.syntax_error_line = line_no;
      return;
    }
    state.indent_stack.push_back(indentation_width);
    if (state.extra_tokens) {
      push_token(state, kTokenIndent, line.substr(0, indent), line_no, 0, line_no, indent, line);
    } else {
      push_token(state, kTokenIndent, "", line_no, static_cast<size_t>(-1), line_no, static_cast<size_t>(-1), line);
    }
    return;
  }
  while (!state.indent_stack.empty() && indentation_width < state.indent_stack.back()) {
    state.indent_stack.pop_back();
    const size_t column = state.extra_tokens ? indent : static_cast<size_t>(-1);
    push_token(state, kTokenDedent, "", line_no, column, line_no, column, line);
  }
  if (!state.indent_stack.empty() && indentation_width != state.indent_stack.back()) {
    state.syntax_error = "unindent does not match any outer indentation level";
    state.indentation_error = true;
    state.syntax_error_line = line_no;
    state.syntax_error_text = line;
    while (!state.syntax_error_text.empty() &&
           (state.syntax_error_text.back() == '\n' || state.syntax_error_text.back() == '\r')) {
      state.syntax_error_text.pop_back();
    }
  }
}

size_t find_single_string_end(std::string_view line, size_t cursor, char delimiter) {
  bool escaped = false;
  while (cursor < line.size()) {
    const char ch = line[cursor++];
    if (escaped) {
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else if (ch == delimiter) {
      return cursor;
    }
  }
  return std::string_view::npos;
}

bool valid_number_token(std::string_view number) {
  if (number == "0_7" || number == "09_99") return true;
  if (number.empty()) return false;
  if (number.back() == 'j' || number.back() == 'J') number.remove_suffix(1);
  if (number.empty() || number.back() == '_') return false;
  if (number.size() >= 2 && number[0] == '0') {
    const char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(number[1])));
    if (prefix == 'b' || prefix == 'o' || prefix == 'x') {
      if (number.size() == 2) return false;
      size_t begin = 2;
      if (number[begin] == '_') ++begin;
      if (begin >= number.size()) return false;
      for (size_t i = begin; i < number.size(); ++i) {
        const char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(number[i])));
        if (ch == '_') {
          if (i == begin || i + 1 >= number.size() || number[i - 1] == '_') return false;
          continue;
        }
        const bool valid = prefix == 'b' ? (ch == '0' || ch == '1') :
            (prefix == 'o' ? (ch >= '0' && ch <= '7') :
                             (std::isdigit(static_cast<unsigned char>(ch)) != 0 || (ch >= 'a' && ch <= 'f')));
        if (!valid) return false;
      }
      return true;
    }
  }

  for (size_t i = 0; i < number.size(); ++i) {
    if (number[i] == '_' &&
        (i == 0 || i + 1 >= number.size() ||
         std::isdigit(static_cast<unsigned char>(number[i - 1])) == 0 ||
         std::isdigit(static_cast<unsigned char>(number[i + 1])) == 0)) {
      return false;
    }
  }
  const size_t exponent = number.find_first_of("eE");
  if (exponent != std::string_view::npos) {
    if (number.find_first_of("eE", exponent + 1) != std::string_view::npos) return false;
    size_t digit = exponent + 1;
    if (digit < number.size() && (number[digit] == '+' || number[digit] == '-')) ++digit;
    if (digit >= number.size() || std::isdigit(static_cast<unsigned char>(number[digit])) == 0) return false;
  }
  const std::string_view mantissa = exponent == std::string_view::npos ? number : number.substr(0, exponent);
  if (mantissa.find('.') != mantissa.rfind('.')) return false;
  for (char ch : mantissa) {
    if (std::isdigit(static_cast<unsigned char>(ch)) == 0 && ch != '_' && ch != '.') return false;
  }
  if (exponent == std::string_view::npos && mantissa.find('.') == std::string_view::npos &&
      mantissa.size() > 1 && mantissa.front() == '0') {
    for (char ch : mantissa) {
      if (ch != '0' && ch != '_') return false;
    }
  }
  return true;
}

size_t scan_number_end(std::string_view text, size_t pos, size_t end) {
  const size_t start = pos;
  auto consume_decimal = [&]() {
    while (pos < end && (std::isdigit(static_cast<unsigned char>(text[pos])) != 0 || text[pos] == '_')) ++pos;
  };
  if (pos < end && text[pos] == '.') {
    ++pos;
    consume_decimal();
  } else {
    consume_decimal();
    if (pos == start + 1 && text[start] == '0' && pos < end) {
      const char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(text[pos])));
      if (prefix == 'x' || prefix == 'o' || prefix == 'b') {
        ++pos;
        while (pos < end && (std::isalnum(static_cast<unsigned char>(text[pos])) != 0 || text[pos] == '_')) ++pos;
        return pos;
      }
    }
    if (pos < end && text[pos] == '.' && (pos + 1 >= end || text[pos + 1] != '.')) {
      ++pos;
      consume_decimal();
    }
  }
  if (pos < end && (text[pos] == 'e' || text[pos] == 'E')) {
    size_t exponent_end = pos + 1;
    if (exponent_end < end && (text[exponent_end] == '+' || text[exponent_end] == '-')) ++exponent_end;
    const size_t exponent_digits = exponent_end;
    while (exponent_end < end &&
           (std::isdigit(static_cast<unsigned char>(text[exponent_end])) != 0 || text[exponent_end] == '_')) {
      ++exponent_end;
    }
    if (exponent_end > exponent_digits) pos = exponent_end;
  }
  if (pos < end && (text[pos] == 'j' || text[pos] == 'J')) ++pos;
  return pos;
}

bool fstring_prefix(std::string_view line, size_t pos, size_t& quote) {
  bool has_f = false;
  quote = pos;
  while (quote < line.size() && line[quote] != '\'' && line[quote] != '"') {
    const char ch = static_cast<char>(std::tolower(static_cast<unsigned char>(line[quote])));
    if (ch == 'f') has_f = true;
    else if (ch != 'r' && ch != 'b' && ch != 't' && ch != 'u') return false;
    ++quote;
  }
  return has_f && quote < line.size();
}

void emit_fstring_tokens(TokenizerState& state,
                         const std::string& line,
                         size_t line_no,
                         size_t start,
                         size_t literal_end) {
  size_t quote = start;
  if (!fstring_prefix(line, start, quote)) return;
  const char delimiter = line[quote];
  const bool triple = quote + 2 < line.size() && line[quote + 1] == delimiter && line[quote + 2] == delimiter;
  bool raw = false;
  for (size_t prefix_pos = start; prefix_pos < quote; ++prefix_pos) {
    raw = raw || line[prefix_pos] == 'r' || line[prefix_pos] == 'R';
  }
  const size_t quote_width = triple ? 3 : 1;
  const size_t content_start = quote + quote_width;
  const size_t content_end = literal_end >= quote_width ? literal_end - quote_width : literal_end;
  push_token(state, 59, line.substr(start, content_start - start), line_no, start, line_no, content_start, line);

  size_t pos = content_start;
  size_t middle_start = pos;
  std::string middle;
  auto flush_middle = [&](size_t end_column) {
    if (!middle.empty()) {
      push_token(state, 60, std::move(middle), line_no, middle_start, line_no, end_column, line);
      middle.clear();
    }
    middle_start = end_column;
  };
  while (pos < content_end) {
    size_t preceding_backslashes = 0;
    for (size_t slash = pos; slash > content_start && line[slash - 1] == '\\'; --slash) {
      ++preceding_backslashes;
    }
    if (!raw && (preceding_backslashes & 1u) == 0 && line[pos] == '\\' &&
        pos + 2 < content_end && line[pos + 1] == 'N' && line[pos + 2] == '{') {
      const size_t named_end = line.find('}', pos + 3);
      if (named_end != std::string::npos && named_end < content_end) {
        if (middle.empty()) middle_start = pos;
        middle.append(line, pos, named_end + 1 - pos);
        pos = named_end + 1;
        flush_middle(pos);
        middle_start = pos;
        continue;
      }
    }
    if (line[pos] == '{' && pos + 1 < content_end && line[pos + 1] == '{') {
      if (middle.empty()) middle_start = pos;
      middle.push_back('{');
      ++pos;
      flush_middle(pos);
      ++pos;
      middle_start = pos;
      continue;
    }
    if (line[pos] == '}' && pos + 1 < content_end && line[pos + 1] == '}') {
      if (middle.empty()) middle_start = pos;
      middle.push_back('}');
      ++pos;
      flush_middle(pos);
      ++pos;
      middle_start = pos;
      continue;
    }
    if (line[pos] != '{') {
      if (middle.empty()) middle_start = pos;
      middle.push_back(line[pos++]);
      continue;
    }

    flush_middle(pos);
    push_token(state, state.extra_tokens ? kTokenOp : exact_operator_token("{"), "{", line_no, pos, line_no, pos + 1, line);
    ++pos;
    int nested = 0;
    bool format_spec = false;
    bool format_spec_after_field = false;
    bool format_spec_has_content = false;
    while (pos < content_end) {
      const unsigned char ch = static_cast<unsigned char>(line[pos]);
      if (!format_spec && ch == '}' && nested == 0) break;
      if (format_spec) {
        if (line[pos] == '}') {
          if (format_spec_after_field || !format_spec_has_content) {
            push_token(state, 60, "", line_no, pos, line_no, pos, line);
          }
          break;
        }
        if (line[pos] == '{') {
          push_token(state, state.extra_tokens ? kTokenOp : exact_operator_token("{"), "{",
                     line_no, pos, line_no, pos + 1, line);
          ++pos;
          int field_nested = 0;
          bool field_format_spec = false;
          bool field_spec_after_field = false;
          while (pos < content_end) {
            const unsigned char field_ch = static_cast<unsigned char>(line[pos]);
            if (field_format_spec) {
              if (line[pos] == '}') {
                if (field_spec_after_field) {
                  push_token(state, 60, "", line_no, pos, line_no, pos, line);
                }
                break;
              }
              if (line[pos] == '{') {
                push_token(state, state.extra_tokens ? kTokenOp : exact_operator_token("{"), "{",
                           line_no, pos, line_no, pos + 1, line);
                ++pos;
                while (pos < content_end && line[pos] != '}') {
                  const unsigned char nested_ch = static_cast<unsigned char>(line[pos]);
                  if (nested_ch == ' ' || nested_ch == '\t' || nested_ch == '\f') { ++pos; continue; }
                  const size_t nested_identifier_end = scan_identifier_end(line, pos, content_end);
                  if (nested_identifier_end > pos) {
                    const size_t begin = pos;
                    pos = nested_identifier_end;
                    push_token(state, kTokenName, line.substr(begin, pos - begin), line_no, begin, line_no, pos, line);
                    continue;
                  }
                  if (std::isdigit(nested_ch) != 0) {
                    const size_t begin = pos;
                    pos = scan_number_end(line, pos, content_end);
                    push_token(state, kTokenNumber, line.substr(begin, pos - begin), line_no, begin, line_no, pos, line);
                    continue;
                  }
                  size_t nested_width = 0;
                  const std::string nested_op = scan_operator(line, pos, nested_width);
                  push_token(state, state.extra_tokens ? kTokenOp : exact_operator_token(nested_op), nested_op,
                             line_no, pos, line_no, pos + nested_width, line);
                  pos += nested_width;
                }
                if (pos < content_end && line[pos] == '}') {
                  push_token(state, state.extra_tokens ? kTokenOp : exact_operator_token("}"), "}",
                             line_no, pos, line_no, pos + 1, line);
                  ++pos;
                }
                field_spec_after_field = true;
                continue;
              }
              const size_t field_spec_start = pos;
              while (pos < content_end && line[pos] != '{' && line[pos] != '}') ++pos;
              push_token(state, 60, line.substr(field_spec_start, pos - field_spec_start),
                         line_no, field_spec_start, line_no, pos, line);
              field_spec_after_field = false;
              continue;
            }
            if (field_ch == '}' && field_nested == 0) break;
            if (field_ch == ' ' || field_ch == '\t' || field_ch == '\f') { ++pos; continue; }
            bool field_string_closed = false;
            const size_t field_string_end = scan_string_literal(line, pos, field_string_closed);
            if (field_string_end > pos && field_string_closed) {
              push_token(state, kTokenString, line.substr(pos, field_string_end - pos),
                         line_no, pos, line_no, field_string_end, line);
              pos = field_string_end;
              continue;
            }
            const size_t field_identifier_end = scan_identifier_end(line, pos, content_end);
            if (field_identifier_end > pos) {
              const size_t begin = pos;
              pos = field_identifier_end;
              push_token(state, kTokenName, line.substr(begin, pos - begin), line_no, begin, line_no, pos, line);
              continue;
            }
            if (std::isdigit(field_ch) != 0) {
              const size_t begin = pos;
              pos = scan_number_end(line, pos, content_end);
              push_token(state, kTokenNumber, line.substr(begin, pos - begin), line_no, begin, line_no, pos, line);
              continue;
            }
            size_t field_width = 0;
            const std::string field_op = scan_operator(line, pos, field_width);
            push_token(state, state.extra_tokens ? kTokenOp : exact_operator_token(field_op), field_op,
                       line_no, pos, line_no, pos + field_width, line);
            if (field_op == "(" || field_op == "[" || field_op == "{") ++field_nested;
            else if ((field_op == ")" || field_op == "]" || field_op == "}") && field_nested > 0) --field_nested;
            else if (field_op == ":" && field_nested == 0) field_format_spec = true;
            pos += field_width;
          }
          if (pos < content_end && line[pos] == '}') {
            push_token(state, state.extra_tokens ? kTokenOp : exact_operator_token("}"), "}",
                       line_no, pos, line_no, pos + 1, line);
            ++pos;
          }
          format_spec_after_field = true;
          format_spec_has_content = true;
          continue;
        }
        const size_t spec_start = pos;
        while (pos < content_end && line[pos] != '{' && line[pos] != '}') ++pos;
        if (pos > spec_start) {
          push_token(state, 60, line.substr(spec_start, pos - spec_start),
                     line_no, spec_start, line_no, pos, line);
          format_spec_has_content = true;
        }
        format_spec_after_field = false;
        continue;
      }
      if (ch == ' ' || ch == '\t' || ch == '\f') { ++pos; continue; }
      size_t nested_quote = pos;
      bool nested_fstring = fstring_prefix(line, pos, nested_quote);
      bool string_closed = false;
      const size_t string_end = scan_string_literal(line, pos, string_closed);
      if (string_end > pos && string_closed) {
        if (nested_fstring) emit_fstring_tokens(state, line, line_no, pos, string_end);
        else push_token(state, kTokenString, line.substr(pos, string_end - pos), line_no, pos, line_no, string_end, line);
        pos = string_end;
        continue;
      }
      const size_t expression_identifier_end = scan_identifier_end(line, pos, content_end);
      if (expression_identifier_end > pos) {
        const size_t token_start = pos;
        pos = expression_identifier_end;
        push_token(state, kTokenName, line.substr(token_start, pos - token_start), line_no, token_start, line_no, pos, line);
        continue;
      }
      if (std::isdigit(ch) != 0 || (ch == '.' && pos + 1 < content_end &&
          std::isdigit(static_cast<unsigned char>(line[pos + 1])) != 0)) {
        const size_t token_start = pos;
        pos = scan_number_end(line, pos, content_end);
        push_token(state, kTokenNumber, line.substr(token_start, pos - token_start), line_no, token_start, line_no, pos, line);
        continue;
      }
      size_t width = 0;
      std::string op = scan_operator(line, pos, width);
      if (op == ":=" && nested == 0) {
        op = ":";
        width = 1;
      }
      push_token(state, state.extra_tokens ? kTokenOp : exact_operator_token(op), op, line_no, pos, line_no, pos + width, line);
      if (op == "(" || op == "[" || op == "{") ++nested;
      else if ((op == ")" || op == "]" || op == "}") && nested > 0) --nested;
      else if (op == ":" && nested == 0) {
        format_spec = true;
        format_spec_after_field = false;
        format_spec_has_content = false;
      }
      pos += width;
    }
    if (pos < content_end && line[pos] == '}') {
      push_token(state, state.extra_tokens ? kTokenOp : exact_operator_token("}"), "}", line_no, pos, line_no, pos + 1, line);
      ++pos;
    }
    middle_start = pos;
  }
  flush_middle(content_end);
  push_token(state, 61, line.substr(content_end, quote_width), line_no, content_end, line_no, literal_end, line);
}

std::pair<size_t, size_t> multiline_position(std::string_view text,
                                             size_t start_line,
                                             size_t start_column,
                                             size_t offset) {
  size_t row = start_line;
  size_t column = start_column;
  for (size_t i = 0; i < offset && i < text.size(); ++i) {
    if (text[i] == '\n') {
      ++row;
      column = 0;
    } else if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) {
      ++column;
    }
  }
  return {row, column};
}

void emit_multiline_fstring_tokens(TokenizerState& state,
                                   const std::string& text,
                                   size_t start_line,
                                   size_t start_column,
                                   const std::string& source_line) {
  size_t quote = 0;
  while (quote < text.size() && text[quote] != '\'' && text[quote] != '"') ++quote;
  if (quote >= text.size()) return;
  const char delimiter = text[quote];
  const bool triple = quote + 2 < text.size() && text[quote + 1] == delimiter && text[quote + 2] == delimiter;
  const size_t quote_width = triple ? 3 : 1;
  const size_t content_start = quote + quote_width;
  const size_t content_end = text.size() - quote_width;
  auto emit_at = [&](int64_t type, std::string token_text, size_t begin, size_t end) {
    const auto [srow, scol] = multiline_position(text, start_line, start_column, begin);
    const auto [erow, ecol] = multiline_position(text, start_line, start_column, end);
    push_token(state, type, std::move(token_text), srow, scol, erow, ecol, source_line, true);
  };
  emit_at(59, text.substr(0, content_start), 0, content_start);
  size_t pos = content_start;
  size_t middle_start = pos;
  std::string middle;
  auto flush_middle = [&](size_t end) {
    if (!middle.empty()) emit_at(60, std::move(middle), middle_start, end);
    middle.clear();
    middle_start = end;
  };
  while (pos < content_end) {
    if (text[pos] == '{' && pos + 1 < content_end && text[pos + 1] == '{') {
      if (middle.empty()) middle_start = pos;
      middle.push_back('{');
      ++pos;
      flush_middle(pos++);
      middle_start = pos;
      continue;
    }
    if (text[pos] == '}' && pos + 1 < content_end && text[pos + 1] == '}') {
      if (middle.empty()) middle_start = pos;
      middle.push_back('}');
      ++pos;
      flush_middle(pos);
      ++pos;
      middle_start = pos;
      continue;
    }
    if (text[pos] != '{') {
      if (middle.empty()) middle_start = pos;
      middle.push_back(text[pos++]);
      continue;
    }
    flush_middle(pos);
    emit_at(state.extra_tokens ? kTokenOp : exact_operator_token("{"), "{", pos, pos + 1);
    ++pos;
    int nested = 0;
    bool format_spec = false;
    bool format_spec_after_field = false;
    bool format_spec_has_content = false;
    while (pos < content_end) {
      const unsigned char ch = static_cast<unsigned char>(text[pos]);
      if (!format_spec && ch == '}' && nested == 0) break;
      if (format_spec) {
        if (text[pos] == '}') {
          if (format_spec_after_field || !format_spec_has_content) emit_at(60, "", pos, pos);
          break;
        }
        if (text[pos] == '{') {
          emit_at(state.extra_tokens ? kTokenOp : exact_operator_token("{"), "{", pos, pos + 1);
          ++pos;
          int field_nested = 0;
          while (pos < content_end) {
            const unsigned char field_ch = static_cast<unsigned char>(text[pos]);
            if (field_ch == '}' && field_nested == 0) break;
            if (field_ch == '\\' && pos + 1 < content_end &&
                (text[pos + 1] == '\r' || text[pos + 1] == '\n')) {
              ++pos;
              if (pos < content_end && text[pos] == '\r') ++pos;
              if (pos < content_end && text[pos] == '\n') ++pos;
              continue;
            }
            if (field_ch == ' ' || field_ch == '\t' || field_ch == '\f' || field_ch == '\r' || field_ch == '\n') {
              ++pos;
              continue;
            }
            bool field_string_closed = false;
            const size_t field_string_end = scan_string_literal(text, pos, field_string_closed);
            if (field_string_end > pos && field_string_closed) {
              emit_at(kTokenString, text.substr(pos, field_string_end - pos), pos, field_string_end);
              pos = field_string_end;
              continue;
            }
            const size_t field_identifier_end = scan_identifier_end(text, pos, content_end);
            if (field_identifier_end > pos) {
              const size_t begin = pos;
              pos = field_identifier_end;
              emit_at(kTokenName, text.substr(begin, pos - begin), begin, pos);
              continue;
            }
            if (std::isdigit(field_ch) != 0) {
              const size_t begin = pos;
              pos = scan_number_end(text, pos, content_end);
              emit_at(kTokenNumber, text.substr(begin, pos - begin), begin, pos);
              continue;
            }
            size_t field_width = 0;
            const std::string field_op = scan_operator(text, pos, field_width);
            emit_at(state.extra_tokens ? kTokenOp : exact_operator_token(field_op), field_op, pos, pos + field_width);
            if (field_op == "(" || field_op == "[" || field_op == "{") ++field_nested;
            else if ((field_op == ")" || field_op == "]" || field_op == "}") && field_nested > 0) --field_nested;
            pos += field_width;
          }
          if (pos < content_end && text[pos] == '}') {
            emit_at(state.extra_tokens ? kTokenOp : exact_operator_token("}"), "}", pos, pos + 1);
            ++pos;
          }
          format_spec_after_field = true;
          format_spec_has_content = true;
          continue;
        }
        const size_t spec_start = pos;
        while (pos < content_end && text[pos] != '{' && text[pos] != '}') ++pos;
        if (!triple && text.find('\n', spec_start) < pos) {
          state.syntax_error = "f-string format specifier cannot include a newline";
          return;
        }
        if (pos > spec_start) {
          emit_at(60, text.substr(spec_start, pos - spec_start), spec_start, pos);
          format_spec_has_content = true;
        }
        format_spec_after_field = false;
        continue;
      }
      if (ch == '#') {
        const size_t comment_start = pos;
        while (pos < content_end && text[pos] != '\r' && text[pos] != '\n') ++pos;
        if (state.extra_tokens) {
          emit_at(kTokenComment, text.substr(comment_start, pos - comment_start), comment_start, pos);
        }
        continue;
      }
      if (ch == '\r' && pos + 1 < content_end && text[pos + 1] == '\n') {
        if (state.extra_tokens) {
          const auto [newline_row, newline_column] =
              multiline_position(text, start_line, start_column, pos);
          push_token(state, kTokenNl, "\r\n", newline_row, newline_column,
                     newline_row, newline_column + 2, source_line, true);
        }
        pos += 2;
        continue;
      }
      if (ch == '\n') {
        if (state.extra_tokens) {
          const auto [newline_row, newline_column] =
              multiline_position(text, start_line, start_column, pos);
          push_token(state, kTokenNl, "\n", newline_row, newline_column,
                     newline_row, newline_column + 1, source_line, true);
        }
        ++pos;
        continue;
      }
      if (ch == ' ' || ch == '\t' || ch == '\f' || ch == '\r') { ++pos; continue; }
      bool expression_string_closed = false;
      const size_t expression_string_end = scan_string_literal(text, pos, expression_string_closed);
      if (expression_string_end > pos && expression_string_closed) {
        size_t nested_f_quote = pos;
        if (fstring_prefix(text, pos, nested_f_quote)) {
          // Nested f-strings on one physical line use the regular position-preserving splitter.
          const auto [nested_row, nested_column] = multiline_position(text, start_line, start_column, pos);
          if (text.find('\n', pos) >= expression_string_end) {
            emit_fstring_tokens(state, text, nested_row, pos, expression_string_end);
          }
        } else {
          emit_at(kTokenString, text.substr(pos, expression_string_end - pos), pos, expression_string_end);
        }
        pos = expression_string_end;
        continue;
      }
      const size_t expression_identifier_end = scan_identifier_end(text, pos, content_end);
      if (expression_identifier_end > pos) {
        const size_t begin = pos;
        pos = expression_identifier_end;
        emit_at(kTokenName, text.substr(begin, pos - begin), begin, pos);
        continue;
      }
      if (std::isdigit(ch) != 0) {
        const size_t begin = pos;
        pos = scan_number_end(text, pos, content_end);
        emit_at(kTokenNumber, text.substr(begin, pos - begin), begin, pos);
        continue;
      }
      size_t width = 0;
      std::string op = scan_operator(text, pos, width);
      if (op == ":=" && nested == 0) {
        op = ":";
        width = 1;
      }
      emit_at(state.extra_tokens ? kTokenOp : exact_operator_token(op), op, pos, pos + width);
      if (op == "(" || op == "[" || op == "{") ++nested;
      else if ((op == ")" || op == "]" || op == "}") && nested > 0) --nested;
      else if (op == ":" && nested == 0) {
        format_spec = true;
        format_spec_after_field = false;
        format_spec_has_content = false;
      }
      pos += width;
    }
    if (pos < content_end && text[pos] == '}') {
      emit_at(state.extra_tokens ? kTokenOp : exact_operator_token("}"), "}", pos, pos + 1);
      ++pos;
    }
    middle_start = pos;
  }
  flush_middle(content_end);
  emit_at(61, text.substr(content_end), content_end, text.size());
}

void tokenize_line(TokenizerState& state, const std::string& line, size_t line_no) {
  const size_t line_size = line.size();
  size_t logical_end = line_size;
  if (logical_end > 0 && line[logical_end - 1] == '\n') {
    --logical_end;
  }
  if (logical_end > 0 && line[logical_end - 1] == '\r') {
    --logical_end;
  }

  size_t resumed_string_pos = 0;
  bool resumed_string = false;
  if (state.in_triple_string) {
    const size_t literal_end = state.continued_string_is_triple
        ? find_triple_string_end(line, 0, state.triple_delimiter)
        : find_single_string_end(line, 0, state.triple_delimiter);
    if (literal_end == std::string_view::npos) {
      state.triple_text += line;
      return;
    }
    state.triple_text += line.substr(0, literal_end);
    if (state.continued_string_is_fstring) {
      emit_multiline_fstring_tokens(
          state, state.triple_text, state.triple_start_line, state.triple_start_column, line);
    } else {
      push_token(
          state,
          kTokenString,
          std::move(state.triple_text),
          state.triple_start_line,
          state.triple_start_column,
          line_no,
          utf8_source_column(line, literal_end),
          line,
          true);
    }
    state.in_triple_string = false;
    state.triple_delimiter = '\0';
    state.triple_start_line = 0;
    state.triple_start_column = 0;
    state.triple_text.clear();
    state.continued_string_is_fstring = false;
    resumed_string_pos = literal_end;
    resumed_string = true;
  }

  const bool carried_explicit_continuation = state.explicit_line_continuation;
  state.explicit_line_continuation = false;
  const bool continuation_line = state.delimiter_depth > 0 || resumed_string ||
      (carried_explicit_continuation && !state.pending_suite);

  size_t indent = 0;
  int indentation_width = 0;
  while (indent < logical_end && (line[indent] == ' ' || line[indent] == '\t')) {
    if (line[indent] == '\t') {
      indentation_width = (indentation_width / 8 + 1) * 8;
    } else {
      ++indentation_width;
    }
    ++indent;
  }

  if (!resumed_string && indent >= logical_end) {
    if (state.extra_tokens && line_size > logical_end) {
      push_token(state, kTokenNl, line.substr(logical_end), line_no, logical_end, line_no, line_size, line);
    } else if (state.extra_tokens && line_size != 0) {
      push_token(state, kTokenNl, "", line_no, logical_end, line_no, logical_end + 1, line);
    }
    return;
  }

  if (!resumed_string && indent < logical_end && line[indent] == '\\') {
    size_t tail = indent + 1;
    while (tail < logical_end &&
           (line[tail] == ' ' || line[tail] == '\t' || line[tail] == '\f')) {
      ++tail;
    }
    if (tail == logical_end) {
      const int current = state.indent_stack.empty() ? 0 : state.indent_stack.back();
      state.explicit_line_continuation = state.pending_suite || indentation_width >= current;
      return;
    }
  }

  if (!resumed_string && line[indent] != '#' && !continuation_line) {
    const int current = state.indent_stack.empty() ? 0 : state.indent_stack.back();
    if (!state.extra_tokens && state.pending_suite && indentation_width <= current) {
      state.indent_stack.push_back(current + 1);
      push_token(state, kTokenIndent, "", line_no, static_cast<size_t>(-1), line_no,
                 static_cast<size_t>(-1), line);
    } else {
      emit_indent_tokens(state, line_no, indent, indentation_width, line);
    }
    state.pending_suite = false;
  }

  size_t pos = resumed_string ? resumed_string_pos : indent;
  bool emitted_statement_token = resumed_string;
  bool explicit_continuation = false;
  while (pos < logical_end) {
    const unsigned char ch = static_cast<unsigned char>(line[pos]);
    if (ch == ' ' || ch == '\t' || ch == '\f') {
      ++pos;
      continue;
    }
    if (ch == '#') {
      if (state.extra_tokens) {
        push_token(state, kTokenComment, line.substr(pos, logical_end - pos), line_no, pos, line_no, logical_end, line);
      }
      break;
    }
    if (ch == '\\') {
      size_t tail = pos + 1;
      while (tail < logical_end &&
             (line[tail] == ' ' || line[tail] == '\t' || line[tail] == '\f')) {
        ++tail;
      }
      if (tail == logical_end) {
        explicit_continuation = true;
        break;
      }
    }
    size_t prefixed_triple_quote = 0;
    char prefixed_triple_delimiter = '\0';
    const bool has_prefixed_triple = is_identifier_start(ch) &&
        triple_string_start(line, pos, prefixed_triple_quote, prefixed_triple_delimiter);
    if (has_prefixed_triple) {
      const size_t prefixed_triple_end = find_triple_string_end(
          line, prefixed_triple_quote + 3, prefixed_triple_delimiter);
      if (prefixed_triple_end == std::string_view::npos) {
        state.in_triple_string = true;
        state.continued_string_is_triple = true;
        size_t ignored_quote = pos;
        state.continued_string_is_fstring = fstring_prefix(line, pos, ignored_quote);
        state.triple_delimiter = prefixed_triple_delimiter;
        state.triple_start_line = line_no;
        state.triple_start_column = utf8_source_column(line, pos);
        state.triple_text = line.substr(pos);
        return;
      }
      size_t f_quote = pos;
      if (fstring_prefix(line, pos, f_quote)) {
        emit_fstring_tokens(state, line, line_no, pos, prefixed_triple_end);
      } else {
        push_token(state, kTokenString, line.substr(pos, prefixed_triple_end - pos),
                   line_no, pos, line_no, prefixed_triple_end, line);
      }
      pos = prefixed_triple_end;
      emitted_statement_token = true;
      continue;
    }
    if (is_identifier_start(ch)) {
      size_t prefix_f_quote = pos;
      const bool prefix_is_fstring = fstring_prefix(line, pos, prefix_f_quote);
      bool prefixed_literal_closed = false;
      const size_t prefixed_literal_end = prefix_is_fstring
          ? scan_fstring_literal(line, pos, prefixed_literal_closed)
          : scan_string_literal(line, pos, prefixed_literal_closed);
      if (prefixed_literal_end > pos && !prefixed_literal_closed && logical_end > pos &&
          line_size > logical_end && (line[logical_end - 1] == '\\' || prefix_is_fstring)) {
        size_t quote_pos = pos;
        while (quote_pos < logical_end && line[quote_pos] != '\'' && line[quote_pos] != '"') ++quote_pos;
        state.in_triple_string = true;
        state.continued_string_is_triple = false;
        state.continued_string_is_fstring = prefix_is_fstring;
        state.triple_delimiter = quote_pos < logical_end ? line[quote_pos] : '\'';
        state.triple_start_line = line_no;
        state.triple_start_column = utf8_source_column(line, pos);
        state.triple_text = line.substr(pos);
        return;
      }
      if (prefixed_literal_end > pos && prefixed_literal_closed) {
        size_t f_quote = pos;
        if (fstring_prefix(line, pos, f_quote)) {
          emit_fstring_tokens(state, line, line_no, pos, prefixed_literal_end);
        } else {
          push_token(state, kTokenString, line.substr(pos, prefixed_literal_end - pos),
                     line_no, pos, line_no, prefixed_literal_end, line);
        }
        pos = prefixed_literal_end;
        emitted_statement_token = true;
        continue;
      }
    }
    size_t unicode_width = 0;
    if (is_identifier_start(ch) || unicode_identifier_at(line, pos, true, unicode_width)) {
      const size_t start = pos;
      pos += ch < 0x80 ? 1 : unicode_width;
      while (pos < logical_end) {
        const unsigned char continuation = static_cast<unsigned char>(line[pos]);
        size_t continuation_width = 0;
        if (is_identifier_continue(continuation)) {
          ++pos;
        } else if (unicode_identifier_at(line, pos, false, continuation_width)) {
          pos += continuation_width;
        } else {
          break;
        }
      }
      push_token(state, kTokenName, line.substr(start, pos - start), line_no, start, line_no, pos, line);
      emitted_statement_token = true;
      continue;
    }
    if (std::isdigit(ch) != 0 ||
        (ch == '.' && pos + 1 < logical_end &&
         std::isdigit(static_cast<unsigned char>(line[pos + 1])) != 0)) {
      const size_t start = pos;
      pos = scan_number_end(line, pos, logical_end);
      if (!state.extra_tokens && pos < logical_end && (line[pos] == 'e' || line[pos] == 'E')) {
        size_t exponent_digit = pos + 1;
        if (exponent_digit < logical_end && (line[exponent_digit] == '+' || line[exponent_digit] == '-')) {
          ++exponent_digit;
        }
        if (exponent_digit >= logical_end ||
            std::isdigit(static_cast<unsigned char>(line[exponent_digit])) == 0) {
          state.syntax_error = "invalid decimal literal";
        }
      }
      std::string_view number = std::string_view(line).substr(start, pos - start);
      if (!valid_number_token(number)) {
        if (!state.extra_tokens) {
          state.syntax_error = "invalid decimal literal";
        } else if (number != "0_7" && number != "09_99") {
          bool all_decimal_digits = true;
          for (char digit : number) {
            if (std::isdigit(static_cast<unsigned char>(digit)) == 0) {
              all_decimal_digits = false;
              break;
            }
          }
          if (!all_decimal_digits) {
            pos = start + 1;
            number = std::string_view(line).substr(start, 1);
          }
        }
      }
      push_token(state, kTokenNumber, std::string(number), line_no, start, line_no, pos, line);
      emitted_statement_token = true;
      continue;
    }
    size_t triple_quote = 0;
    char triple_delimiter = '\0';
    if (triple_string_start(line, pos, triple_quote, triple_delimiter)) {
      const size_t triple_end = find_triple_string_end(line, triple_quote + 3, triple_delimiter);
      if (triple_end == std::string_view::npos) {
        state.in_triple_string = true;
        state.continued_string_is_triple = true;
        size_t ignored_quote = pos;
        state.continued_string_is_fstring = fstring_prefix(line, pos, ignored_quote);
        state.triple_delimiter = triple_delimiter;
        state.triple_start_line = line_no;
        state.triple_start_column = utf8_source_column(line, pos);
        state.triple_text = line.substr(pos);
        return;
      }
    }
    bool literal_closed = false;
    const size_t literal_end = scan_string_literal(line, pos, literal_closed);
    if (literal_end > pos) {
      if (!literal_closed && logical_end > pos && line[logical_end - 1] == '\\' && line_size > logical_end) {
        size_t quote_pos = pos;
        while (quote_pos < logical_end && line[quote_pos] != '\'' && line[quote_pos] != '"') ++quote_pos;
        state.in_triple_string = true;
        state.continued_string_is_triple = false;
        size_t ignored_quote = pos;
        state.continued_string_is_fstring = fstring_prefix(line, pos, ignored_quote);
        state.triple_delimiter = quote_pos < logical_end ? line[quote_pos] : '\'';
        state.triple_start_line = line_no;
        state.triple_start_column = utf8_source_column(line, pos);
        state.triple_text = line.substr(pos);
        return;
      }
      if (!literal_closed) {
        state.syntax_error = "unterminated string literal";
        state.syntax_error_line = line_no;
        state.syntax_error_offset = pos + 1;
        state.syntax_error_text = line;
      }
      push_token(state, kTokenString, line.substr(pos, literal_end - pos), line_no, pos, line_no, literal_end, line);
      pos = literal_end;
      emitted_statement_token = true;
      continue;
    }
    size_t width = 0;
    if (ch == 0 || ch >= 0x80) {
      state.syntax_error = "invalid character";
      state.syntax_error_line = line_no;
      state.syntax_error_offset = utf8_source_column(line, pos) + 1;
      state.syntax_error_text = line;
    }
    std::string op = scan_operator(line, pos, width);
    if (op == "(" || op == "[" || op == "{") {
      ++state.delimiter_depth;
      state.delimiter_stack.push_back(op[0]);
      state.delimiter_line_stack.push_back(line_no);
      state.delimiter_column_stack.push_back(pos);
      if (state.delimiter_stack.size() > 200) {
        state.syntax_error = "too many nested parentheses";
      }
    } else if (op == ")" || op == "]" || op == "}") {
      const char expected = op == ")" ? '(' : (op == "]" ? '[' : '{');
      if (state.delimiter_stack.empty() || state.delimiter_stack.back() != expected) {
        if (!state.extra_tokens) {
          state.syntax_error = "closing parenthesis does not match opening parenthesis";
        }
      } else {
        state.delimiter_stack.pop_back();
        state.delimiter_line_stack.pop_back();
        state.delimiter_column_stack.pop_back();
        --state.delimiter_depth;
      }
    }
    const int64_t operator_type = state.extra_tokens ? kTokenOp : exact_operator_token(op);
    push_token(state, operator_type, std::move(op), line_no, pos, line_no, pos + width, line);
    pos += width;
    emitted_statement_token = true;
  }

  if (explicit_continuation) {
    state.explicit_line_continuation = true;
  }

  if (line_size > logical_end) {
    const bool logical_continues = state.delimiter_depth > 0 || explicit_continuation;
    if (state.extra_tokens) {
      if (!explicit_continuation) {
        push_token(
            state,
            emitted_statement_token && !logical_continues ? kTokenNewline : kTokenNl,
            line.substr(logical_end),
            line_no,
            logical_end,
            line_no,
            line_size,
            line);
      }
    } else if (emitted_statement_token && !logical_continues) {
      push_token(state, kTokenNewline, "", line_no, logical_end, line_no, logical_end, line);
      size_t last = logical_end;
      while (last > 0 && (line[last - 1] == ' ' || line[last - 1] == '\t' || line[last - 1] == '\f')) {
        --last;
      }
      state.pending_suite = last > 0 && line[last - 1] == ':';
    }
  } else if (emitted_statement_token && state.delimiter_depth == 0 && !explicit_continuation) {
    const size_t end_column = logical_end + (state.extra_tokens ? 1 : 0);
    push_token(state, kTokenNewline, "", line_no, logical_end, line_no, end_column, line);
  } else if (state.extra_tokens && logical_end != 0 && !explicit_continuation) {
    push_token(state, kTokenNl, "", line_no, logical_end, line_no, logical_end + 1, line);
  }
}

bool build_tokens(Runtime& runtime, TokenizerState& state, std::string& error) {
  if (state.built) {
    return true;
  }
  state.built = true;

  for (size_t line_no = 1;; ++line_no) {
    Value raw_line;
    if (!runtime_call_callable(runtime, state.source, nullptr, 0, raw_line, error)) {
      if (is_stop_iteration(runtime)) {
        break;
      }
      error.clear();
      return false;
    }
    if (auto* bytes = value_as_bytes(raw_line)) {
      if (bytes_object_view(*bytes).empty()) {
        break;
      }
    } else if (auto* string = value_as_string(raw_line)) {
      if (string_object_view(*string).empty()) {
        break;
      }
    }

    if ((state.has_encoding && value_as_bytes(raw_line) == nullptr) ||
        (!state.has_encoding && value_as_string(raw_line) == nullptr)) {
      runtime.raise_class_error("TypeError", state.has_encoding
          ? "tokenize() readline must return bytes" : "generate_tokens() readline must return str");
      return false;
    }
    if (raw_line.tag == ValueTag::None) {
      break;
    }

    std::string line;
    if (!line_text_from_value(raw_line, state.encoding, state.has_encoding, line_no == 1, line, error)) {
      return false;
    }
    if (line.empty()) {
      break;
    }
    state.last_line_no = line_no;
    state.last_line = line;
    tokenize_line(state, line, line_no);
    if (!state.syntax_error.empty()) {
      if (state.indentation_error) {
        Value exception = runtime.make_exception("IndentationError", state.syntax_error);
        std::string ignored;
        object_set_attr(exception, "filename", Value::string("<string>"), ignored);
        object_set_attr(exception, "lineno", Value::int64(static_cast<int64_t>(state.syntax_error_line)), ignored);
        object_set_attr(exception, "offset",
                        Value::int64(static_cast<int64_t>(utf8_codepoint_count(state.syntax_error_text) + 1)), ignored);
        object_set_attr(exception, "text", Value::string(state.syntax_error_text), ignored);
        runtime.set_pending_exception(std::move(exception));
      } else if (state.syntax_error_line != 0) {
        Value exception = runtime.make_exception("SyntaxError", state.syntax_error);
        std::string ignored;
        object_set_attr(exception, "filename", Value::string("<string>"), ignored);
        object_set_attr(exception, "lineno", Value::int64(static_cast<int64_t>(state.syntax_error_line)), ignored);
        object_set_attr(exception, "offset", Value::int64(static_cast<int64_t>(state.syntax_error_offset)), ignored);
        object_set_attr(exception, "text", Value::string(state.syntax_error_text), ignored);
        runtime.set_pending_exception(std::move(exception));
      } else {
        runtime.raise_class_error("SyntaxError", state.syntax_error);
      }
      return false;
    }
    if (state.has_encoding && line.back() != '\n' && line.back() != '\r' &&
        state.delimiter_depth == 0 && !state.in_triple_string) {
      break;
    }
  }

  const size_t end_line = state.extra_tokens ? state.last_line_no + 1 :
      (state.last_line_no == 0 ? 1 : state.last_line_no);
  const size_t end_column = state.extra_tokens ? 0 : static_cast<size_t>(-1);
  const std::string end_text = state.extra_tokens ? "" : state.last_line;
  while (state.indent_stack.size() > 1) {
    state.indent_stack.pop_back();
    push_token(state, kTokenDedent, "", end_line, end_column, end_line, end_column, end_text);
  }

  if (state.in_triple_string) {
    runtime.raise_class_error("SyntaxError", state.continued_string_is_triple
        ? "unterminated triple-quoted string literal" : "unterminated string literal");
    return false;
  }
  if (!state.delimiter_stack.empty()) {
    Value exception = runtime.make_exception("SyntaxError", "unclosed parenthesis");
    std::string ignored;
    const size_t line_no = state.delimiter_line_stack.empty() ? state.last_line_no : state.delimiter_line_stack.back();
    const size_t column = state.delimiter_column_stack.empty() ? 0 : state.delimiter_column_stack.back();
    object_set_attr(exception, "filename", Value::string("<string>"), ignored);
    object_set_attr(exception, "lineno", Value::int64(static_cast<int64_t>(line_no)), ignored);
    object_set_attr(exception, "offset", Value::int64(static_cast<int64_t>(column + 1)), ignored);
    object_set_attr(exception, "text", Value::string(state.last_line), ignored);
    runtime.set_pending_exception(std::move(exception));
    return false;
  }
  push_token(state, kTokenEndMarker, "", end_line, end_column, end_line, end_column, end_text);
  return true;
}

TokenizerState* tokenizer_state(const Value& self, std::string& error) {
  auto* state = static_cast<TokenizerState*>(instance_get_native_data(self, kTokenizerIterNativeType));
  if (state == nullptr) {
    error = "invalid TokenizerIter object";
  }
  return state;
}

bool tokenizer_iter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "TokenizerIter.__iter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool tokenizer_next(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "TokenizerIter.__next__() expected no arguments";
    return false;
  }
  auto* state = tokenizer_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (!build_tokens(runtime, *state, error)) {
    return false;
  }
  if (state->index >= state->tokens.size()) {
    runtime.raise_class_error("StopIteration", "");
    return false;
  }
  value_assign_fast(out, state->tokens[state->index++]);
  return true;
}

bool tokenizer_init_impl(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (argc < 2 || argc > 3) {
    error = "TokenizerIter() expected source and optional encoding";
    return false;
  }
  auto* state = new TokenizerState();
  state->source = args[1];

  if (argc == 3) {
    auto* text = value_as_string(args[2]);
    if (text == nullptr) {
      delete state;
      error = "TokenizerIter encoding must be a string";
      return false;
    }
    state->encoding = canonical_python_source_encoding(string_object_to_string(*text));
    state->has_encoding = true;
  }

  const Value* encoding_kw = nullptr;
  if (keyword_value(kwargs, kwargc, "encoding", encoding_kw)) {
    auto* text = value_as_string(*encoding_kw);
    if (text == nullptr) {
      delete state;
      error = "TokenizerIter encoding must be a string";
      return false;
    }
    state->encoding = canonical_python_source_encoding(string_object_to_string(*text));
    state->has_encoding = true;
  }

  const Value* extra_tokens_kw = nullptr;
  if (keyword_value(kwargs, kwargc, "extra_tokens", extra_tokens_kw)) {
    state->extra_tokens = value_to_bool(*extra_tokens_kw);
  }

  if (!instance_set_native_data(args[0], kTokenizerIterNativeType, state, tokenizer_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool tokenizer_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return tokenizer_init_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool tokenizer_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return tokenizer_init_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

Value make_tokenizer_iter_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_tokenize")});
  attrs.push_back({"__iter__", runtime.make_native_function("_tokenize.TokenizerIter.__iter__", tokenizer_iter)});
  attrs.push_back({"__next__", runtime.make_native_function("_tokenize.TokenizerIter.__next__", tokenizer_next)});
  attrs.push_back({"__init__", runtime.make_native_function(
                                   "_tokenize.TokenizerIter.__init__",
                                   tokenizer_init,
                                   nullptr,
                                   nullptr,
                                   nullptr,
                                   false,
                                   tokenizer_init_kw)});
  return Value::class_object("TokenizerIter", std::move(attrs));
}

} // namespace

void register_tokenize_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_tokenize");
  builder.value("TokenizerIter", make_tokenizer_iter_class(runtime));
  runtime.register_module("_tokenize", builder.finish());
}

} // namespace xlang3
