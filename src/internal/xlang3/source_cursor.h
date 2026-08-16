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

#include <cstdint>
#include <string>
#include <string_view>

namespace xlang3 {

struct SourceLine {
  std::string_view text;
  uint32_t line = 1;
};

class SourceLines {
public:
  explicit SourceLines(std::string_view source);

  bool next(SourceLine& out);

private:
  std::string_view source_;
  size_t offset_ = 0;
  uint32_t line_ = 1;
  bool finished_ = false;
};

class SourceCursor {
public:
  explicit SourceCursor(std::string_view source);

  bool eof() const;
  char peek(size_t offset = 0) const;
  char advance();
  bool match(char ch);

  size_t offset() const { return offset_; }
  uint32_t line() const { return line_; }
  uint32_t column() const { return column_; }

private:
  std::string_view source_;
  size_t offset_ = 0;
  uint32_t line_ = 1;
  uint32_t column_ = 1;
};

std::string_view trim_ascii_space(std::string_view text);
bool next_ascii_word(std::string_view text, size_t& offset, std::string_view& word);

} // namespace xlang3
