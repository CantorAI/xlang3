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
#include "xlang3/source_cursor.h"

#include <cctype>

namespace xlang3 {

SourceLines::SourceLines(std::string_view source) : source_(source) {}

bool SourceLines::next(SourceLine& out) {
  if (finished_) {
    return false;
  }
  size_t line_end = source_.find('\n', offset_);
  if (line_end == std::string_view::npos) {
    line_end = source_.size();
    finished_ = true;
  }

  auto text = source_.substr(offset_, line_end - offset_);
  if (!text.empty() && text.back() == '\r') {
    text.remove_suffix(1);
  }
  out = SourceLine{text, line_++};
  offset_ = line_end + 1;
  return true;
}

SourceCursor::SourceCursor(std::string_view source) : source_(source) {}

bool SourceCursor::eof() const {
  return offset_ >= source_.size();
}

char SourceCursor::peek(size_t offset) const {
  const size_t pos = offset_ + offset;
  return pos < source_.size() ? source_[pos] : '\0';
}

char SourceCursor::advance() {
  if (eof()) {
    return '\0';
  }
  const char ch = source_[offset_++];
  if (ch == '\n') {
    ++line_;
    column_ = 1;
  } else {
    ++column_;
  }
  return ch;
}

bool SourceCursor::match(char ch) {
  if (peek() != ch) {
    return false;
  }
  advance();
  return true;
}

std::string_view trim_ascii_space(std::string_view text) {
  size_t first = 0;
  while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
    ++first;
  }
  size_t last = text.size();
  while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
    --last;
  }
  return text.substr(first, last - first);
}

bool next_ascii_word(std::string_view text, size_t& offset, std::string_view& word) {
  while (offset < text.size() && std::isspace(static_cast<unsigned char>(text[offset]))) {
    ++offset;
  }
  const size_t start = offset;
  while (offset < text.size() && !std::isspace(static_cast<unsigned char>(text[offset]))) {
    ++offset;
  }
  if (offset == start) {
    word = {};
    return false;
  }
  word = text.substr(start, offset - start);
  return true;
}

} // namespace xlang3
