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

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace xlang3 {

namespace {

constexpr const char* kPatternNativeType = "_sre.Pattern";
constexpr const char* kMatchNativeType = "_sre.Match";
constexpr const char* kFindIterNativeType = "_sre.FindIter";
constexpr const char* kScannerNativeType = "_sre.Scanner";
constexpr int64_t kSreMagic = 20230612;
constexpr int64_t kSreCodeSize = 4;
constexpr int64_t kFlagIgnoreCase = 2;
constexpr int64_t kFlagMultiline = 8;
constexpr int64_t kFlagDotAll = 16;
constexpr int64_t kFlagVerbose = 64;

enum class FastRegexKind {
  None,
  LiteralPrefix,
  AnySuffix,
  OrderedSuffix,
};

struct LookbehindAssertion {
  size_t engine_offset = 0;
  std::string literal;
  bool positive = true;
  char category = '\0';
  bool empty_only = false;
  int64_t group_ref = 0;
  bool anchor_from_end = false;
  size_t trailing_width = 0;
  int64_t conditional_group = 0;
  std::string conditional_yes;
  std::string conditional_no;
  bool forward = false;
  int64_t captures_before_assertion = 0;
  bool terminal_consuming = false;
  std::string expression;
  int64_t marker_group = 0;
  int64_t alternate_marker_group = 0;
};

struct BoundaryAssertion {
  bool at_end = false;
  bool boundary = true;
  bool empty_only = false;
};

struct PatternState {
  std::string pattern;
  std::string engine_pattern;
  bool bytes_pattern = false;
  int64_t flags = 0;
  bool regex_available = true;
  bool regex_compiled = false;
  std::regex::flag_type regex_flags = std::regex::ECMAScript;
  std::regex regex;
  FastRegexKind fast_kind = FastRegexKind::None;
  std::string fast_literal;
  std::vector<std::string> fast_literals;
  int64_t group_count = 0;
  std::unordered_map<std::string, int64_t> group_names;
  std::vector<int64_t> group_close_order;
  std::vector<size_t> engine_group_for_python;
  std::vector<LookbehindAssertion> lookbehinds;
  std::vector<BoundaryAssertion> boundaries;
  bool requires_absolute_start = false;
  bool requires_absolute_end = false;
  size_t minimum_match_start = 0;
};

bool regex_dot_repeat_lookbehind_width(std::string_view pattern, size_t& width, bool& positive) {
  if (pattern.size() < 6 || pattern.back() != ')' ||
      (pattern.substr(0, 4) != "(?<=" && pattern.substr(0, 4) != "(?<!")) return false;
  positive = pattern[3] == '=';
  size_t dots = 0;
  size_t product = 1;
  for (size_t i = 4; i + 1 < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (ch == '.') {
      ++dots;
      continue;
    }
    if (ch == '(' || ch == ')') continue;
    if (ch != '{') return false;
    size_t value = 0;
    bool saw_digit = false;
    while (++i + 1 < pattern.size() && pattern[i] >= '0' && pattern[i] <= '9') {
      saw_digit = true;
      const size_t digit = static_cast<size_t>(pattern[i] - '0');
      if (value > (std::numeric_limits<size_t>::max() - digit) / 10u) return false;
      value = value * 10u + digit;
    }
    if (!saw_digit || pattern[i] != '}' || value == 0 ||
        product > std::numeric_limits<size_t>::max() / value) return false;
    product *= value;
  }
  if (dots != 1) return false;
  width = product;
  return true;
}

struct MatchGroup {
  int64_t start = -1;
  int64_t end = -1;
  bool matched = false;
};

struct MatchState {
  Value pattern;
  Value subject;
  std::shared_ptr<const std::string> text;
  bool bytes_text = false;
  bool ascii_text = false;
  std::vector<MatchGroup> groups;
  int64_t pos = 0;
  int64_t endpos = 0;
};

struct FindIterState {
  Value pattern;
  Value subject;
  std::string text;
  bool bytes_text = false;
  size_t pos = 0;
  size_t cursor = 0;
  size_t endpos = 0;
  bool retry_nonempty_at_cursor = false;
  bool exports_bytearray = false;
};

PatternState* pattern_state(const Value& self, std::string& error) {
  auto* state = static_cast<PatternState*>(instance_get_native_data(self, kPatternNativeType));
  if (state == nullptr) {
    error = "invalid _sre.Pattern object";
  }
  return state;
}

MatchState* match_state(const Value& self, std::string& error) {
  auto* state = static_cast<MatchState*>(instance_get_native_data(self, kMatchNativeType));
  if (state == nullptr) {
    error = "invalid _sre.Match object";
  }
  return state;
}

void pattern_cleanup(void* data) {
  delete static_cast<PatternState*>(data);
}

void match_cleanup(void* data) {
  delete static_cast<MatchState*>(data);
}

void finditer_cleanup(void* data) {
  auto* state = static_cast<FindIterState*>(data);
  if (state->exports_bytearray) {
    if (auto* bytearray = value_as_bytearray(state->subject); bytearray != nullptr && bytearray->buffer_exports > 0) {
      --bytearray->buffer_exports;
    }
  }
  delete state;
}

void scanner_cleanup(void* data) {
  delete static_cast<FindIterState*>(data);
}

bool value_to_pattern_text(const Value& value, std::string& out, bool& is_bytes) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    is_bytes = false;
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    const auto view = bytes_object_view(*bytes);
    out.assign(view.data(), view.size());
    is_bytes = true;
    return true;
  }
  if (auto* instance = value_as_instance(value)) {
    auto* klass = value_as_class(instance->klass);
    Value payload;
    std::string ignored;
    if (klass != nullptr && class_has_builtin_base_name(klass, "str") &&
        object_get_attr(value, "__xlang3_string_value__", payload, ignored)) {
      if (auto* string = value_as_string(payload)) {
        out = string_object_to_string(*string);
        is_bytes = false;
        return true;
      }
    }
    ignored.clear();
    if (klass != nullptr && class_has_builtin_base_name(klass, "bytes") &&
        object_get_attr(value, "__xlang3_bytes_value__", payload, ignored)) {
      if (auto* bytes = value_as_bytes(payload)) {
        const auto view = bytes_object_view(*bytes);
        out.assign(view.data(), view.size());
        is_bytes = true;
        return true;
      }
    }
  }
  return false;
}

bool value_to_match_text(const Value& value, std::string& out, bool& is_bytes) {
  if (value_to_pattern_text(value, out, is_bytes)) {
    return true;
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    out = bytearray->value;
    is_bytes = true;
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    const auto bytes = memoryview_object_view(*view);
    out.assign(bytes.data(), bytes.size());
    is_bytes = true;
    return true;
  }
  if (value_as_instance(value) != nullptr) {
    Value payload;
    std::string ignored;
    if (object_get_attr(value, "__xlang3_string_value__", payload, ignored)) {
      if (auto* string = value_as_string(payload)) {
        out = string_object_to_string(*string);
        is_bytes = false;
        return true;
      }
    }
    ignored.clear();
    if (object_get_attr(value, "__xlang3_bytes_value__", payload, ignored)) {
      if (auto* bytes = value_as_bytes(payload)) {
        const auto view = bytes_object_view(*bytes);
        out.assign(view.data(), view.size());
        is_bytes = true;
        return true;
      }
    }
  }
  return false;
}

bool pattern_anchored_literal_miss(const PatternState& state, const Value& subject) {
  if ((state.flags & kFlagIgnoreCase) != 0) return false;
  size_t literal_offset = 0;
  if (state.pattern.rfind("\\A", 0) == 0) {
    literal_offset = 2;
  } else if (state.pattern.rfind('^', 0) == 0 && (state.flags & kFlagMultiline) == 0) {
    literal_offset = 1;
  } else {
    return false;
  }
  if (literal_offset >= state.pattern.size()) return false;
  const unsigned char literal = static_cast<unsigned char>(state.pattern[literal_offset]);
  if (literal >= 0x80u || std::string_view(R"(\.^$|()[]{}*+?)").find(static_cast<char>(literal)) != std::string_view::npos) {
    return false;
  }
  std::string_view text;
  bool bytes_subject = false;
  if (auto* string = value_as_string(subject)) {
    text = string_object_view(*string);
  } else if (auto* bytes = value_as_bytes(subject)) {
    text = bytes_object_view(*bytes);
    bytes_subject = true;
  } else {
    return false;
  }
  return bytes_subject == state.bytes_pattern && (text.empty() || static_cast<unsigned char>(text.front()) != literal);
}

bool ensure_pattern_regex(PatternState& state, std::string& error) {
  if (!state.regex_available) {
    error = "regular expression construct is not supported by the current native matcher";
    return false;
  }
  if (state.regex_compiled) {
    return true;
  }
  try {
    state.regex = std::regex(state.engine_pattern, state.regex_flags);
    state.regex_compiled = true;
    return true;
  } catch (const std::regex_error& exc) {
    error = std::string("bad regex pattern '") + state.pattern + "': " + exc.what();
    return false;
  }
}

bool sre_value_is_callable(const Value& value) {
  return value_as_function(value) != nullptr ||
         value_as_native_function(value) != nullptr ||
         value_as_bound_method(value) != nullptr ||
         value_as_class(value) != nullptr;
}

std::string strip_verbose_regex(std::string_view pattern) {
  std::string out;
  out.reserve(pattern.size());
  bool in_class = false;
  bool escaped = false;
  for (size_t i = 0; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      out.push_back(ch);
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      out.push_back(ch);
      escaped = true;
      continue;
    }
    if (ch == '[') {
      in_class = true;
      out.push_back(ch);
      continue;
    }
    if (ch == ']' && in_class) {
      in_class = false;
      out.push_back(ch);
      continue;
    }
    if (!in_class && ch == '#') {
      while (i + 1 < pattern.size() && pattern[i + 1] != '\n') {
        ++i;
      }
      continue;
    }
    if (!in_class && std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
    out.push_back(ch);
  }
  return out;
}

bool regex_brace_starts_repeat(std::string_view pattern, size_t open) {
  size_t i = open + 1;
  bool saw_first_digit = false;
  while (i < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[i])) != 0) {
    saw_first_digit = true;
    ++i;
  }
  if (i < pattern.size() && pattern[i] == ',') {
    ++i;
    while (i < pattern.size() && std::isdigit(static_cast<unsigned char>(pattern[i])) != 0) {
      ++i;
    }
    return i < pattern.size() && pattern[i] == '}';
  }
  return saw_first_digit && i < pattern.size() && pattern[i] == '}';
}

void regex_append_utf8_codepoint(std::string& out, uint32_t codepoint);

const std::string& regex_unicode_latin1_word_atom() {
  static const std::string atom = [] {
    std::string result = "(?:[A-Za-z0-9_]";
    for (uint32_t codepoint = 0x80; codepoint <= 0xff; ++codepoint) {
      const bool word = codepoint == 0xaau || codepoint == 0xb5u || codepoint == 0xbau ||
          (codepoint >= 0xc0u && codepoint <= 0xd6u) ||
          (codepoint >= 0xd8u && codepoint <= 0xf6u) ||
          (codepoint >= 0xf8u && codepoint <= 0xffu);
      if (!word) continue;
      result.push_back('|');
      regex_append_utf8_codepoint(result, codepoint);
    }
    result.push_back(')');
    return result;
  }();
  return atom;
}

const std::string& regex_unicode_latin1_letter_atom() {
  static const std::string atom = [] {
    std::string result = "(?:[A-Za-z_]";
    for (uint32_t codepoint = 0x80; codepoint <= 0xff; ++codepoint) {
      const bool letter = codepoint == 0xaau || codepoint == 0xb5u || codepoint == 0xbau ||
          (codepoint >= 0xc0u && codepoint <= 0xd6u) ||
          (codepoint >= 0xd8u && codepoint <= 0xf6u) ||
          (codepoint >= 0xf8u && codepoint <= 0xffu);
      if (!letter) continue;
      result.push_back('|');
      regex_append_utf8_codepoint(result, codepoint);
    }
    result.push_back(')');
    return result;
  }();
  return atom;
}

const std::string& regex_unicode_ascii_nonword_atom() {
  static const std::string atom =
      "(?:[\\xC2-\\xDF][\\x80-\\xBF]|[\\xE0-\\xEF][\\x80-\\xBF]{2}|"
      "[\\xF0-\\xF4][\\x80-\\xBF]{3}|[^A-Za-z0-9_\\x80-\\xFF])";
  return atom;
}

const std::string& regex_unicode_codepoint_atom(bool dotall) {
  static const std::string any =
      "(?:[\\x00-\\x7F]|[\\xC2-\\xDF][\\x80-\\xBF]|[\\xE0-\\xEF][\\x80-\\xBF]{2}|"
      "[\\xF0-\\xF4][\\x80-\\xBF]{3})";
  static const std::string except_newline =
      "(?:[\\x00-\\x09\\x0B-\\x7F]|[\\xC2-\\xDF][\\x80-\\xBF]|"
      "[\\xE0-\\xEF][\\x80-\\xBF]{2}|[\\xF0-\\xF4][\\x80-\\xBF]{3})";
  return dotall ? any : except_newline;
}

uint32_t regex_latin1_case_pair(uint32_t codepoint) {
#if defined(_WIN32)
  if (codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) return codepoint;
  wchar_t source[2]{};
  int source_count = 1;
  if (codepoint <= 0xffffu) {
    source[0] = static_cast<wchar_t>(codepoint);
  } else {
    const uint32_t adjusted = codepoint - 0x10000u;
    source[0] = static_cast<wchar_t>(0xd800u + (adjusted >> 10u));
    source[1] = static_cast<wchar_t>(0xdc00u + (adjusted & 0x3ffu));
    source_count = 2;
  }
  auto mapped_codepoint = [&](DWORD flags) -> uint32_t {
    wchar_t mapped[3]{};
    const int count = LCMapStringEx(
        LOCALE_NAME_INVARIANT, flags, source, source_count,
        mapped, static_cast<int>(std::size(mapped)), nullptr, nullptr, 0);
    if (count == 1) return static_cast<uint32_t>(mapped[0]);
    if (count == 2 && mapped[0] >= 0xd800 && mapped[0] <= 0xdbff &&
        mapped[1] >= 0xdc00 && mapped[1] <= 0xdfff) {
      return 0x10000u + ((static_cast<uint32_t>(mapped[0]) - 0xd800u) << 10u) +
          (static_cast<uint32_t>(mapped[1]) - 0xdc00u);
    }
    return codepoint;
  };
  const uint32_t lower = mapped_codepoint(LCMAP_LOWERCASE);
  if (lower != codepoint) return lower;
  const uint32_t upper = mapped_codepoint(LCMAP_UPPERCASE);
  if (upper != codepoint) return upper;
#else
  if ((codepoint >= 0xc0u && codepoint <= 0xd6u) ||
      (codepoint >= 0xd8u && codepoint <= 0xdeu)) return codepoint + 0x20u;
  if ((codepoint >= 0xe0u && codepoint <= 0xf6u) ||
      (codepoint >= 0xf8u && codepoint <= 0xfeu)) return codepoint - 0x20u;
#endif
  return codepoint;
}

std::vector<uint32_t> regex_unicode_case_equivalents(uint32_t codepoint) {
  std::vector<uint32_t> result;
  auto append_unique = [&](uint32_t value) {
    if (std::find(result.begin(), result.end(), value) == result.end()) result.push_back(value);
  };
  append_unique(codepoint);
  // Following both directions closes ordinary one-codepoint upper/lower pairs.
  // A few Unicode simple-fold classes contain a third member, or share a
  // multi-codepoint uppercase spelling; those classes are explicit below.
  uint32_t current = codepoint;
  for (int i = 0; i < 3; ++i) {
    current = regex_latin1_case_pair(current);
    append_unique(current);
  }
  const auto append_class = [&](std::initializer_list<uint32_t> values) {
    if (std::find(values.begin(), values.end(), codepoint) == values.end()) return;
    for (uint32_t value : values) append_unique(value);
  };
  append_class({0x004bu, 0x006bu, 0x212au});  // K, k, Kelvin sign
  append_class({0x0053u, 0x0073u, 0x017fu});  // S, s, long s
  append_class({0x0412u, 0x0432u, 0x1c80u});  // Cyrillic Ve variants
  append_class({0xfb05u, 0xfb06u});           // long-s-t and st ligatures
  return result;
}

void regex_append_unicode_case_atom(std::string& out, uint32_t codepoint) {
  const std::vector<uint32_t> equivalents = regex_unicode_case_equivalents(codepoint);
  if (equivalents.size() > 1) out += "(?:";
  for (size_t i = 0; i < equivalents.size(); ++i) {
    if (i != 0) out.push_back('|');
    regex_append_utf8_codepoint(out, equivalents[i]);
  }
  if (equivalents.size() > 1) out.push_back(')');
}

bool regex_parse_fixed_literal_lookbehind(
    std::string_view pattern,
    size_t open,
    bool& positive,
    size_t& close,
    std::string& literal,
    char& category,
    int64_t& group_ref);

bool regex_parse_conditional_literal_lookbehind(
    std::string_view pattern,
    size_t open,
    bool& positive,
    size_t& close,
    int64_t& conditional_group,
    std::string& yes_literal,
    std::string& no_literal);

bool regex_parse_conditional_literal_lookahead(
    std::string_view pattern,
    size_t open,
    bool& positive,
    size_t& close,
    int64_t& conditional_group,
    std::string& yes_literal,
    std::string& no_literal);

bool regex_parse_captured_literal_negative_lookbehind(
    std::string_view pattern,
    size_t open,
    size_t& close,
    std::string& literal);

bool regex_parse_terminal_conditional(
    std::string_view pattern,
    size_t open,
    size_t& close,
    std::string& condition,
    std::string& yes_engine,
    std::string& no_engine,
    std::string& yes_literal,
    std::string& no_literal);

bool regex_has_unsupported_std_construct(std::string_view pattern) {
  bool in_class = false;
  bool escaped = false;
  for (size_t i = 0; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '[') {
      in_class = true;
      continue;
    }
    if (ch == ']' && in_class) {
      in_class = false;
      continue;
    }
    if (!in_class && ch == '(' && i + 3 < pattern.size() && pattern[i + 1] == '?' &&
        pattern[i + 2] == '<') {
      const char lookbehind_kind = pattern[i + 3];
      bool positive = true;
      size_t close = 0;
      std::string literal;
      char category = '\0';
      int64_t group_ref = 0;
      int64_t conditional_group = 0;
      std::string conditional_yes;
      std::string conditional_no;
      std::string captured_negative_literal;
      if (lookbehind_kind == '!' &&
          !regex_parse_fixed_literal_lookbehind(
              pattern, i, positive, close, literal, category, group_ref) &&
          !regex_parse_captured_literal_negative_lookbehind(
              pattern, i, close, captured_negative_literal) &&
          !regex_parse_conditional_literal_lookbehind(
              pattern, i, positive, close, conditional_group,
              conditional_yes, conditional_no)) {
        return true;
      }
    }
  }
  return false;
}

struct ScopedInlineFlags {
  bool enable_dotall = false;
  bool disable_dotall = false;
  bool enable_ignorecase = false;
  bool disable_ignorecase = false;
  bool enable_multiline = false;
  bool disable_multiline = false;
  bool enable_verbose = false;
  bool disable_verbose = false;
  bool enable_ascii = false;
  bool enable_unicode = false;
};

bool parse_scoped_inline_flags(std::string_view pattern, size_t open, size_t& colon, ScopedInlineFlags& flags) {
  if (open + 2 >= pattern.size() || pattern[open] != '(' || pattern[open + 1] != '?') {
    return false;
  }
  bool negated = false;
  bool saw_flag = false;
  flags = {};
  for (size_t i = open + 2; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (ch == ':') {
      colon = i;
      return saw_flag;
    }
    if (ch == '-') {
      negated = true;
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(ch)) == 0) {
      return false;
    }
    saw_flag = true;
    if (ch == 's') {
      (negated ? flags.disable_dotall : flags.enable_dotall) = true;
    } else if (ch == 'i') {
      (negated ? flags.disable_ignorecase : flags.enable_ignorecase) = true;
    } else if (ch == 'm') {
      (negated ? flags.disable_multiline : flags.enable_multiline) = true;
    } else if (ch == 'x') {
      (negated ? flags.disable_verbose : flags.enable_verbose) = true;
    } else if (ch == 'a' && !negated) {
      flags.enable_ascii = true;
    } else if (ch == 'u' && !negated) {
      flags.enable_unicode = true;
    }
  }
  return false;
}

bool regex_parse_octal_escape(std::string_view pattern, size_t& index, unsigned char& out) {
  if (index >= pattern.size() || pattern[index] < '0' || pattern[index] > '7') {
    return false;
  }
  unsigned int value = 0;
  uint32_t count = 0;
  while (index < pattern.size() && count < 3 && pattern[index] >= '0' && pattern[index] <= '7') {
    value = value * 8u + static_cast<unsigned int>(pattern[index] - '0');
    ++index;
    ++count;
  }
  --index;
  out = static_cast<unsigned char>(value & 0xffu);
  return true;
}

bool regex_parse_terminal_conditional(
    std::string_view pattern,
    size_t open,
    size_t& close,
    std::string& condition,
    std::string& yes_engine,
    std::string& no_engine,
    std::string& yes_literal,
    std::string& no_literal) {
  if (open + 4 >= pattern.size() || pattern.substr(open, 3) != "(?(") return false;
  const size_t condition_close = pattern.find(')', open + 3);
  if (condition_close == std::string_view::npos || condition_close == open + 3) return false;
  condition.assign(pattern.substr(open + 3, condition_close - open - 3));
  std::string* engine = &yes_engine;
  std::string* literal = &yes_literal;
  bool escaped = false;
  for (size_t i = condition_close + 1; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      engine->push_back('\\');
      engine->push_back(ch);
      switch (ch) {
        case 'n': literal->push_back('\n'); break;
        case 'r': literal->push_back('\r'); break;
        case 't': literal->push_back('\t'); break;
        default: literal->push_back(ch); break;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '|' && engine == &yes_engine) {
      engine = &no_engine;
      literal = &no_literal;
      continue;
    }
    if (ch == ')') {
      close = i;
      for (size_t tail = close + 1; tail < pattern.size(); ++tail) {
        if (pattern[tail] != ')' && pattern[tail] != '$' &&
            !(pattern[tail] == '\\' && tail + 1 < pattern.size() &&
              (pattern[++tail] == 'z' || pattern[tail] == 'Z'))) return false;
      }
      return true;
    }
    if (std::string_view(".^$()[]{}*+?").find(ch) != std::string_view::npos) return false;
    engine->push_back(ch);
    literal->push_back(ch);
  }
  return false;
}

bool regex_prefix_is_global_flags(std::string_view pattern, size_t end) {
  size_t cursor = 0;
  while (cursor < end) {
    if (cursor + 3 > end || pattern.substr(cursor, 2) != "(?") return false;
    const size_t close = pattern.find(')', cursor + 2);
    if (close == std::string_view::npos || close >= end || close == cursor + 2) return false;
    for (size_t i = cursor + 2; i < close; ++i) {
      if (std::string_view("aiLmsux").find(pattern[i]) == std::string_view::npos) return false;
    }
    cursor = close + 1;
  }
  return cursor == end;
}

int64_t regex_capture_count_before(std::string_view pattern, size_t end) {
  int64_t count = 0;
  bool escaped = false;
  bool in_class = false;
  for (size_t i = 0; i < std::min(end, pattern.size()); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '[') {
      in_class = true;
      continue;
    }
    if (ch == ']' && in_class) {
      in_class = false;
      continue;
    }
    if (in_class || ch != '(') continue;
    if (i + 1 >= pattern.size() || pattern[i + 1] != '?' ||
        (i + 3 < pattern.size() && pattern.substr(i + 1, 3) == "?P<")) {
      ++count;
    }
  }
  return count;
}

bool regex_parse_hex_escape(
    std::string_view pattern,
    size_t& index,
    size_t digits,
    uint32_t& codepoint);
void regex_append_utf8_codepoint(std::string& out, uint32_t codepoint);

const std::string& regex_unicode_decimal_atom() {
  static const std::string atom = [] {
    static constexpr std::array<std::array<uint32_t, 2>, 71> ranges{{
        {{0x0030, 0x0039}}, {{0x0660, 0x0669}}, {{0x06f0, 0x06f9}}, {{0x07c0, 0x07c9}},
        {{0x0966, 0x096f}}, {{0x09e6, 0x09ef}}, {{0x0a66, 0x0a6f}}, {{0x0ae6, 0x0aef}},
        {{0x0b66, 0x0b6f}}, {{0x0be6, 0x0bef}}, {{0x0c66, 0x0c6f}}, {{0x0ce6, 0x0cef}},
        {{0x0d66, 0x0d6f}}, {{0x0de6, 0x0def}}, {{0x0e50, 0x0e59}}, {{0x0ed0, 0x0ed9}},
        {{0x0f20, 0x0f29}}, {{0x1040, 0x1049}}, {{0x1090, 0x1099}}, {{0x17e0, 0x17e9}},
        {{0x1810, 0x1819}}, {{0x1946, 0x194f}}, {{0x19d0, 0x19d9}}, {{0x1a80, 0x1a89}},
        {{0x1a90, 0x1a99}}, {{0x1b50, 0x1b59}}, {{0x1bb0, 0x1bb9}}, {{0x1c40, 0x1c49}},
        {{0x1c50, 0x1c59}}, {{0xa620, 0xa629}}, {{0xa8d0, 0xa8d9}}, {{0xa900, 0xa909}},
        {{0xa9d0, 0xa9d9}}, {{0xa9f0, 0xa9f9}}, {{0xaa50, 0xaa59}}, {{0xabf0, 0xabf9}},
        {{0xff10, 0xff19}}, {{0x104a0, 0x104a9}}, {{0x10d30, 0x10d39}}, {{0x10d40, 0x10d49}},
        {{0x11066, 0x1106f}}, {{0x110f0, 0x110f9}}, {{0x11136, 0x1113f}}, {{0x111d0, 0x111d9}},
        {{0x112f0, 0x112f9}}, {{0x11450, 0x11459}}, {{0x114d0, 0x114d9}}, {{0x11650, 0x11659}},
        {{0x116c0, 0x116c9}}, {{0x116d0, 0x116e3}}, {{0x11730, 0x11739}}, {{0x118e0, 0x118e9}},
        {{0x11950, 0x11959}}, {{0x11bf0, 0x11bf9}}, {{0x11c50, 0x11c59}}, {{0x11d50, 0x11d59}},
        {{0x11da0, 0x11da9}}, {{0x11f50, 0x11f59}}, {{0x16130, 0x16139}}, {{0x16a60, 0x16a69}},
        {{0x16ac0, 0x16ac9}}, {{0x16b50, 0x16b59}}, {{0x16d70, 0x16d79}}, {{0x1ccf0, 0x1ccf9}},
        {{0x1d7ce, 0x1d7ff}}, {{0x1e140, 0x1e149}}, {{0x1e2f0, 0x1e2f9}}, {{0x1e4f0, 0x1e4f9}},
        {{0x1e5f1, 0x1e5fa}}, {{0x1e950, 0x1e959}}, {{0x1fbf0, 0x1fbf9}},
    }};
    std::string result = "(?:";
    bool first = true;
    for (const auto& range : ranges) {
      for (uint32_t codepoint = range[0]; codepoint <= range[1]; ++codepoint) {
        if (!first) result.push_back('|');
        regex_append_utf8_codepoint(result, codepoint);
        first = false;
      }
    }
    result.push_back(')');
    return result;
  }();
  return atom;
}

bool regex_parse_fixed_literal_lookbehind(
    std::string_view pattern,
    size_t open,
    bool& positive,
    size_t& close,
    std::string& literal,
    char& category,
    int64_t& group_ref) {
  if (open + 3 >= pattern.size() || pattern[open] != '(' || pattern[open + 1] != '?' || pattern[open + 2] != '<') {
    return false;
  }
  if (pattern[open + 3] == '=') {
    positive = true;
  } else if (pattern[open + 3] == '!') {
    positive = false;
  } else {
    return false;
  }

  literal.clear();
  category = '\0';
  group_ref = 0;
  if (open + 6 < pattern.size() && pattern[open + 4] == '\\' &&
      (pattern[open + 5] == 'w' || pattern[open + 5] == 'W') &&
      pattern[open + 6] == ')') {
    category = pattern[open + 5];
    close = open + 6;
    return true;
  }
  if (open + 6 < pattern.size() && pattern[open + 4] == '\\' &&
      pattern[open + 5] >= '1' && pattern[open + 5] <= '9') {
    size_t digit_end = open + 6;
    group_ref = pattern[open + 5] - '0';
    if (digit_end < pattern.size() && pattern[digit_end] >= '0' && pattern[digit_end] <= '9') {
      group_ref = group_ref * 10 + (pattern[digit_end] - '0');
      ++digit_end;
    }
    if (digit_end < pattern.size() && pattern[digit_end] == ')') {
      close = digit_end;
      return true;
    }
    group_ref = 0;
  }
  bool escaped = false;
  for (size_t i = open + 4; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      switch (ch) {
        case 'n': literal.push_back('\n'); break;
        case 'r': literal.push_back('\r'); break;
        case 't': literal.push_back('\t'); break;
        default: literal.push_back(ch); break;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == ')') {
      close = i;
      return true;
    }
    if (ch == '(' || ch == '[' || ch == '{' || ch == '.' || ch == '*' || ch == '+' || ch == '?' || ch == '|' || ch == '^' ||
        ch == '$') {
      return false;
    }
    literal.push_back(ch);
  }
  return false;
}

bool regex_parse_captured_literal_negative_lookbehind(
    std::string_view pattern,
    size_t open,
    size_t& close,
    std::string& literal) {
  if (open + 7 >= pattern.size() || pattern.substr(open, 5) != "(?<!(") return false;
  literal.clear();
  bool escaped = false;
  for (size_t i = open + 5; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      switch (ch) {
        case 'n': literal.push_back('\n'); break;
        case 'r': literal.push_back('\r'); break;
        case 't': literal.push_back('\t'); break;
        default: literal.push_back(ch); break;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == ')' && i + 1 < pattern.size() && pattern[i + 1] == ')') {
      close = i + 1;
      return true;
    }
    if (std::string_view(".^$|()[]{}*+?").find(ch) != std::string_view::npos) return false;
    literal.push_back(ch);
  }
  return false;
}

bool regex_parse_conditional_literal_lookbehind(
    std::string_view pattern,
    size_t open,
    bool& positive,
    size_t& close,
    int64_t& conditional_group,
    std::string& yes_literal,
    std::string& no_literal) {
  if (open + 10 >= pattern.size() || pattern.substr(open, 3) != "(?<" ||
      (pattern[open + 3] != '=' && pattern[open + 3] != '!') ||
      pattern.substr(open + 4, 3) != "(?(") return false;
  positive = pattern[open + 3] == '=';
  size_t cursor = open + 7;
  conditional_group = 0;
  bool saw_digit = false;
  while (cursor < pattern.size() && pattern[cursor] >= '0' && pattern[cursor] <= '9') {
    saw_digit = true;
    conditional_group = conditional_group * 10 + (pattern[cursor] - '0');
    ++cursor;
  }
  if (!saw_digit || conditional_group <= 0 || cursor >= pattern.size() || pattern[cursor] != ')') {
    return false;
  }
  const size_t yes_start = ++cursor;
  const size_t separator = pattern.find('|', yes_start);
  if (separator == std::string_view::npos) return false;
  const size_t conditional_close = pattern.find(')', separator + 1);
  if (conditional_close == std::string_view::npos || conditional_close + 1 >= pattern.size() ||
      pattern[conditional_close + 1] != ')') return false;
  const auto yes = pattern.substr(yes_start, separator - yes_start);
  const auto no = pattern.substr(separator + 1, conditional_close - separator - 1);
  const auto simple = [](std::string_view value) {
    return value.size() <= 1 && (value.empty() ||
        std::string_view("\\.^$|()[]{}*+?").find(value.front()) == std::string_view::npos);
  };
  if (!simple(yes) || !simple(no)) return false;
  yes_literal.assign(yes);
  no_literal.assign(no);
  close = conditional_close + 1;
  return true;
}

bool regex_parse_conditional_literal_lookahead(
    std::string_view pattern,
    size_t open,
    bool& positive,
    size_t& close,
    int64_t& conditional_group,
    std::string& yes_literal,
    std::string& no_literal) {
  if (open + 9 >= pattern.size() || pattern.substr(open, 2) != "(?" ||
      (pattern[open + 2] != '=' && pattern[open + 2] != '!') ||
      pattern.substr(open + 3, 3) != "(?(") return false;
  positive = pattern[open + 2] == '=';
  size_t cursor = open + 6;
  conditional_group = 0;
  bool saw_digit = false;
  while (cursor < pattern.size() && pattern[cursor] >= '0' && pattern[cursor] <= '9') {
    saw_digit = true;
    conditional_group = conditional_group * 10 + (pattern[cursor] - '0');
    ++cursor;
  }
  if (!saw_digit || conditional_group <= 0 || cursor >= pattern.size() || pattern[cursor] != ')') {
    return false;
  }
  const size_t yes_start = ++cursor;
  const size_t separator = pattern.find('|', yes_start);
  if (separator == std::string_view::npos) return false;
  const size_t conditional_close = pattern.find(')', separator + 1);
  if (conditional_close == std::string_view::npos || conditional_close + 1 >= pattern.size() ||
      pattern[conditional_close + 1] != ')') return false;
  const auto yes = pattern.substr(yes_start, separator - yes_start);
  const auto no = pattern.substr(separator + 1, conditional_close - separator - 1);
  const auto simple = [](std::string_view value) {
    return value.size() <= 1 && (value.empty() ||
        std::string_view("\\.^$|()[]{}*+?").find(value.front()) == std::string_view::npos);
  };
  if (!simple(yes) || !simple(no)) return false;
  yes_literal.assign(yes);
  no_literal.assign(no);
  close = conditional_close + 1;
  return true;
}

void regex_append_literal_char(std::string& out, unsigned char value, bool in_class) {
  const char ch = static_cast<char>(value);
  const std::string_view special = in_class ? std::string_view(R"(\^-[])") : std::string_view(R"(\.^$|()[]{}*+?)");
  if (special.find(ch) != std::string_view::npos) {
    out.push_back('\\');
  }
  out.push_back(ch);
}

bool normalize_simple_escaped_literal_class(
    std::string_view pattern,
    bool utf8_pattern,
    bool unicode_ignorecase,
    std::string& out) {
  if (pattern.size() < 3 || pattern.front() != '[' || pattern.back() != ']' ||
      pattern[1] == '^') {
    return false;
  }
  size_t range_dash = std::string_view::npos;
  bool scan_escape = false;
  for (size_t i = 1; i + 1 < pattern.size(); ++i) {
    if (scan_escape) {
      scan_escape = false;
      continue;
    }
    if (pattern[i] == '\\') {
      scan_escape = true;
    } else if (pattern[i] == '-') {
      if (range_dash != std::string_view::npos) return false;
      range_dash = i;
    }
  }
  if (range_dash != std::string_view::npos &&
      (range_dash == 1 || range_dash + 2 >= pattern.size())) return false;
  bool validation_escape = false;
  bool validation_literal_slot = true;
  for (size_t i = 1; i + 1 < pattern.size(); ++i) {
    if (validation_escape) {
      validation_escape = false;
      validation_literal_slot = false;
      continue;
    }
    if (pattern[i] == '\\') {
      validation_escape = true;
      continue;
    }
    if (pattern[i] == ']' && !validation_literal_slot) {
      return false;
    }
    if (pattern[i] != '^' || !validation_literal_slot) {
      validation_literal_slot = false;
    }
  }
  if (validation_escape) return false;
  std::vector<uint32_t> literals;
  for (size_t i = 1; i + 1 < pattern.size(); ++i) {
    uint32_t codepoint = static_cast<unsigned char>(pattern[i]);
    if (pattern[i] == '\\') {
      if (++i + 1 >= pattern.size()) return false;
      const char kind = pattern[i];
      if (kind >= '0' && kind <= '7') {
        unsigned int value = 0;
        size_t count = 0;
        while (i + 1 < pattern.size() && count < 3 && pattern[i] >= '0' && pattern[i] <= '7') {
          value = value * 8u + static_cast<unsigned int>(pattern[i] - '0');
          ++i;
          ++count;
        }
        --i;
        codepoint = value;
      } else if (kind == 'x') {
        if (!regex_parse_hex_escape(pattern, i, 2, codepoint)) return false;
      } else if (kind == 'u') {
        if (!utf8_pattern || !regex_parse_hex_escape(pattern, i, 4, codepoint)) return false;
      } else if (kind == 'U') {
        if (!utf8_pattern || !regex_parse_hex_escape(pattern, i, 8, codepoint)) return false;
      } else {
        switch (kind) {
          case 'a': codepoint = '\a'; break;
          case 'b': codepoint = '\b'; break;
          case 'f': codepoint = '\f'; break;
          case 'n': codepoint = '\n'; break;
          case 'r': codepoint = '\r'; break;
          case 't': codepoint = '\t'; break;
          case 'v': codepoint = '\v'; break;
          case '\\': case ']': case '[': case '^': codepoint = static_cast<unsigned char>(kind); break;
          default: return false;
        }
      }
    } else if (static_cast<unsigned char>(pattern[i]) >= 0x80u) {
      if (!utf8_pattern) return false;
      const size_t width = utf8_codepoint_width(static_cast<unsigned char>(pattern[i]));
      if (width <= 1 || i + width >= pattern.size()) return false;
      codepoint = static_cast<unsigned char>(pattern[i]) &
          ((1u << (7u - static_cast<uint32_t>(width))) - 1u);
      for (size_t j = 1; j < width; ++j) {
        const unsigned char continuation = static_cast<unsigned char>(pattern[i + j]);
        if ((continuation & 0xc0u) != 0x80u) return false;
        codepoint = (codepoint << 6u) | (continuation & 0x3fu);
      }
      i += width - 1;
    }
    if ((!utf8_pattern && codepoint > 0xffu) || codepoint > 0x10ffffu) return false;
    literals.push_back(codepoint);
  }
  if (literals.empty()) return false;
  if (range_dash != std::string_view::npos) {
    if (literals.size() != 3 || literals[1] != static_cast<uint32_t>('-') ||
        literals[0] > literals[2]) return false;
    if (literals[2] - literals[0] > 4096u) {
      if (!utf8_pattern || unicode_ignorecase) return false;
      std::vector<uint32_t> ascii_extras;
      for (uint32_t codepoint = 'A'; codepoint <= 'Z'; ++codepoint) {
        const uint32_t lower = codepoint + ('a' - 'A');
        if (codepoint < literals[0] && lower >= literals[0] && lower <= literals[2]) {
          ascii_extras.push_back(codepoint);
        }
      }
      if (ascii_extras.empty()) return false;
      out += "(?:";
      for (uint32_t codepoint : ascii_extras) {
        regex_append_literal_char(out, static_cast<unsigned char>(codepoint), false);
        out.push_back('|');
      }
      out.push_back('[');
      if (literals[0] <= 0x7fu) {
        regex_append_literal_char(out, static_cast<unsigned char>(literals[0]), true);
      } else {
        regex_append_utf8_codepoint(out, literals[0]);
      }
      out.push_back('-');
      if (literals[2] <= 0x7fu) {
        regex_append_literal_char(out, static_cast<unsigned char>(literals[2]), true);
      } else {
        regex_append_utf8_codepoint(out, literals[2]);
      }
      out += "])";
      return true;
    }
    std::vector<uint32_t> expanded;
    expanded.reserve(static_cast<size_t>(literals[2] - literals[0] + 1u));
    for (uint32_t codepoint = literals[0]; codepoint <= literals[2]; ++codepoint) {
      expanded.push_back(codepoint);
    }
    literals = std::move(expanded);
  }
  if (unicode_ignorecase) {
    std::vector<uint32_t> expanded;
    for (uint32_t codepoint : literals) {
      for (uint32_t equivalent : regex_unicode_case_equivalents(codepoint)) {
        if (std::find(expanded.begin(), expanded.end(), equivalent) == expanded.end()) {
          expanded.push_back(equivalent);
        }
      }
    }
    literals = std::move(expanded);
  }
  if (literals.size() > 1) out += "(?:";
  for (size_t i = 0; i < literals.size(); ++i) {
    if (i != 0) out.push_back('|');
    if (literals[i] <= 0x7fu || (!utf8_pattern && literals[i] <= 0xffu)) {
      regex_append_literal_char(out, static_cast<unsigned char>(literals[i]), false);
    } else {
      regex_append_utf8_codepoint(out, literals[i]);
    }
  }
  if (literals.size() > 1) out.push_back(')');
  return true;
}

bool regex_append_unescaped_literal(std::string_view text, std::string& out) {
  bool escaped = false;
  for (const char ch : text) {
    if (escaped) {
      switch (ch) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 's':
        case 'S':
        case 'd':
        case 'D':
        case 'w':
        case 'W':
        case 'b':
        case 'B':
          return false;
        default: out.push_back(ch); break;
      }
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (std::string_view(".^$|()[]{}*+?").find(ch) != std::string_view::npos) {
      return false;
    }
    out.push_back(ch);
  }
  return !escaped;
}

bool regex_simple_atom_matches_char(std::string_view atom, char following) {
  while (atom.size() >= 3 && atom.substr(0, 3) == "?:") atom.remove_prefix(2);
  while (atom.size() >= 2 && atom.front() == '(' && atom.back() == ')') {
    atom.remove_prefix(1);
    atom.remove_suffix(1);
    if (atom.substr(0, 2) == "?:") atom.remove_prefix(2);
  }
  if (atom.size() != 1) return false;
  return atom.front() == '.' || following == '.' || atom.front() == following;
}

bool regex_atomic_commit_rejects(
    std::string_view pattern,
    std::string_view text,
    size_t match_start,
    size_t match_end) {
  for (size_t open = pattern.find("(?>"); open != std::string_view::npos;
       open = pattern.find("(?>", open + 3)) {
    size_t depth = 1;
    size_t close = open + 3;
    bool escaped = false;
    for (; close < pattern.size(); ++close) {
      const char ch = pattern[close];
      if (escaped) { escaped = false; continue; }
      if (ch == '\\') { escaped = true; continue; }
      if (ch == '(') ++depth;
      if (ch == ')' && --depth == 0) break;
    }
    if (close >= pattern.size()) continue;
    const auto body = pattern.substr(open + 3, close - open - 3);
    auto suffix = pattern.substr(close + 1);
    if (suffix.substr(0, 2) == "++") suffix.remove_prefix(2);
    const size_t separator = body.find('|');
    if (separator != std::string_view::npos && body.find('|', separator + 1) == std::string_view::npos) {
      std::string prefix_literal;
      std::string first_literal;
      std::string suffix_literal;
      auto prefix = pattern.substr(0, open);
      if (!prefix.empty() && prefix.front() == '^') prefix.remove_prefix(1);
      if (regex_append_unescaped_literal(prefix, prefix_literal) &&
          regex_append_unescaped_literal(body.substr(0, separator), first_literal) &&
          regex_append_unescaped_literal(suffix, suffix_literal)) {
        const std::string committed = prefix_literal + first_literal;
        if (match_start + committed.size() <= text.size() &&
            text.compare(match_start, committed.size(), committed) == 0) {
          const size_t suffix_start = match_start + committed.size();
          if (suffix_start + suffix_literal.size() > text.size() ||
              text.compare(suffix_start, suffix_literal.size(), suffix_literal) != 0) return true;
        }
      }
    }
    if (body == ".*" && !suffix.empty() && suffix.front() == '.' && match_end > match_start) return true;
    std::string_view body_atom = body;
    if (body_atom.size() >= 2 && body_atom.substr(body_atom.size() - 2) == "++") {
      body_atom.remove_suffix(2);
    }
    if (!suffix.empty() && regex_simple_atom_matches_char(body_atom, suffix.front())) return true;
  }

  for (size_t quantifier = 0; quantifier + 2 < pattern.size(); ++quantifier) {
    const char quantifier_ch = pattern[quantifier];
    bool possessive = (quantifier_ch == '*' || quantifier_ch == '+' || quantifier_ch == '?') &&
        pattern[quantifier + 1] == '+';
    size_t modifier_end = quantifier + 2;
    size_t atom_end = quantifier;
    if (!possessive && quantifier_ch == '}') {
      const size_t open_brace = pattern.rfind('{', quantifier);
      possessive = open_brace != std::string_view::npos && pattern[quantifier + 1] == '+' &&
          regex_brace_starts_repeat(pattern, open_brace);
      atom_end = open_brace;
    }
    if (!possessive || modifier_end >= pattern.size()) continue;
    const char following = pattern[modifier_end];
    if (following == ')' || following == '$' || following == '|') continue;
    std::string_view atom;
    if (atom_end > 0 && pattern[atom_end - 1] == ')') {
      size_t open_group = atom_end - 1;
      size_t depth = 1;
      while (open_group > 0) {
        --open_group;
        if (pattern[open_group] == ')') ++depth;
        if (pattern[open_group] == '(' && --depth == 0) break;
      }
      if (pattern[open_group] == '(') atom = pattern.substr(open_group, atom_end - open_group);
    } else if (atom_end > 0) {
      atom = pattern.substr(atom_end - 1, 1);
    }
    if (!atom.empty() && regex_simple_atom_matches_char(atom, following)) return true;
  }
  return false;
}

FastRegexKind detect_fast_regex(std::string_view engine_pattern, std::string& literal) {
  literal.clear();
  if (engine_pattern.empty()) {
    return FastRegexKind::None;
  }

  constexpr std::string_view dotall_prefix = "([\\s\\S]*";
  if (engine_pattern.size() >= dotall_prefix.size() + 2 &&
      engine_pattern.substr(0, dotall_prefix.size()) == dotall_prefix &&
      engine_pattern.back() == '$') {
    const std::string_view body = engine_pattern.substr(dotall_prefix.size(), engine_pattern.size() - dotall_prefix.size() - 1);
    if (!body.empty() && body.back() == ')') {
      if (regex_append_unescaped_literal(body.substr(0, body.size() - 1), literal)) {
        return FastRegexKind::AnySuffix;
      }
    }
  }

  constexpr std::string_view any_prefix = ".*";
  if (engine_pattern.size() >= any_prefix.size() + 1 &&
      engine_pattern.substr(0, any_prefix.size()) == any_prefix &&
      engine_pattern.back() == '$') {
    if (regex_append_unescaped_literal(engine_pattern.substr(any_prefix.size(), engine_pattern.size() - any_prefix.size() - 1), literal)) {
      return FastRegexKind::AnySuffix;
    }
  }

  if (regex_append_unescaped_literal(engine_pattern, literal)) {
    return FastRegexKind::LiteralPrefix;
  }
  literal.clear();
  return FastRegexKind::None;
}

std::string normalize_std_regex_pattern(
    std::string_view pattern,
    const std::unordered_map<std::string, int64_t>* group_names = nullptr,
    std::vector<LookbehindAssertion>* lookbehinds = nullptr,
    bool global_dotall = false,
    bool global_ignorecase = false,
    bool global_verbose = false,
    bool global_multiline = false,
    bool utf8_pattern = true,
    bool unicode_categories = true,
    bool* requires_absolute_start = nullptr,
    bool* requires_absolute_end = nullptr,
    std::vector<BoundaryAssertion>* boundaries = nullptr) {
  std::string out;
  out.reserve(pattern.size());
  if (normalize_simple_escaped_literal_class(
          pattern, utf8_pattern, global_ignorecase && unicode_categories, out)) {
    return out;
  }
  bool in_class = false;
  bool class_literal_slot = false;
  bool escaped = false;
  std::vector<std::array<uint32_t, 5>> group_flag_stack;
  uint32_t dotall_depth = global_dotall ? 1u : 0u;
  uint32_t ignorecase_depth = global_ignorecase ? 1u : 0u;
  uint32_t verbose_depth = global_verbose ? 1u : 0u;
  uint32_t multiline_depth = global_multiline ? 1u : 0u;
  uint32_t unicode_category_depth = unicode_categories ? 1u : 0u;
  for (size_t i = 0; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (!escaped && !in_class && ignorecase_depth != 0 && unicode_categories &&
        static_cast<unsigned char>(ch) >= 0xc2u) {
      const size_t width = utf8_codepoint_width(static_cast<unsigned char>(ch));
      if (width > 1 && i + width <= pattern.size()) {
        uint32_t codepoint = static_cast<unsigned char>(ch) &
            ((1u << (7u - static_cast<uint32_t>(width))) - 1u);
        for (size_t j = 1; j < width; ++j) {
          codepoint = (codepoint << 6u) | (static_cast<unsigned char>(pattern[i + j]) & 0x3fu);
        }
        const std::vector<uint32_t> equivalents = regex_unicode_case_equivalents(codepoint);
        if (equivalents.size() > 1) {
          regex_append_unicode_case_atom(out, codepoint);
          i += width - 1;
          continue;
        }
      }
    }
    if (escaped) {
      unsigned char octal = 0;
      uint32_t codepoint = 0;
      const bool starts_octal = ch == '0' || (in_class && ch >= '1' && ch <= '7') ||
          (ch >= '1' && ch <= '7' && i + 2 < pattern.size() &&
           pattern[i + 1] >= '0' && pattern[i + 1] <= '7' &&
           pattern[i + 2] >= '0' && pattern[i + 2] <= '7');
      if (starts_octal && regex_parse_octal_escape(pattern, i, octal)) {
        if (utf8_pattern && octal > 0x7fu) {
          if (!in_class) out += "(?:";
          regex_append_utf8_codepoint(out, octal);
          if (!in_class) out.push_back(')');
        } else {
          regex_append_literal_char(out, octal, in_class);
        }
      } else if (ch == 'N' && i + 1 < pattern.size() && pattern[i + 1] == '{') {
        const size_t name_start = i + 2;
        const size_t close = pattern.find('}', name_start);
        uint32_t named_codepoint = 0;
        if (close == std::string_view::npos ||
            !unicodedata_lookup_codepoint(pattern.substr(name_start, close - name_start), named_codepoint)) {
          out += "\\N";
        } else {
          const bool single_byte = named_codepoint <= 0x7fu || (!utf8_pattern && named_codepoint <= 0xffu);
          if (!in_class && !single_byte) out += "(?:";
          if (single_byte) {
            regex_append_literal_char(out, static_cast<unsigned char>(named_codepoint), in_class);
          } else {
            regex_append_utf8_codepoint(out, named_codepoint);
          }
          if (!in_class && !single_byte) out.push_back(')');
          i = close;
        }
      } else if (!in_class && ch >= '1' && ch <= '9' &&
                 i + 1 < pattern.size() &&
                 pattern[i + 1] >= '0' && pattern[i + 1] <= '9') {
        // CPython consumes at most two decimal digits for a non-octal
        // backreference. Separate a following digit so ECMAScript does not
        // greedily fold it into the reference number.
        out.push_back('\\');
        out.push_back(ch);
        out.push_back(pattern[++i]);
        out += "(?:)";
      } else if ((ch == 'x' && regex_parse_hex_escape(pattern, i, 2, codepoint)) ||
                 (ch == 'u' && regex_parse_hex_escape(pattern, i, 4, codepoint)) ||
                 (ch == 'U' && regex_parse_hex_escape(pattern, i, 8, codepoint))) {
        const bool single_byte = codepoint <= 0x7fu || (!utf8_pattern && codepoint <= 0xffu);
        if (!in_class && ignorecase_depth != 0 && unicode_categories) {
          regex_append_unicode_case_atom(out, codepoint);
        } else if (single_byte) {
          regex_append_literal_char(out, static_cast<unsigned char>(codepoint), in_class);
        } else {
          if (!in_class) out += "(?:";
          regex_append_utf8_codepoint(out, codepoint);
          if (!in_class) out.push_back(')');
        }
      } else if (ch == 'A') {
        if (requires_absolute_start != nullptr) *requires_absolute_start = true;
        out.push_back('^');
      } else if (ch == 'z' || ch == 'Z') {
        out += "(?![\\s\\S])";
      } else if (ch == 'a' || ch == 'f' || ch == 'n' || ch == 'r' || ch == 't' || ch == 'v' || (ch == 'b' && in_class)) {
        unsigned char control = 0;
        switch (ch) {
          case 'a': control = '\a'; break;
          case 'b': control = '\b'; break;
          case 'f': control = '\f'; break;
          case 'n': control = '\n'; break;
          case 'r': control = '\r'; break;
          case 't': control = '\t'; break;
          case 'v': control = '\v'; break;
        }
        regex_append_literal_char(out, control, in_class);
      } else if (ch == 's' && !in_class) {
        out += "[ \\t\\n\\r\\f\\v]";
      } else if (ch == 'S' && !in_class) {
        out += "[^ \\t\\n\\r\\f\\v]";
      } else if (ch == 'd' && !in_class && unicode_category_depth != 0) {
        out += regex_unicode_decimal_atom();
      } else if (ch == 'w' && !in_class && unicode_category_depth != 0) {
        out += regex_unicode_latin1_word_atom();
      } else if (ch == 'W' && !in_class && utf8_pattern && unicode_category_depth == 0) {
        out += regex_unicode_ascii_nonword_atom();
      } else if ((ch == 'b' || ch == 'B') && !in_class && unicode_categories &&
                 boundaries != nullptr &&
                 (i == 1 || regex_prefix_is_global_flags(pattern, i - 1) ||
                  i + 1 == pattern.size())) {
        const bool at_end = i + 1 == pattern.size();
        const bool empty_only = (!at_end && i + 1 < pattern.size() && pattern[i + 1] == '|') ||
            (at_end && i >= 2 && pattern[i - 2] == '|');
        boundaries->push_back(BoundaryAssertion{at_end, ch == 'b', empty_only});
      } else {
        out.push_back('\\');
        out.push_back(ch);
      }
      escaped = false;
      if (in_class) {
        class_literal_slot = false;
      }
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (!in_class && verbose_depth != 0 && ch == '#') {
      while (i + 1 < pattern.size() && pattern[i + 1] != '\n') ++i;
      continue;
    }
    if (!in_class && verbose_depth != 0 && std::isspace(static_cast<unsigned char>(ch)) != 0) {
      continue;
    }
    if (!in_class && unicode_category_depth != 0 && pattern.substr(i, 7) == "[^\\d\\W]") {
      out += regex_unicode_latin1_letter_atom();
      i += 6;
      continue;
    }
    if (ch == ']' && in_class && class_literal_slot) {
      out.push_back('\\');
      out.push_back(']');
      class_literal_slot = false;
      continue;
    }
    if (ch == ']' && in_class) {
      in_class = false;
      out.push_back(ch);
      continue;
    }
    if (ch == ']' && !in_class) {
      out += "\\]";
      continue;
    }
    if (in_class) {
      if (ch == '[') {
        out.push_back('\\');
        out.push_back('[');
        class_literal_slot = false;
        continue;
      }
      out.push_back(ch);
      if (ch != '^') {
        class_literal_slot = false;
      }
      continue;
    }
    if (ch == '[') {
      bool class_escape = false;
      bool first_literal_slot = true;
      size_t close = std::string_view::npos;
      for (size_t candidate = i + 1; candidate < pattern.size(); ++candidate) {
        const char candidate_ch = pattern[candidate];
        if (class_escape) {
          class_escape = false;
          first_literal_slot = false;
          continue;
        }
        if (candidate_ch == '\\') {
          class_escape = true;
          continue;
        }
        if (candidate_ch == ']' && !first_literal_slot) {
          close = candidate;
          break;
        }
        if (candidate_ch != '^' || !first_literal_slot) {
          first_literal_slot = false;
        }
      }
      if (close != std::string_view::npos) {
        std::string singleton;
        const auto candidate = pattern.substr(i, close - i + 1);
        if (normalize_simple_escaped_literal_class(
                candidate, utf8_pattern, ignorecase_depth != 0 && unicode_categories, singleton) &&
            singleton.rfind("(?:", 0) != 0) {
          out += singleton;
          i = close;
          continue;
        }
      }
    }
    if (ch == '[' && i + 2 < pattern.size() && pattern[i + 1] == ']' && pattern[i + 2] == ']') {
      out += "\\]";
      i += 2;
      continue;
    }
    if (ch == '[' && i + 3 < pattern.size() && pattern[i + 1] == '\\' &&
        pattern[i + 2] == ']' && pattern[i + 3] == ']') {
      out += "\\]";
      i += 3;
      continue;
    }
    if (ch == '[' && !utf8_pattern && i + 2 < pattern.size() &&
        static_cast<unsigned char>(pattern[i + 1]) >= 0x80u && pattern[i + 2] == ']') {
      out += "(?:";
      regex_append_literal_char(out, static_cast<unsigned char>(pattern[i + 1]), false);
      out.push_back(')');
      i += 2;
      continue;
    }
    if (ch == '[' && utf8_pattern && i + 2 < pattern.size() &&
        static_cast<unsigned char>(pattern[i + 1]) >= 0x80u) {
      const unsigned char lead = static_cast<unsigned char>(pattern[i + 1]);
      const size_t width = (lead & 0xe0u) == 0xc0u ? 2u :
                           (lead & 0xf0u) == 0xe0u ? 3u :
                           (lead & 0xf8u) == 0xf0u ? 4u : 0u;
      if (width != 0 && i + width + 1 < pattern.size() && pattern[i + width + 1] == ']') {
        bool continuation = true;
        for (size_t j = 2; j <= width; ++j) {
          continuation = continuation &&
              (static_cast<unsigned char>(pattern[i + j]) & 0xc0u) == 0x80u;
        }
        if (continuation) {
          out += "(?:";
          out.append(pattern.substr(i + 1, width));
          out.push_back(')');
          i += width + 1;
          continue;
        }
      }
    }
    if (ch == '[') {
      in_class = true;
      class_literal_slot = true;
      out.push_back(ch);
      continue;
    }
    if (utf8_pattern && static_cast<unsigned char>(ch) >= 0x80u) {
      const unsigned char lead = static_cast<unsigned char>(ch);
      const size_t width = (lead & 0xe0u) == 0xc0u ? 2u :
                           (lead & 0xf0u) == 0xe0u ? 3u :
                           (lead & 0xf8u) == 0xf0u ? 4u : 0u;
      if (width != 0 && i + width <= pattern.size()) {
        bool continuation = true;
        for (size_t j = 1; j < width; ++j) {
          continuation = continuation &&
              (static_cast<unsigned char>(pattern[i + j]) & 0xc0u) == 0x80u;
        }
        if (continuation) {
          out += "(?:";
          out.append(pattern.substr(i, width));
          out.push_back(')');
          i += width - 1;
          continue;
        }
      }
    }
    if (!in_class && ch == '{') {
      if (regex_brace_starts_repeat(pattern, i)) {
        size_t close = i + 1;
        while (close < pattern.size() && pattern[close] != '}') {
          ++close;
        }
        if (i + 1 < pattern.size() && pattern[i + 1] == ',') {
          // Python accepts an omitted minimum; ECMAScript requires it.
          out += "{0";
          out.append(pattern.substr(i + 1, close - i));
        } else {
          out.append(pattern.substr(i, close - i + 1));
        }
        i = close;
      } else {
        out.push_back('\\');
        out.push_back('{');
      }
      continue;
    }
    if (!in_class && ch == '}') {
      out.push_back('\\');
      out.push_back('}');
      continue;
    }
    if (!in_class && ch == '+' && i > 0) {
      const char previous = pattern[i - 1];
      bool possessive_suffix = previous == '*' || previous == '+' || previous == '?';
      if (!possessive_suffix && previous == '}') {
        const size_t open_brace = pattern.rfind('{', i - 1);
        possessive_suffix = open_brace != std::string_view::npos &&
            regex_brace_starts_repeat(pattern, open_brace);
      }
      if (possessive_suffix) continue;
    }
    if (!in_class && ch == '.') {
      if (utf8_pattern) {
        out += regex_unicode_codepoint_atom(dotall_depth != 0);
      } else if (dotall_depth != 0) {
        out += "[\\s\\S]";
      } else {
        out += "[^\\n]";
      }
      continue;
    }
    if (!in_class && ch == '$' && multiline_depth == 0) {
      out += "(?=\\n?$)";
      continue;
    }
    if (!in_class && ignorecase_depth != 0 && std::isalpha(static_cast<unsigned char>(ch)) != 0) {
      if (unicode_categories) {
        regex_append_unicode_case_atom(out, static_cast<unsigned char>(ch));
      } else {
        out.push_back('[');
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
        out.push_back(']');
      }
      continue;
    }
    if (!in_class && ch == ')') {
      if (!group_flag_stack.empty()) {
        dotall_depth = group_flag_stack.back()[0];
        ignorecase_depth = group_flag_stack.back()[1];
        verbose_depth = group_flag_stack.back()[2];
        multiline_depth = group_flag_stack.back()[3];
        unicode_category_depth = group_flag_stack.back()[4];
        group_flag_stack.pop_back();
      }
      out.push_back(ch);
      continue;
    }
    if (!in_class && pattern[i] == '(' && i + 2 < pattern.size() &&
        pattern[i + 1] == '?' && pattern[i + 2] == '>') {
      out += "(?:";
      group_flag_stack.push_back({dotall_depth, ignorecase_depth, verbose_depth, multiline_depth,
                                  unicode_category_depth});
      i += 2;
      continue;
    }
    if (!in_class && pattern[i] == '(' && i + 2 < pattern.size() &&
        pattern[i + 1] == '?' && pattern[i + 2] == '(') {
      size_t close = 0;
      std::string condition;
      std::string yes_engine;
      std::string no_engine;
      std::string yes_literal;
      std::string no_literal;
      if (regex_parse_terminal_conditional(
              pattern, i, close, condition, yes_engine, no_engine,
              yes_literal, no_literal)) {
        int64_t conditional_group = 0;
        const bool numeric = !condition.empty() && std::all_of(
            condition.begin(), condition.end(), [](char value) { return value >= '0' && value <= '9'; });
        if (numeric) {
          for (char value : condition) conditional_group = conditional_group * 10 + (value - '0');
        } else if (group_names != nullptr) {
          const auto found = group_names->find(condition);
          if (found != group_names->end()) conditional_group = found->second;
        }
        if (conditional_group > 0) {
          out += "(?:";
          out += yes_engine;
          out.push_back('|');
          out += no_engine;
          out.push_back(')');
          if (lookbehinds != nullptr) {
            LookbehindAssertion assertion;
            assertion.conditional_group = conditional_group;
            assertion.conditional_yes = std::move(yes_literal);
            assertion.conditional_no = std::move(no_literal);
            assertion.captures_before_assertion = regex_capture_count_before(pattern, i);
            assertion.terminal_consuming = true;
            lookbehinds->push_back(std::move(assertion));
          }
          i = close;
          continue;
        }
      }
    }
    if (!in_class && pattern[i] == '(' && i + 2 < pattern.size() &&
        pattern[i + 1] == '?' && (pattern[i + 2] == '=' || pattern[i + 2] == '!')) {
      bool positive = true;
      size_t close = 0;
      int64_t conditional_group = 0;
      std::string conditional_yes;
      std::string conditional_no;
      if (regex_parse_conditional_literal_lookahead(
              pattern, i, positive, close, conditional_group,
              conditional_yes, conditional_no)) {
        if (lookbehinds != nullptr) {
          LookbehindAssertion assertion;
          assertion.engine_offset = out.size();
          assertion.positive = positive;
          assertion.conditional_group = conditional_group;
          assertion.conditional_yes = std::move(conditional_yes);
          assertion.conditional_no = std::move(conditional_no);
          assertion.forward = true;
          assertion.captures_before_assertion = regex_capture_count_before(pattern, i);
          std::string trailing_literal;
          assertion.anchor_from_end = close + 1 < pattern.size() &&
              regex_append_unescaped_literal(pattern.substr(close + 1), trailing_literal);
          assertion.trailing_width = trailing_literal.size();
          const auto trailing = pattern.substr(close + 1);
          if (!assertion.anchor_from_end && trailing.size() == 3 &&
              trailing.front() == '(' && trailing.back() == ')' &&
              std::string_view("\\.^$|()[]{}*+?").find(trailing[1]) == std::string_view::npos) {
            assertion.anchor_from_end = true;
            assertion.trailing_width = 1;
          }
          lookbehinds->push_back(std::move(assertion));
        }
        i = close;
        continue;
      }
    }
    if (!in_class && pattern[i] == '(' && i + 3 < pattern.size() && pattern[i + 1] == '?' && pattern[i + 2] == '<' &&
        (pattern[i + 3] == '=' || pattern[i + 3] == '!')) {
      bool positive = true;
      size_t close = 0;
      std::string literal;
      char category = '\0';
      int64_t group_ref = 0;
      int64_t conditional_group = 0;
      std::string conditional_yes;
      std::string conditional_no;
      std::string captured_negative_literal;
      if (regex_parse_fixed_literal_lookbehind(
              pattern, i, positive, close, literal, category, group_ref)) {
        if (lookbehinds != nullptr) {
          const size_t branch_end = pattern.find('|', close + 1);
          const bool empty_only = branch_end != std::string_view::npos &&
              (pattern.substr(close + 1, branch_end - close - 1) == "(?=\\w)" ||
               pattern.substr(close + 1, branch_end - close - 1) == "(?!\\w)");
          std::string trailing_literal;
          const bool anchor_from_end = close + 1 < pattern.size() &&
              regex_append_unescaped_literal(pattern.substr(close + 1), trailing_literal);
          lookbehinds->push_back(LookbehindAssertion{
              out.size(), std::move(literal), positive, category, empty_only,
              group_ref, anchor_from_end, trailing_literal.size()});
        }
        i = close;
      } else if (regex_parse_captured_literal_negative_lookbehind(
                     pattern, i, close, captured_negative_literal)) {
        if (lookbehinds != nullptr) {
          std::string trailing_literal;
          const bool anchor_from_end = close + 1 < pattern.size() &&
              regex_append_unescaped_literal(pattern.substr(close + 1), trailing_literal);
          lookbehinds->push_back(LookbehindAssertion{
              out.size(), std::move(captured_negative_literal), false, '\0', false,
              0, anchor_from_end, trailing_literal.size()});
        }
        // Preserve the capture slot from inside the successful negative
        // assertion while making it impossible for the host matcher to set it.
        out += "((?!))?";
        i = close;
      } else if (regex_parse_conditional_literal_lookbehind(
                     pattern, i, positive, close, conditional_group,
                     conditional_yes, conditional_no)) {
        if (lookbehinds != nullptr) {
          LookbehindAssertion assertion;
          assertion.engine_offset = out.size();
          assertion.positive = positive;
          assertion.conditional_group = conditional_group;
          assertion.conditional_yes = std::move(conditional_yes);
          assertion.conditional_no = std::move(conditional_no);
          assertion.captures_before_assertion = regex_capture_count_before(pattern, i);
          std::string trailing_literal;
          assertion.anchor_from_end = close + 1 < pattern.size() &&
              regex_append_unescaped_literal(pattern.substr(close + 1), trailing_literal);
          assertion.trailing_width = trailing_literal.size();
          const auto trailing = pattern.substr(close + 1);
          if (!assertion.anchor_from_end && trailing.size() == 3 &&
              trailing.front() == '(' && trailing.back() == ')' &&
              std::string_view("\\.^$|()[]{}*+?").find(trailing[1]) == std::string_view::npos) {
            assertion.anchor_from_end = true;
            assertion.trailing_width = 1;
          }
          lookbehinds->push_back(std::move(assertion));
        }
        i = close;
      } else {
        size_t depth = 1;
        const size_t body_start = i + 4;
        size_t body_end = body_start;
        bool lookbehind_escaped = false;
        for (; body_end < pattern.size(); ++body_end) {
          const char lookbehind_ch = pattern[body_end];
          if (lookbehind_escaped) {
            lookbehind_escaped = false;
            continue;
          }
          if (lookbehind_ch == '\\') {
            lookbehind_escaped = true;
            continue;
          }
          if (lookbehind_ch == '(') {
            ++depth;
          } else if (lookbehind_ch == ')') {
            --depth;
            if (depth == 0) {
              break;
            }
          }
        }
        if (body_end < pattern.size() &&
            regex_capture_count_before(
                pattern.substr(body_start, body_end - body_start), body_end - body_start) == 0) {
          if (lookbehinds != nullptr) {
            LookbehindAssertion assertion;
            assertion.positive = pattern[i + 3] == '=';
            assertion.captures_before_assertion = regex_capture_count_before(pattern, i);
            int64_t markers_before = 0;
            for (const auto& previous : *lookbehinds) {
              if (previous.marker_group > 0) ++markers_before;
            }
            assertion.marker_group = assertion.captures_before_assertion + markers_before + 1;
            assertion.expression = normalize_std_regex_pattern(
                pattern.substr(body_start, body_end - body_start), group_names, nullptr,
                dotall_depth != 0, ignorecase_depth != 0, verbose_depth != 0,
                multiline_depth != 0, utf8_pattern, unicode_category_depth != 0);
            size_t alternative = body_end + 1;
            while (alternative < pattern.size() &&
                   std::isspace(static_cast<unsigned char>(pattern[alternative])) != 0) {
              ++alternative;
            }
            if (alternative < pattern.size() && pattern[alternative] == '|') {
              do {
                ++alternative;
              } while (alternative < pattern.size() &&
                       std::isspace(static_cast<unsigned char>(pattern[alternative])) != 0);
              if (alternative + 4 <= pattern.size() && pattern.substr(alternative, 4) == "(?<=") {
                assertion.alternate_marker_group = assertion.marker_group + 1;
              }
            }
            lookbehinds->push_back(std::move(assertion));
            out += "()";
          }
          i = body_end;
        } else {
          i = body_end;
        }
      }
      continue;
    }
    if (!in_class && ch == '(') {
      if (i + 2 < pattern.size() && pattern[i + 1] == '?' && pattern[i + 2] == '#') {
        const size_t close = pattern.find(')', i + 3);
        if (close != std::string_view::npos) {
          i = close;
          continue;
        }
      }
      if (i + 3 < pattern.size() && pattern[i + 1] == '?') {
        const size_t close = pattern.find(')', i + 2);
        bool global_flags = close != std::string_view::npos && close > i + 2;
        for (size_t j = i + 2; global_flags && j < close; ++j) {
          global_flags = std::string_view("aiLmsux").find(pattern[j]) != std::string_view::npos;
        }
        if (global_flags) {
          i = close;
          continue;
        }
      }
      if (i + 3 < pattern.size() && pattern[i + 1] == '?' && pattern[i + 2] == 'P' && pattern[i + 3] == '<') {
        size_t name_end = i + 4;
        while (name_end < pattern.size() && pattern[name_end] != '>') {
          ++name_end;
        }
        if (name_end < pattern.size()) {
          out.push_back('(');
          group_flag_stack.push_back({dotall_depth, ignorecase_depth, verbose_depth, multiline_depth,
                                      unicode_category_depth});
          i = name_end;
          continue;
        }
      }
      if (i + 3 < pattern.size() && pattern[i + 1] == '?' && pattern[i + 2] == 'P' && pattern[i + 3] == '=') {
        size_t name_end = i + 4;
        while (name_end < pattern.size() && pattern[name_end] != ')') {
          ++name_end;
        }
        if (name_end < pattern.size()) {
          const std::string name(pattern.substr(i + 4, name_end - (i + 4)));
          auto it = group_names != nullptr ? group_names->find(name) : std::unordered_map<std::string, int64_t>::const_iterator{};
          if (group_names != nullptr && it != group_names->end() && it->second > 0) {
            out.push_back('\\');
            out.append(std::to_string(it->second));
            i = name_end;
            continue;
          }
        }
      }
      size_t colon = 0;
      ScopedInlineFlags scoped_flags;
      if (parse_scoped_inline_flags(pattern, i, colon, scoped_flags)) {
        out.append("(?:");
        group_flag_stack.push_back({dotall_depth, ignorecase_depth, verbose_depth, multiline_depth,
                                    unicode_category_depth});
        if (scoped_flags.enable_dotall) dotall_depth = 1;
        if (scoped_flags.disable_dotall) dotall_depth = 0;
        if (scoped_flags.enable_ignorecase) ignorecase_depth = 1;
        if (scoped_flags.disable_ignorecase) ignorecase_depth = 0;
        if (scoped_flags.enable_multiline) multiline_depth = 1;
        if (scoped_flags.disable_multiline) multiline_depth = 0;
        if (scoped_flags.enable_verbose) verbose_depth = 1;
        if (scoped_flags.disable_verbose) verbose_depth = 0;
        if (scoped_flags.enable_ascii) unicode_category_depth = 0;
        if (scoped_flags.enable_unicode) unicode_category_depth = 1;
        i = colon;
        continue;
      }
      group_flag_stack.push_back({dotall_depth, ignorecase_depth, verbose_depth, multiline_depth,
                                  unicode_category_depth});
    }
    out.push_back(pattern[i]);
  }
  if (escaped) {
    out.push_back('\\');
  }
  return out;
}

bool detect_fast_ordered_suffix(
    std::string_view pattern,
    std::vector<std::string>& literals) {
  literals.clear();
  constexpr std::string_view prefix = "(?s:";
  constexpr std::string_view suffix = ")\\z";
  if (pattern.size() <= prefix.size() + suffix.size() ||
      pattern.substr(0, prefix.size()) != prefix ||
      pattern.substr(pattern.size() - suffix.size()) != suffix) {
    return false;
  }

  const auto body = pattern.substr(
      prefix.size(), pattern.size() - prefix.size() - suffix.size());
  size_t cursor = 0;
  constexpr std::string_view atomic_prefix = "(?>.*?";
  while (body.substr(cursor, atomic_prefix.size()) == atomic_prefix) {
    size_t close = cursor + atomic_prefix.size();
    bool escaped = false;
    for (; close < body.size(); ++close) {
      const char ch = body[close];
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == ')') {
        break;
      }
    }
    if (close >= body.size()) {
      literals.clear();
      return false;
    }
    std::string literal;
    if (!regex_append_unescaped_literal(
            body.substr(cursor + atomic_prefix.size(),
                        close - cursor - atomic_prefix.size()),
            literal) ||
        literal.empty()) {
      literals.clear();
      return false;
    }
    literals.push_back(std::move(literal));
    cursor = close + 1;
  }

  if (literals.empty() || body.substr(cursor, 2) != ".*") {
    literals.clear();
    return false;
  }
  std::string final_literal;
  if (!regex_append_unescaped_literal(body.substr(cursor + 2), final_literal) ||
      final_literal.empty()) {
    literals.clear();
    return false;
  }
  literals.push_back(std::move(final_literal));
  return true;
}

size_t utf8_codepoint_count(std::string_view text, size_t byte_end) {
  byte_end = std::min(byte_end, text.size());
  size_t count = 0;
  for (size_t i = 0; i < byte_end; ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xc0u) != 0x80u) {
      ++count;
    }
  }
  return count;
}

size_t utf8_byte_offset(std::string_view text, size_t codepoint_offset) {
  size_t count = 0;
  size_t i = 0;
  while (i < text.size() && count < codepoint_offset) {
    ++i;
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0u) == 0x80u) {
      ++i;
    }
    ++count;
  }
  return i;
}

Value match_group_value(const MatchState& state, size_t index) {
  if (index >= state.groups.size() || !state.groups[index].matched) {
    return Value::none();
  }
  const auto& group = state.groups[index];
  std::string live_text;
  std::string_view current_text = *state.text;
  bool current_bytes = state.bytes_text;
  if (value_as_bytearray(state.subject) != nullptr ||
      value_as_memoryview(state.subject) != nullptr) {
    bool live_bytes = false;
    if (value_to_match_text(state.subject, live_text, live_bytes) && live_bytes == state.bytes_text) {
      current_text = live_text;
      current_bytes = live_bytes;
    }
  }
  const size_t raw_start = state.bytes_text || state.ascii_text
      ? static_cast<size_t>(group.start)
      : utf8_byte_offset(current_text, static_cast<size_t>(group.start));
  const size_t raw_end = state.bytes_text || state.ascii_text
      ? static_cast<size_t>(group.end)
      : utf8_byte_offset(current_text, static_cast<size_t>(group.end));
  const size_t start = std::min(raw_start, current_text.size());
  const size_t end = std::max(start, std::min(raw_end, current_text.size()));
  std::string text(current_text.substr(start, end - start));
  return current_bytes ? Value::bytes(std::move(text)) : Value::string(std::move(text));
}

int64_t match_lastindex(const MatchState& state) {
  std::string ignored;
  if (auto* pattern = pattern_state(state.pattern, ignored)) {
    for (auto it = pattern->group_close_order.rbegin(); it != pattern->group_close_order.rend(); ++it) {
      if (*it > 0 && static_cast<size_t>(*it) < state.groups.size() &&
          state.groups[static_cast<size_t>(*it)].matched) {
        return *it;
      }
    }
  }
  for (size_t i = state.groups.size(); i > 1; --i) {
    if (state.groups[i - 1].matched) {
      return static_cast<int64_t>(i - 1);
    }
  }
  return -1;
}

Value match_lastgroup(const MatchState& state, int64_t lastindex) {
  if (lastindex < 0) {
    return Value::none();
  }
  std::string ignored;
  auto* pattern = pattern_state(state.pattern, ignored);
  if (pattern == nullptr) {
    return Value::none();
  }
  for (const auto& entry : pattern->group_names) {
    if (entry.second == lastindex) {
      return Value::string(entry.first);
    }
  }
  return Value::none();
}

std::vector<int64_t> regex_group_close_order(std::string_view pattern) {
  std::vector<int64_t> order;
  std::vector<int64_t> stack;
  int64_t next_group = 0;
  bool escaped = false;
  bool in_class = false;
  for (size_t i = 0; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == '\\') {
      escaped = true;
      continue;
    }
    if (ch == '[' && !in_class) {
      in_class = true;
      continue;
    }
    if (ch == ']' && in_class) {
      in_class = false;
      continue;
    }
    if (in_class) continue;
    if (ch == '(') {
      bool capturing = i + 1 >= pattern.size() || pattern[i + 1] != '?';
      if (!capturing && i + 3 < pattern.size() && pattern[i + 1] == '?' &&
          pattern[i + 2] == 'P' && pattern[i + 3] == '<') {
        capturing = true;
      }
      stack.push_back(capturing ? ++next_group : 0);
    } else if (ch == ')' && !stack.empty()) {
      const int64_t group = stack.back();
      stack.pop_back();
      if (group > 0) order.push_back(group);
    }
  }
  return order;
}

std::string_view strip_global_inline_flag_prefix(std::string_view pattern) {
  while (pattern.size() >= 4 && pattern[0] == '(' && pattern[1] == '?') {
    const size_t close = pattern.find(')', 2);
    if (close == std::string_view::npos) {
      break;
    }
    bool valid = close > 2;
    for (size_t i = 2; valid && i < close; ++i) {
      valid = std::string_view("aiLmsux").find(pattern[i]) != std::string_view::npos;
    }
    if (!valid) {
      break;
    }
    pattern.remove_prefix(close + 1);
  }
  return pattern;
}

bool append_match_group(
    const MatchState& state,
    std::string_view group_name,
    std::string& output,
    std::string& error) {
  int64_t index = -1;
  bool numeric = !group_name.empty();
  for (char ch : group_name) {
    if (ch < '0' || ch > '9') {
      numeric = false;
      break;
    }
  }
  if (numeric) {
    index = 0;
    for (char ch : group_name) {
      index = index * 10 + (ch - '0');
    }
  } else {
    auto* pattern = pattern_state(state.pattern, error);
    if (pattern == nullptr) {
      return false;
    }
    auto it = pattern->group_names.find(std::string(group_name));
    if (it != pattern->group_names.end()) {
      index = it->second;
    }
  }
  if (index < 0 || static_cast<size_t>(index) >= state.groups.size()) {
    error = "invalid group reference";
    return false;
  }
  const auto& group = state.groups[static_cast<size_t>(index)];
  if (group.matched) {
    const size_t start = state.bytes_text || state.ascii_text
        ? static_cast<size_t>(group.start)
        : utf8_byte_offset(*state.text, static_cast<size_t>(group.start));
    const size_t end = state.bytes_text || state.ascii_text
        ? static_cast<size_t>(group.end)
        : utf8_byte_offset(*state.text, static_cast<size_t>(group.end));
    output.append(
        *state.text,
        start,
        end - start);
  }
  return true;
}

int regex_hex_digit(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

bool regex_parse_hex_escape(
    std::string_view pattern,
    size_t& index,
    size_t digits,
    uint32_t& codepoint) {
  if (index + digits >= pattern.size()) return false;
  codepoint = 0;
  for (size_t offset = 1; offset <= digits; ++offset) {
    const int digit = regex_hex_digit(pattern[index + offset]);
    if (digit < 0) return false;
    codepoint = (codepoint << 4u) | static_cast<uint32_t>(digit);
  }
  index += digits;
  return codepoint <= 0x10ffffu;
}

void regex_append_utf8_codepoint(std::string& out, uint32_t codepoint) {
  if (codepoint <= 0x7fu) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ffu) {
    out.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else if (codepoint <= 0xffffu) {
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

bool expand_match_template(
    const MatchState& state,
    std::string_view replacement,
    std::string& output,
    std::string& error) {
  output.clear();
  for (size_t i = 0; i < replacement.size(); ++i) {
    const char ch = replacement[i];
    if (ch != '\\' || i + 1 >= replacement.size()) {
      output.push_back(ch);
      continue;
    }
    const char next = replacement[++i];
    if (next == 'g' && i + 1 < replacement.size() && replacement[i + 1] == '<') {
      const size_t close = replacement.find('>', i + 2);
      if (close == std::string_view::npos ||
          !append_match_group(state, replacement.substr(i + 2, close - i - 2), output, error)) {
        if (error.empty()) error = "missing > in group reference";
        return false;
      }
      i = close;
      continue;
    }
    if (next == '0') {
      unsigned int value = 0;
      uint32_t digits = 0;
      size_t end = i;
      while (end < replacement.size() && digits < 3 && replacement[end] >= '0' && replacement[end] <= '7') {
        value = value * 8u + static_cast<unsigned int>(replacement[end] - '0');
        ++end;
        ++digits;
      }
      if (!state.bytes_text && value >= 0x80u) {
        output.push_back(static_cast<char>(0xc0u | (value >> 6)));
        output.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
      } else {
        output.push_back(static_cast<char>(value));
      }
      i = end - 1;
      continue;
    }
    if (next >= '1' && next <= '7' && i + 2 < replacement.size() &&
        replacement[i + 1] >= '0' && replacement[i + 1] <= '7' &&
        replacement[i + 2] >= '0' && replacement[i + 2] <= '7') {
      const unsigned int value = static_cast<unsigned int>(next - '0') * 64u +
          static_cast<unsigned int>(replacement[i + 1] - '0') * 8u +
          static_cast<unsigned int>(replacement[i + 2] - '0');
      if (value > 255u) {
        error = "octal escape value outside of range 0-0o377";
        return false;
      }
      if (!state.bytes_text && value >= 0x80u) {
        output.push_back(static_cast<char>(0xc0u | (value >> 6)));
        output.push_back(static_cast<char>(0x80u | (value & 0x3fu)));
      } else {
        output.push_back(static_cast<char>(value));
      }
      i += 2;
      continue;
    }
    if (next >= '0' && next <= '9') {
      size_t end = i + 1;
      while (end < replacement.size() && end - i < 2 && replacement[end] >= '0' && replacement[end] <= '9') {
        ++end;
      }
      if (!append_match_group(state, replacement.substr(i, end - i), output, error)) {
        return false;
      }
      i = end - 1;
      continue;
    }
    switch (next) {
      case 'a': output.push_back('\a'); break;
      case 'b': output.push_back('\b'); break;
      case 'f': output.push_back('\f'); break;
      case 'n': output.push_back('\n'); break;
      case 'r': output.push_back('\r'); break;
      case 't': output.push_back('\t'); break;
      case 'v': output.push_back('\v'); break;
      case '\\': output.push_back('\\'); break;
      default:
        output.push_back('\\');
        output.push_back(next);
        break;
    }
  }
  return true;
}

bool match_get_attr(const Value& self, const std::string& name, Value& out, std::string& error) {
  auto* state = match_state(self, error);
  if (state == nullptr) {
    return false;
  }
  if (name == "lastindex") {
    const int64_t index = match_lastindex(*state);
    out = index < 0 ? Value::none() : Value::int64(index);
    return true;
  }
  if (name == "lastgroup") {
    out = match_lastgroup(*state, match_lastindex(*state));
    return true;
  }
  if (name == "re") {
    out = state->pattern;
    return true;
  }
  if (name == "string") {
    out = state->subject.tag != ValueTag::Invalid
        ? state->subject
        : (state->bytes_text ? Value::bytes(*state->text) : Value::string(*state->text));
    return true;
  }
  if (name == "pos") {
    out = Value::int64(state->pos);
    return true;
  }
  if (name == "endpos") {
    out = Value::int64(state->endpos);
    return true;
  }
  if (name == "regs") {
    std::vector<Value> spans;
    spans.reserve(state->groups.size());
    for (const auto& group : state->groups) {
      spans.push_back(Value::tuple({Value::int64(group.start), Value::int64(group.end)}));
    }
    out = Value::tuple(std::move(spans));
    return true;
  }
  return false;
}

bool resolve_match_group_index(Runtime& runtime, const MatchState& state, const Value* args, uint32_t argc, int64_t& index, std::string& error) {
  index = 0;
  if (argc == 1) {
    return true;
  }
  if (auto* name = value_as_string(args[1])) {
    auto* pattern = pattern_state(state.pattern, error);
    if (pattern == nullptr) {
      return false;
    }
    auto it = pattern->group_names.find(string_object_to_string(*name));
    if (it == pattern->group_names.end()) {
      error = "no such group";
      runtime.raise_class_error("IndexError", error);
      return false;
    }
    index = it->second;
  } else if (args[1].tag == ValueTag::Int64) {
    index = args[1].as.i64;
  } else if (value_as_instance(args[1]) != nullptr) {
    Value index_method;
    std::string attr_error;
    if (!object_get_attr(args[1], "__index__", index_method, attr_error)) {
      error = "no such group";
      runtime.raise_class_error("IndexError", error);
      return false;
    }
    Value index_value;
    error.clear();
    if (!runtime_call_callable(runtime, index_method, nullptr, 0, index_value, error)) {
      return false;
    }
    if (!value_int_like_to_i64(index_value, index)) {
      if (value_as_bigint(index_value) != nullptr) {
        error = "no such group";
        runtime.raise_class_error("IndexError", error);
        return false;
      }
      error = "__index__ returned non-int";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  } else {
    error = "no such group";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  if (index < 0 || static_cast<size_t>(index) >= state.groups.size()) {
    error = "no such group";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  return true;
}

Value make_match_type(Runtime& runtime) {
  static Value match_type = Value::invalid();
  if (match_type.tag != ValueTag::Invalid) {
    return match_type;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_sre")});
  attrs.push_back({"__repr__", runtime.make_native_function("_sre.Match.__repr__", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc != 1) {
      error = "Match.__repr__() expected no arguments";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr || state->groups.empty()) {
      return false;
    }
    const auto& whole = state->groups[0];
    const Value matched = match_group_value(*state, 0);
    out = Value::string(
        "<_sre.SRE_Match object; span=(" + std::to_string(whole.start) + ", " +
        std::to_string(whole.end) + "), match=" + value_to_repr(matched) + ">");
    return true;
  })});
  attrs.push_back({"__copy__", runtime.make_native_function("_sre.Match.__copy__", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc != 1) { error = "Match.__copy__() expected no arguments"; return false; }
    value_assign_fast(out, args[0]);
    return true;
  })});
  attrs.push_back({"__deepcopy__", runtime.make_native_function("_sre.Match.__deepcopy__", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) { error = "Match.__deepcopy__() expected optional memo"; return false; }
    value_assign_fast(out, args[0]);
    return true;
  })});
  attrs.push_back({"group", runtime.make_native_function("_sre.Match.group", [](Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1) {
      error = "Match.group() expected optional group indices";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    if (argc <= 2) {
      int64_t index = 0;
      if (!resolve_match_group_index(runtime, *state, args, argc, index, error)) {
        return false;
      }
      out = match_group_value(*state, static_cast<size_t>(index));
      return true;
    }
    std::vector<Value> groups;
    groups.reserve(argc - 1);
    for (uint32_t i = 1; i < argc; ++i) {
      Value group_args[] = {args[0], args[i]};
      int64_t index = 0;
      if (!resolve_match_group_index(runtime, *state, group_args, 2, index, error)) {
        return false;
      }
      groups.push_back(match_group_value(*state, static_cast<size_t>(index)));
    }
    out = Value::tuple(std::move(groups));
    return true;
  })});
  attrs.push_back({"__getitem__", runtime.make_native_function("_sre.Match.__getitem__", [](Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc != 2) {
      error = "Match.__getitem__() expected group index";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    int64_t index = 0;
    if (!resolve_match_group_index(runtime, *state, args, argc, index, error)) {
      return false;
    }
    out = match_group_value(*state, static_cast<size_t>(index));
    return true;
  })});
  attrs.push_back({"groups", runtime.make_native_function("_sre.Match.groups", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) {
      error = "Match.groups() expected optional default";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    std::vector<Value> groups;
    for (size_t i = 1; i < state->groups.size(); ++i) {
      groups.push_back(state->groups[i].matched ? match_group_value(*state, i) : (argc == 2 ? args[1] : Value::none()));
    }
    out = Value::tuple(std::move(groups));
    return true;
  })});
  attrs.push_back({"groupdict", runtime.make_native_function("_sre.Match.groupdict", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) {
      error = "Match.groupdict() expected optional default";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    auto* pattern = pattern_state(state->pattern, error);
    if (pattern == nullptr) {
      return false;
    }
    const Value default_value = argc == 2 ? args[1] : Value::none();
    out = Value::dict({});
    for (const auto& entry : pattern->group_names) {
      const int64_t index = entry.second;
      if (index < 0 || static_cast<size_t>(index) >= state->groups.size()) {
        if (!mapping_set_item(out, Value::string(entry.first), default_value, error)) {
          return false;
        }
        continue;
      }
      Value group_value = state->groups[static_cast<size_t>(index)].matched
                              ? match_group_value(*state, static_cast<size_t>(index))
                              : default_value;
      if (!mapping_set_item(out, Value::string(entry.first), group_value, error)) {
        return false;
      }
    }
    return true;
  })});
  attrs.push_back({"expand", runtime.make_native_function("_sre.Match.expand", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc != 2) {
      error = "Match.expand() expected one template";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    std::string replacement;
    bool replacement_bytes = false;
    if (!value_to_match_text(args[1], replacement, replacement_bytes) || replacement_bytes != state->bytes_text) {
      error = "template must be matching string/bytes object";
      return false;
    }
    std::string expanded;
    if (!expand_match_template(*state, replacement, expanded, error)) {
      return false;
    }
    out = state->bytes_text ? Value::bytes(std::move(expanded)) : Value::string(std::move(expanded));
    return true;
  })});
  attrs.push_back({"start", runtime.make_native_function("_sre.Match.start", [](Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) {
      error = "Match.start() expected optional group index";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    int64_t index = 0;
    if (!resolve_match_group_index(runtime, *state, args, argc, index, error)) {
      return false;
    }
    value_set_int64(out, state->groups[static_cast<size_t>(index)].start);
    return true;
  })});
  attrs.push_back({"end", runtime.make_native_function("_sre.Match.end", [](Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) {
      error = "Match.end() expected optional group index";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    int64_t index = 0;
    if (!resolve_match_group_index(runtime, *state, args, argc, index, error)) {
      return false;
    }
    value_set_int64(out, state->groups[static_cast<size_t>(index)].end);
    return true;
  })});
  attrs.push_back({"span", runtime.make_native_function("_sre.Match.span", [](Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) {
      error = "Match.span() expected optional group index";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    int64_t index = 0;
    if (!resolve_match_group_index(runtime, *state, args, argc, index, error)) {
      return false;
    }
    const auto& group = state->groups[static_cast<size_t>(index)];
    out = Value::tuple({Value::int64(group.start), Value::int64(group.end)});
    return true;
  })});
  match_type = Value::class_object("SRE_Match", std::move(attrs));
  return match_type;
}

Value make_pattern_type(Runtime& runtime);
Value make_finditer_type(Runtime& runtime);

void repair_python_repeat_captures(
    const PatternState& pattern,
    const std::string& text,
    size_t byte_match_start,
    size_t byte_match_end,
    bool bytes_text,
    std::vector<MatchGroup>& groups) {
  const auto followed_by_repeat_many = [&](size_t close) {
    if (close + 1 >= pattern.pattern.size()) return false;
    const char quantifier = pattern.pattern[close + 1];
    if (quantifier == '*' || quantifier == '+') return true;
    if (quantifier != '{') return false;
    const size_t brace_close = pattern.pattern.find('}', close + 2);
    if (brace_close == std::string::npos) return false;
    const auto repeat = std::string_view(pattern.pattern).substr(
        close + 2, brace_close - close - 2);
    const size_t comma = repeat.find(',');
    const auto maximum = comma == std::string_view::npos ? repeat : repeat.substr(comma + 1);
    if (comma != std::string_view::npos && maximum.empty()) return true;
    int64_t value = 0;
    if (maximum.empty()) return false;
    for (const char digit : maximum) {
      if (digit < '0' || digit > '9') return false;
      value = value * 10 + (digit - '0');
    }
    return value > 1;
  };
  for (size_t outer_open = pattern.pattern.find("(("); outer_open != std::string::npos;
       outer_open = pattern.pattern.find("((", outer_open + 2)) {
    const size_t inner_open = outer_open + 1;
    const size_t inner_close = pattern.pattern.find(')', inner_open + 1);
    if (inner_close == std::string::npos ||
        pattern.pattern.substr(inner_open + 1, inner_close - inner_open - 1).find('(') != std::string::npos) continue;
    const size_t separator = pattern.pattern.find('|', inner_close + 1);
    if (separator == std::string::npos) continue;
    const size_t outer_close = pattern.pattern.find(')', separator + 1);
    if (outer_close == std::string::npos || !followed_by_repeat_many(outer_close)) continue;
    const int64_t group_index = regex_capture_count_before(pattern.pattern, inner_open) + 1;
    if (group_index <= 0 || static_cast<size_t>(group_index) >= groups.size() ||
        groups[static_cast<size_t>(group_index)].matched) continue;
    const std::string branch = pattern.pattern.substr(
        inner_open, separator - inner_open);
    try {
      const std::regex branch_regex(branch, std::regex::ECMAScript);
      const auto begin = text.cbegin() + static_cast<std::ptrdiff_t>(byte_match_start);
      const auto end = text.cbegin() + static_cast<std::ptrdiff_t>(byte_match_end);
      std::match_results<std::string::const_iterator> branch_match;
      auto cursor = begin;
      bool found = false;
      size_t captured_start = 0;
      size_t captured_end = 0;
      while (cursor <= end && std::regex_search(cursor, end, branch_match, branch_regex)) {
        if (branch_match.size() > 1 && branch_match[1].matched) {
          captured_start = static_cast<size_t>(cursor - text.cbegin()) +
              static_cast<size_t>(branch_match.position(1));
          captured_end = captured_start + static_cast<size_t>(branch_match.length(1));
          found = true;
        }
        const size_t advance = static_cast<size_t>(branch_match.position(0)) +
            std::max<size_t>(1, static_cast<size_t>(branch_match.length(0)));
        if (advance > static_cast<size_t>(end - cursor)) break;
        cursor += static_cast<std::ptrdiff_t>(advance);
      }
      if (found) {
        auto& group = groups[static_cast<size_t>(group_index)];
        group.matched = true;
        group.start = static_cast<int64_t>(bytes_text ? captured_start : utf8_codepoint_count(text, captured_start));
        group.end = static_cast<int64_t>(bytes_text ? captured_end : utf8_codepoint_count(text, captured_end));
      }
    } catch (const std::regex_error&) {
    }
  }

  struct GroupBounds {
    size_t open = 0;
    size_t close = 0;
    bool capturing = false;
    int64_t index = 0;
  };
  std::vector<GroupBounds> bounds;
  std::vector<size_t> stack;
  bool escaped = false;
  bool in_class = false;
  int64_t capture_index = 0;
  for (size_t i = 0; i < pattern.pattern.size(); ++i) {
    const char ch = pattern.pattern[i];
    if (escaped) { escaped = false; continue; }
    if (ch == '\\') { escaped = true; continue; }
    if (ch == '[') { in_class = true; continue; }
    if (ch == ']' && in_class) { in_class = false; continue; }
    if (in_class) continue;
    if (ch == '(') {
      const bool capturing = i + 1 >= pattern.pattern.size() || pattern.pattern[i + 1] != '?' ||
          (i + 3 < pattern.pattern.size() && pattern.pattern.substr(i + 1, 3) == "?P<");
      bounds.push_back(GroupBounds{i, 0, capturing, capturing ? ++capture_index : 0});
      stack.push_back(bounds.size() - 1);
    } else if (ch == ')' && !stack.empty()) {
      bounds[stack.back()].close = i;
      stack.pop_back();
    }
  }
  for (const auto& capture : bounds) {
    if (!capture.capturing || capture.close <= capture.open || capture.index <= 0 ||
        static_cast<size_t>(capture.index) >= groups.size() || groups[static_cast<size_t>(capture.index)].matched) continue;
    bool repeated_ancestor = false;
    bool assertion_ancestor = false;
    for (const auto& ancestor : bounds) {
      if (ancestor.open < capture.open && ancestor.close > capture.close &&
          ancestor.open + 2 < pattern.pattern.size() && pattern.pattern[ancestor.open + 1] == '?' &&
          (pattern.pattern[ancestor.open + 2] == '=' || pattern.pattern[ancestor.open + 2] == '!')) {
        assertion_ancestor = true;
      }
      if (ancestor.open < capture.open && ancestor.close > capture.close &&
          followed_by_repeat_many(ancestor.close)) {
        repeated_ancestor = true;
      }
    }
    if (!repeated_ancestor || assertion_ancestor) continue;
    size_t body_start = capture.open + 1;
    if (body_start + 2 < capture.close && pattern.pattern.substr(body_start, 3) == "?P<") {
      body_start = pattern.pattern.find('>', body_start + 3);
      if (body_start == std::string::npos || ++body_start >= capture.close) continue;
    }
    const auto body = std::string_view(pattern.pattern).substr(body_start, capture.close - body_start);
    if (body.empty() || !std::all_of(body.begin(), body.end(), [](char value) {
          return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') || value == '|';
        })) continue;
    std::string candidate = "(" + std::string(body) + ")";
    for (size_t tail = capture.close + 1; tail < pattern.pattern.size(); ++tail) {
      const char value = pattern.pattern[tail];
      if (std::string_view("|)*+?{").find(value) != std::string_view::npos) break;
      if (!std::isalnum(static_cast<unsigned char>(value))) break;
      candidate.push_back(value);
    }
    try {
      const std::regex candidate_regex(candidate, std::regex::ECMAScript);
      auto cursor = text.cbegin() + static_cast<std::ptrdiff_t>(byte_match_start);
      const auto end = text.cbegin() + static_cast<std::ptrdiff_t>(byte_match_end);
      std::match_results<std::string::const_iterator> candidate_match;
      bool found = false;
      size_t captured_start = 0;
      size_t captured_end = 0;
      while (cursor <= end && std::regex_search(cursor, end, candidate_match, candidate_regex)) {
        captured_start = static_cast<size_t>(cursor - text.cbegin()) +
            static_cast<size_t>(candidate_match.position(1));
        captured_end = captured_start + static_cast<size_t>(candidate_match.length(1));
        found = true;
        const size_t advance = static_cast<size_t>(candidate_match.position(0)) +
            std::max<size_t>(1, static_cast<size_t>(candidate_match.length(0)));
        if (advance > static_cast<size_t>(end - cursor)) break;
        cursor += static_cast<std::ptrdiff_t>(advance);
      }
      if (found) {
        auto& group = groups[static_cast<size_t>(capture.index)];
        group.matched = true;
        group.start = static_cast<int64_t>(bytes_text ? captured_start : utf8_codepoint_count(text, captured_start));
        group.end = static_cast<int64_t>(bytes_text ? captured_end : utf8_codepoint_count(text, captured_end));
      }
    } catch (const std::regex_error&) {
    }
  }

  for (const auto& capture : bounds) {
    if (!capture.capturing || capture.close <= capture.open || !followed_by_repeat_many(capture.close) ||
        capture.index <= 0 || static_cast<size_t>(capture.index) >= groups.size()) continue;
    size_t repeat_end = capture.close + 2;
    if (pattern.pattern[capture.close + 1] == '{') {
      const size_t brace_close = pattern.pattern.find('}', capture.close + 2);
      if (brace_close == std::string::npos) continue;
      repeat_end = brace_close + 1;
    }
    if (repeat_end < pattern.pattern.size() && pattern.pattern[repeat_end] == '?') continue;
    if (repeat_end < pattern.pattern.size() && pattern.pattern[repeat_end] == '+') ++repeat_end;
    bool terminal_repeat = true;
    for (size_t tail = repeat_end; tail < pattern.pattern.size(); ++tail) {
      if (pattern.pattern[tail] == ')' || pattern.pattern[tail] == '$') continue;
      if (pattern.pattern[tail] == '\\' && tail + 1 < pattern.pattern.size() &&
          (pattern.pattern[tail + 1] == 'z' || pattern.pattern[tail + 1] == 'Z')) {
        ++tail;
        continue;
      }
      terminal_repeat = false;
      break;
    }
    if (!terminal_repeat) continue;
    const auto body = std::string_view(pattern.pattern).substr(
        capture.open + 1, capture.close - capture.open - 1);
    const bool body_can_finish_empty = !body.empty() &&
        (body.back() == '*' || body.back() == '?' ||
         (body.size() >= 4 && body.substr(body.size() - 4) == "{0,}"));
    if (!body_can_finish_empty) continue;
    auto& group = groups[static_cast<size_t>(capture.index)];
    group.matched = true;
    group.start = group.end = static_cast<int64_t>(
        bytes_text ? byte_match_end : utf8_codepoint_count(text, byte_match_end));
  }

  for (size_t open = 0; open < pattern.pattern.size(); ++open) {
    if (pattern.pattern[open] != '(' ||
        (open + 1 < pattern.pattern.size() && pattern.pattern[open + 1] == '?')) continue;
    const size_t close = pattern.pattern.find(')', open + 1);
    if (close == std::string::npos) continue;
    const auto body = std::string_view(pattern.pattern).substr(open + 1, close - open - 1);
    if (body.find('(') != std::string_view::npos || (!body.empty() && body.back() != '?')) continue;
    size_t suffix = close + 1;
    if (suffix + 1 >= pattern.pattern.size()) continue;
    if (pattern.pattern[suffix] == '{') {
      suffix = pattern.pattern.find('}', suffix);
      if (suffix == std::string::npos || suffix + 1 >= pattern.pattern.size() || pattern.pattern[suffix + 1] != '+') continue;
      suffix += 2;
    } else if (std::string_view("*+?").find(pattern.pattern[suffix]) != std::string_view::npos &&
               pattern.pattern[suffix + 1] == '+') {
      suffix += 2;
    } else {
      continue;
    }
    std::string suffix_literal;
    if (!regex_append_unescaped_literal(std::string_view(pattern.pattern).substr(suffix), suffix_literal) ||
        suffix_literal.size() > byte_match_end - byte_match_start) continue;
    const int64_t group_index = regex_capture_count_before(pattern.pattern, open) + 1;
    if (group_index <= 0 || static_cast<size_t>(group_index) >= groups.size()) continue;
    const size_t empty_at = byte_match_end - suffix_literal.size();
    auto& group = groups[static_cast<size_t>(group_index)];
    group.matched = true;
    group.start = group.end = static_cast<int64_t>(
        bytes_text ? empty_at : utf8_codepoint_count(text, empty_at));
  }
}

Value make_match(
    Runtime& runtime,
    const Value& pattern,
    const std::string& text,
    bool bytes_text,
    const std::match_results<std::string::const_iterator>& match,
    size_t base,
    size_t pos,
    size_t endpos,
    const Value* subject = nullptr,
    std::shared_ptr<const std::string> shared_text = {},
    bool ascii_text = false) {
  Value value = Value::instance(make_match_type(runtime));
  auto* state = new MatchState();
  state->pattern = pattern;
  if (subject != nullptr) state->subject = *subject;
  state->text = shared_text
      ? std::move(shared_text)
      : std::make_shared<const std::string>(text);
  state->bytes_text = bytes_text;
  state->ascii_text = ascii_text;
  state->pos = static_cast<int64_t>(pos);
  state->endpos = static_cast<int64_t>(endpos);
  std::string pattern_error;
  auto* compiled = pattern_state(pattern, pattern_error);
  const size_t public_group_count = compiled != nullptr
      ? static_cast<size_t>(std::max<int64_t>(0, compiled->group_count)) + 1
      : match.size();
  state->groups.reserve(public_group_count);
  for (size_t i = 0; i < public_group_count; ++i) {
    const size_t engine_index = compiled != nullptr && i < compiled->engine_group_for_python.size()
        ? compiled->engine_group_for_python[i] : i;
    MatchGroup group;
    group.matched = engine_index < match.size() && match[engine_index].matched;
    if (group.matched) {
      const size_t byte_start = base + static_cast<size_t>(match.position(engine_index));
      const size_t byte_end = byte_start + static_cast<size_t>(match.length(engine_index));
      group.start = static_cast<int64_t>(
          bytes_text || ascii_text ? byte_start : utf8_codepoint_count(text, byte_start));
      group.end = static_cast<int64_t>(
          bytes_text || ascii_text ? byte_end : utf8_codepoint_count(text, byte_end));
    }
    state->groups.push_back(group);
  }
  std::string error;
  if (compiled != nullptr && !state->groups.empty()) {
    const size_t byte_match_start = base + static_cast<size_t>(match.position(0));
    const size_t byte_match_end = byte_match_start + static_cast<size_t>(match.length(0));
    repair_python_repeat_captures(
        *compiled, text, byte_match_start, byte_match_end,
        bytes_text || ascii_text, state->groups);
  }
  (void)instance_set_native_data(value, kMatchNativeType, state, match_cleanup, error);
  (void)instance_set_native_attr_hooks(value, match_get_attr, nullptr, nullptr, error);
  return value;
}

Value make_simple_match(
    Runtime& runtime,
    const Value& pattern,
    const std::string& text,
    bool bytes_text,
    size_t start,
    size_t end,
    size_t pos,
    size_t endpos,
    int64_t group_count,
    const Value* subject = nullptr) {
  Value value = Value::instance(make_match_type(runtime));
  auto* state = new MatchState();
  state->pattern = pattern;
  if (subject != nullptr) state->subject = *subject;
  state->text = std::make_shared<const std::string>(text);
  state->bytes_text = bytes_text;
  state->ascii_text = bytes_text;
  state->pos = static_cast<int64_t>(pos);
  state->endpos = static_cast<int64_t>(endpos);
  state->groups.reserve(static_cast<size_t>(std::max<int64_t>(1, group_count + 1)));
  state->groups.push_back(MatchGroup{
      static_cast<int64_t>(bytes_text ? start : utf8_codepoint_count(text, start)),
      static_cast<int64_t>(bytes_text ? end : utf8_codepoint_count(text, end)),
      true});
  for (int64_t i = 0; i < group_count; ++i) {
    state->groups.push_back(MatchGroup{});
  }
  std::string error;
  (void)instance_set_native_data(value, kMatchNativeType, state, match_cleanup, error);
  (void)instance_set_native_attr_hooks(value, match_get_attr, nullptr, nullptr, error);
  return value;
}

bool pattern_match_fast(
    Runtime& runtime,
    const PatternState& state,
    const Value& pattern_value,
    const Value& subject,
    const std::string& text,
    bool bytes_text,
    size_t pos,
    bool continuous,
    bool full,
    Value& out) {
  if (!continuous || !state.lookbehinds.empty() || (state.flags & kFlagIgnoreCase) != 0) {
    return false;
  }
  if (state.fast_kind == FastRegexKind::LiteralPrefix) {
    const bool matched = pos <= text.size() &&
                         text.compare(pos, state.fast_literal.size(), state.fast_literal) == 0 &&
                         (!full || pos + state.fast_literal.size() == text.size());
    if (!matched) {
      value_set_none(out);
      return true;
    }
    out = make_simple_match(runtime, pattern_value, text, bytes_text, pos, pos + state.fast_literal.size(), pos, text.size(), state.group_count, &subject);
    return true;
  }
  if (state.fast_kind == FastRegexKind::AnySuffix) {
    const size_t suffix_size = state.fast_literal.size();
    const bool matched = pos <= text.size() &&
                         suffix_size <= text.size() - pos &&
                         text.compare(text.size() - suffix_size, suffix_size, state.fast_literal) == 0;
    if (!matched) {
      value_set_none(out);
      return true;
    }
    out = make_simple_match(runtime, pattern_value, text, bytes_text, pos, text.size(), pos, text.size(), state.group_count, &subject);
    return true;
  }
  if (state.fast_kind == FastRegexKind::OrderedSuffix) {
    if (state.fast_literals.empty() || pos > text.size()) {
      value_set_none(out);
      return true;
    }
    size_t cursor = pos;
    for (size_t i = 0; i + 1 < state.fast_literals.size(); ++i) {
      const size_t found = text.find(state.fast_literals[i], cursor);
      if (found == std::string::npos) {
        value_set_none(out);
        return true;
      }
      cursor = found + state.fast_literals[i].size();
    }
    const auto& suffix = state.fast_literals.back();
    const bool matched = suffix.size() <= text.size() - pos &&
                         text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0 &&
                         text.size() - suffix.size() >= cursor;
    if (!matched) {
      value_set_none(out);
      return true;
    }
    out = make_simple_match(
        runtime, pattern_value, text, bytes_text, pos, text.size(), pos,
        text.size(), state.group_count, &subject);
    return true;
  }
  return false;
}

bool regex_unicode_word_at(std::string_view text, size_t offset) {
  if (offset >= text.size()) return false;
  const size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[offset]));
  if (width == 0 || offset + width > text.size()) return false;
  uint32_t codepoint = static_cast<unsigned char>(text[offset]);
  if (width > 1) {
    codepoint &= (1u << (7u - static_cast<uint32_t>(width))) - 1u;
    for (size_t i = 1; i < width; ++i) {
      codepoint = (codepoint << 6u) | (static_cast<unsigned char>(text[offset + i]) & 0x3fu);
    }
  }
  if (codepoint == '_') return true;
  if (codepoint < 128) return std::isalnum(static_cast<unsigned char>(codepoint)) != 0;
#if defined(_WIN32)
  wchar_t units[2];
  int count = 1;
  if (codepoint <= 0xffff) {
    units[0] = static_cast<wchar_t>(codepoint);
  } else {
    const uint32_t adjusted = codepoint - 0x10000u;
    units[0] = static_cast<wchar_t>(0xd800u + (adjusted >> 10u));
    units[1] = static_cast<wchar_t>(0xdc00u + (adjusted & 0x3ffu));
    count = 2;
  }
  WORD types[2]{};
  return GetStringTypeW(CT_CTYPE1, units, count, types) != 0 &&
         (types[0] & (C1_ALPHA | C1_DIGIT)) != 0;
#else
  return true;
#endif
}

bool regex_unicode_word_before(std::string_view text, size_t offset) {
  if (offset == 0 || offset > text.size()) return false;
  size_t start = offset - 1;
  while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xc0u) == 0x80u) --start;
  return regex_unicode_word_at(text, start);
}

bool match_satisfies_lookbehinds(
    const PatternState& state,
    const std::string& text,
    const std::match_results<std::string::const_iterator>& match,
    size_t base) {
  const size_t match_start = base + static_cast<size_t>(match.position(0));
  size_t match_end = match_start + static_cast<size_t>(match.length(0));
  if (!state.bytes_pattern) {
    while (match_end < text.size() &&
           (static_cast<unsigned char>(text[match_end]) & 0xc0u) == 0x80u) {
      ++match_end;
    }
  }
  if ((state.requires_absolute_start && match_start != 0) ||
      (state.requires_absolute_end && match_end != text.size())) {
    return false;
  }
  if (regex_atomic_commit_rejects(state.pattern, text, match_start, match_end)) {
    return false;
  }
  for (const auto& assertion : state.boundaries) {
    if (assertion.empty_only && match_start != match_end) continue;
    const size_t offset = assertion.at_end ? match_end : match_start;
    const bool boundary = regex_unicode_word_before(text, offset) != regex_unicode_word_at(text, offset);
    if (boundary != assertion.boundary) return false;
  }
  for (const auto& assertion : state.lookbehinds) {
    if (assertion.empty_only && match_start != match_end) continue;
    if (assertion.marker_group > 0 &&
        (static_cast<size_t>(assertion.marker_group) >= match.size() ||
         !match[static_cast<size_t>(assertion.marker_group)].matched)) {
      continue;
    }
    const size_t anchor = assertion.marker_group > 0
        ? base + static_cast<size_t>(match.position(static_cast<size_t>(assertion.marker_group)))
        : assertion.anchor_from_end && assertion.trailing_width <= match_end
        ? match_end - assertion.trailing_width
        : match_start + assertion.engine_offset;
    bool present = false;
    if (!assertion.expression.empty()) {
      const auto expression_is_present = [&](const std::string& source) {
        try {
          const std::regex expression("(?:" + source + ")$", state.regex_flags);
          std::match_results<std::string::const_iterator> expression_match;
          const auto expression_end =
              text.cbegin() + static_cast<std::ptrdiff_t>(std::min(anchor, text.size()));
          return std::regex_search(text.cbegin(), expression_end, expression_match, expression) &&
              expression_match[0].second == expression_end;
        } catch (const std::regex_error&) {
          return false;
        }
      };
      present = expression_is_present(assertion.expression);
      if (!present && assertion.positive && assertion.alternate_marker_group > 0) {
        const auto alternate = std::find_if(
            state.lookbehinds.begin(), state.lookbehinds.end(), [&](const LookbehindAssertion& candidate) {
              return candidate.marker_group == assertion.alternate_marker_group;
            });
        if (alternate != state.lookbehinds.end() && alternate->positive && !alternate->expression.empty()) {
          present = expression_is_present(alternate->expression);
        }
      }
    } else if (assertion.conditional_group > 0) {
      const size_t conditional_engine_group =
          static_cast<size_t>(assertion.conditional_group) < state.engine_group_for_python.size()
          ? state.engine_group_for_python[static_cast<size_t>(assertion.conditional_group)]
          : static_cast<size_t>(assertion.conditional_group);
      const bool group_matched = assertion.conditional_group <= assertion.captures_before_assertion &&
          conditional_engine_group < match.size() && match[conditional_engine_group].matched;
      const std::string& selected = group_matched ? assertion.conditional_yes : assertion.conditional_no;
      if (assertion.terminal_consuming) {
        if (selected.empty()) {
          const std::string& unselected = group_matched ? assertion.conditional_no : assertion.conditional_yes;
          present = unselected.empty() || unselected.size() > match_end ||
              text.compare(match_end - unselected.size(), unselected.size(), unselected) != 0;
        } else {
          present = selected.size() <= match_end &&
              text.compare(match_end - selected.size(), selected.size(), selected) == 0;
        }
      } else if (assertion.forward) {
        present = anchor <= text.size() && selected.size() <= text.size() - anchor &&
            text.compare(anchor, selected.size(), selected) == 0;
      } else {
        present = anchor <= text.size() && anchor >= selected.size() &&
            text.compare(anchor - selected.size(), selected.size(), selected) == 0;
      }
    } else if (assertion.group_ref > 0) {
      const size_t reference_engine_group =
          static_cast<size_t>(assertion.group_ref) < state.engine_group_for_python.size()
          ? state.engine_group_for_python[static_cast<size_t>(assertion.group_ref)]
          : static_cast<size_t>(assertion.group_ref);
      const std::string captured = reference_engine_group < match.size() && match[reference_engine_group].matched
          ? match[reference_engine_group].str() : std::string();
      present = anchor >= captured.size() &&
          text.compare(anchor - captured.size(), captured.size(), captured) == 0;
    } else if (assertion.category == 'w' || assertion.category == 'W') {
      present = regex_unicode_word_before(text, anchor);
      if (assertion.category == 'W') present = !present;
    } else if (anchor >= assertion.literal.size()) {
      present = text.compare(anchor - assertion.literal.size(), assertion.literal.size(), assertion.literal) == 0;
    }
    if (assertion.positive != present) {
      return false;
    }
  }
  bool in_character_class = false;
  for (size_t i = 0; i + 1 < state.pattern.size(); ++i) {
    if (state.pattern[i] == '[' && !in_character_class) {
      in_character_class = true;
      continue;
    }
    if (state.pattern[i] == ']' && in_character_class) {
      in_character_class = false;
      continue;
    }
    if (state.pattern[i] != '\\' || state.pattern[i + 1] < '1' || state.pattern[i + 1] > '9') {
      continue;
    }
    if (in_character_class) {
      ++i;
      continue;
    }
    const size_t first = i + 1;
    const bool three_digit_octal = first + 2 < state.pattern.size() &&
        state.pattern[first] <= '7' && state.pattern[first + 1] >= '0' && state.pattern[first + 1] <= '7' &&
        state.pattern[first + 2] >= '0' && state.pattern[first + 2] <= '7';
    if (three_digit_octal) {
      i = first + 2;
      continue;
    }
    size_t end = first + 1;
    int64_t group = state.pattern[first] - '0';
    if (end < state.pattern.size() && state.pattern[end] >= '0' && state.pattern[end] <= '9') {
      group = group * 10 + (state.pattern[end] - '0');
      ++end;
    }
    const bool directly_optional = end < state.pattern.size() && (state.pattern[end] == '?' || state.pattern[end] == '*');
    const bool enclosing_optional = end + 1 < state.pattern.size() && state.pattern[end] == ')' &&
        (state.pattern[end + 1] == '?' || state.pattern[end + 1] == '*');
    const size_t engine_group = group > 0 && static_cast<size_t>(group) < state.engine_group_for_python.size()
        ? state.engine_group_for_python[static_cast<size_t>(group)] : static_cast<size_t>(group);
    if (!directly_optional && !enclosing_optional &&
        (group <= 0 || engine_group >= match.size() || !match[engine_group].matched)) {
      return false;
    }
    i = end - 1;
  }
  return true;
}

bool regex_retry_longer_match_at_same_start(
    const PatternState& state,
    const std::string& text,
    size_t match_start,
    size_t rejected_end,
    size_t endpos,
    std::match_results<std::string::const_iterator>& match) {
  for (size_t candidate_end = rejected_end + 1; candidate_end <= endpos; ++candidate_end) {
    try {
      const size_t remaining = endpos - candidate_end;
      const std::regex constrained(
          "(?:" + state.engine_pattern + ")(?=[\\s\\S]{" +
              std::to_string(remaining) + "}$)",
          state.regex_flags);
      std::match_results<std::string::const_iterator> candidate;
      auto retry_flags = std::regex_constants::match_continuous;
      if (match_start != 0) {
        retry_flags |= std::regex_constants::match_prev_avail | std::regex_constants::match_not_bol;
      }
      if (std::regex_search(
              text.cbegin() + static_cast<std::ptrdiff_t>(match_start),
              text.cbegin() + static_cast<std::ptrdiff_t>(endpos),
              candidate, constrained, retry_flags) &&
          match_start + static_cast<size_t>(candidate.length(0)) == candidate_end &&
          match_satisfies_lookbehinds(state, text, candidate, match_start)) {
        match = std::move(candidate);
        return true;
      }
    } catch (const std::regex_error&) {
      return false;
    }
  }
  return false;
}

bool pattern_match_impl(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, bool continuous, bool full) {
  if (argc < 2 || argc > 4) {
    error = "Pattern match/search expected string and optional positions";
    return false;
  }
  auto* state = pattern_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (pattern_anchored_literal_miss(*state, args[1])) {
    value_set_none(out);
    return true;
  }
  std::string text;
  bool bytes_text = false;
  if (!value_to_match_text(args[1], text, bytes_text)) {
    const char* actual = args[1].tag == ValueTag::Int64 ? "int" : value_as_class(args[1]) != nullptr ? "type" : "object";
    error = std::string("expected string or bytes-like object, got '") + actual + "'";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (bytes_text != state->bytes_pattern) {
    error = "cannot mix string and bytes regex operands";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  size_t pos = 0;
  if (argc >= 3 && args[2].tag == ValueTag::Int64 && args[2].as.i64 > 0) {
    pos = static_cast<size_t>(args[2].as.i64);
  }
  size_t endpos = text.size();
  if (argc >= 4 && args[3].tag == ValueTag::Int64) {
    endpos = args[3].as.i64 < 0 ? 0 : std::min(static_cast<size_t>(args[3].as.i64), text.size());
  }
  if (pos > endpos) {
    value_set_none(out);
    return true;
  }
  if (endpos == text.size() &&
      pattern_match_fast(runtime, *state, args[0], args[1], text, bytes_text, pos, continuous, full, out)) {
    return true;
  }
  if (!ensure_pattern_regex(*state, error)) {
    return false;
  }
  std::match_results<std::string::const_iterator> match;
  const auto flags = continuous ? std::regex_constants::match_continuous : std::regex_constants::match_default;
  if (continuous && pos < state->minimum_match_start) {
    value_set_none(out);
    return true;
  }
  size_t cursor = continuous ? pos : std::max(pos, state->minimum_match_start);
  while (cursor <= endpos) {
    auto begin = text.cbegin() + static_cast<std::ptrdiff_t>(cursor);
    auto end = text.cbegin() + static_cast<std::ptrdiff_t>(endpos);
    const bool matched = full
        ? std::regex_match(begin, end, match, state->regex)
        : std::regex_search(begin, end, match, state->regex, flags);
    if (!matched) {
      value_set_none(out);
      return true;
    }
    if (match_satisfies_lookbehinds(*state, text, match, cursor)) {
      out = make_match(runtime, args[0], text, bytes_text, match, cursor, pos, endpos, &args[1]);
      return true;
    }
    const size_t rejected_start = cursor + static_cast<size_t>(match.position(0));
    const size_t rejected_end = rejected_start + static_cast<size_t>(match.length(0));
    if (!state->lookbehinds.empty() && rejected_end < endpos) {
      const auto retry_flags = rejected_start == 0
          ? std::regex_constants::match_default
          : std::regex_constants::match_prev_avail | std::regex_constants::match_not_bol;
      for (size_t candidate_end = rejected_end + 1; candidate_end <= endpos; ++candidate_end) {
        std::match_results<std::string::const_iterator> extended;
        if (!std::regex_match(
                text.cbegin() + static_cast<std::ptrdiff_t>(rejected_start),
                text.cbegin() + static_cast<std::ptrdiff_t>(candidate_end),
                extended, state->regex, retry_flags)) {
          continue;
        }
        if (match_satisfies_lookbehinds(*state, text, extended, rejected_start)) {
          out = make_match(
              runtime, args[0], text, bytes_text, extended,
              rejected_start, pos, endpos, &args[1]);
          return true;
        }
      }
    }
    if (continuous) {
      break;
    }
    cursor = rejected_end > rejected_start ? rejected_end : rejected_start + 1;
  }
  value_set_none(out);
  return true;
}

bool pattern_match(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return pattern_match_impl(runtime, args, argc, out, error, true, false);
}

bool pattern_search(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return pattern_match_impl(runtime, args, argc, out, error, false, false);
}

bool pattern_fullmatch(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return pattern_match_impl(runtime, args, argc, out, error, true, true);
}

bool bind_pattern_string_positions(
    const char* method,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value (&values)[4],
    uint32_t& bound_argc,
    std::string& error) {
  if (argc < 1 || argc > 4) {
    error = std::string(method) + "() expected string and optional positions";
    return false;
  }
  values[0] = args[0];
  values[1] = Value::invalid();
  values[2] = Value::int64(0);
  values[3] = Value::invalid();
  bool present[] = {true, false, false, false};
  for (uint32_t i = 1; i < argc; ++i) {
    value_assign_fast(values[i], args[i]);
    present[i] = true;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = std::string(method) + "() received invalid keyword argument";
      return false;
    }
    const std::string_view name(kwargs[i].name);
    const uint32_t slot = name == "string" ? 1u : name == "pos" ? 2u : name == "endpos" ? 3u : 4u;
    if (slot == 4 || present[slot]) {
      error = slot == 4 ? std::string(method) + "() got an unexpected keyword argument '" + std::string(name) + "'"
                        : std::string(method) + "() got multiple values for argument '" + std::string(name) + "'";
      return false;
    }
    value_assign_fast(values[slot], *kwargs[i].value);
    present[slot] = true;
  }
  if (!present[1]) {
    error = std::string(method) + "() missing required argument 'string'";
    return false;
  }
  bound_argc = present[3] ? 4u : present[2] ? 3u : 2u;
  return true;
}

bool pattern_match_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  Value values[4];
  uint32_t bound_argc = 0;
  const intptr_t kind = reinterpret_cast<intptr_t>(user_data);
  const char* method = kind == 1 ? "Pattern.search" : kind == 2 ? "Pattern.fullmatch" : "Pattern.match";
  if (!bind_pattern_string_positions(method, args, argc, kwargs, kwargc, values, bound_argc, error)) {
    return false;
  }
  return pattern_match_impl(runtime, values, bound_argc, out, error, kind != 1, kind == 2);
}

bool scanner_step(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) {
    error = "SRE_Scanner method expected no arguments";
    return false;
  }
  auto* state = static_cast<FindIterState*>(instance_get_native_data(args[0], kScannerNativeType));
  if (state == nullptr) {
    error = "invalid SRE scanner object";
    return false;
  }
  if (state->cursor > state->endpos) {
    value_set_none(out);
    return true;
  }
  Value text = state->bytes_text ? Value::bytes(state->text) : Value::string(state->text);
  Value match_args[] = {
      state->pattern,
      std::move(text),
      Value::int64(static_cast<int64_t>(state->cursor)),
      Value::int64(static_cast<int64_t>(state->endpos)),
  };
  const bool continuous = reinterpret_cast<intptr_t>(user_data) != 0;
  if (!pattern_match_impl(runtime, match_args, 4, out, error, continuous, false)) {
    return false;
  }
  if (out.tag == ValueTag::None) {
    state->cursor = state->endpos + 1;
    return true;
  }
  auto* match = match_state(out, error);
  if (match == nullptr || match->groups.empty()) {
    return false;
  }
  const size_t start = static_cast<size_t>(match->groups[0].start);
  const size_t end = static_cast<size_t>(match->groups[0].end);
  state->cursor = end;
  if (start == end) {
    state->cursor = end < state->endpos ? end + 1 : state->endpos + 1;
  }
  return true;
}

Value make_scanner_type(Runtime& runtime) {
  static Value scanner_type = Value::invalid();
  if (scanner_type.tag != ValueTag::Invalid) {
    return scanner_type;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_sre")});
  attrs.push_back({"__copy__", runtime.make_native_function("_sre.Match.__copy__", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc != 1) { error = "Match.__copy__() expected no arguments"; return false; }
    value_assign_fast(out, args[0]);
    return true;
  })});
  attrs.push_back({"__deepcopy__", runtime.make_native_function("_sre.Match.__deepcopy__", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) { error = "Match.__deepcopy__() expected optional memo"; return false; }
    value_assign_fast(out, args[0]);
    return true;
  })});
  attrs.push_back({"search", runtime.make_native_function("_sre.Scanner.search", scanner_step)});
  attrs.push_back({"match", runtime.make_native_function("_sre.Scanner.match", scanner_step, reinterpret_cast<void*>(1))});
  scanner_type = Value::class_object("SRE_Scanner", std::move(attrs));
  return scanner_type;
}

bool pattern_scanner_bound(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  Value values[4];
  uint32_t bound_argc = 0;
  if (!bind_pattern_string_positions("Pattern.scanner", args, argc, kwargs, kwargc, values, bound_argc, error)) {
    return false;
  }
  auto* pattern = pattern_state(values[0], error);
  std::string text;
  bool bytes_text = false;
  if (pattern == nullptr || !value_to_match_text(values[1], text, bytes_text) || bytes_text != pattern->bytes_pattern) {
    if (error.empty()) error = "expected matching string/bytes object";
    return false;
  }
  size_t pos = 0;
  if (bound_argc >= 3 && values[2].tag == ValueTag::Int64 && values[2].as.i64 > 0) {
    pos = std::min(static_cast<size_t>(values[2].as.i64), text.size());
  }
  size_t endpos = text.size();
  if (bound_argc >= 4 && values[3].tag == ValueTag::Int64) {
    endpos = values[3].as.i64 < 0 ? 0 : std::min(static_cast<size_t>(values[3].as.i64), text.size());
  }
  auto* state = new FindIterState();
  state->pattern = values[0];
  state->text = std::move(text);
  state->bytes_text = bytes_text;
  state->pos = pos;
  state->cursor = pos;
  state->endpos = endpos;
  out = Value::instance(make_scanner_type(runtime));
  if (!instance_set_native_data(out, kScannerNativeType, state, scanner_cleanup, error)) {
    delete state;
    return false;
  }
  std::string attr_error;
  (void)object_set_attr(out, "pattern", values[0], attr_error);
  return true;
}

bool pattern_scanner(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return pattern_scanner_bound(runtime, args, argc, nullptr, 0, out, error);
}

bool pattern_scanner_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return pattern_scanner_bound(runtime, args, argc, kwargs, kwargc, out, error);
}

bool finditer_iter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SRE_FindIter.__iter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool finditer_next(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "SRE_FindIter.__next__() expected no arguments";
    return false;
  }
  auto* state = static_cast<FindIterState*>(instance_get_native_data(args[0], kFindIterNativeType));
  if (state == nullptr) {
    error = "invalid SRE finditer object";
    return false;
  }
  auto* pattern = pattern_state(state->pattern, error);
  if (pattern == nullptr) {
    return false;
  }
  if (!ensure_pattern_regex(*pattern, error)) {
    return false;
  }
  while (state->cursor <= state->endpos && state->cursor <= state->text.size()) {
    std::match_results<std::string::const_iterator> match;
    auto begin = state->text.cbegin() + static_cast<std::ptrdiff_t>(state->cursor);
    auto end = state->text.cbegin() + static_cast<std::ptrdiff_t>(state->endpos);
    auto search_flags = state->cursor == 0 ? std::regex_constants::match_default
                                           : std::regex_constants::match_prev_avail | std::regex_constants::match_not_bol;
    if (state->retry_nonempty_at_cursor) search_flags |= std::regex_constants::match_not_null;
    const bool found = std::regex_search(begin, end, match, pattern->regex, search_flags);
    if (!found || (state->retry_nonempty_at_cursor && match.position(0) != 0)) {
      if (state->retry_nonempty_at_cursor && state->cursor < state->endpos) {
        ++state->cursor;
        state->retry_nonempty_at_cursor = false;
        continue;
      }
      break;
    }
    const size_t start = state->cursor + static_cast<size_t>(match.position(0));
    const size_t match_end = start + static_cast<size_t>(match.length(0));
    if (!match_satisfies_lookbehinds(*pattern, state->text, match, state->cursor)) {
      state->cursor = match_end > start ? match_end : start + 1;
      continue;
    }
    out = make_match(runtime, state->pattern, state->text, state->bytes_text, match,
                     state->cursor, state->pos, state->endpos, &state->subject);
    state->cursor = match_end;
    state->retry_nonempty_at_cursor = start == match_end;
    return true;
  }
  if (state->exports_bytearray) {
    if (auto* bytearray = value_as_bytearray(state->subject); bytearray != nullptr && bytearray->buffer_exports > 0) {
      --bytearray->buffer_exports;
    }
    state->exports_bytearray = false;
  }
  runtime.raise_class_error("StopIteration", "");
  return false;
}

Value make_finditer_type(Runtime& runtime) {
  static Value finditer_type = Value::invalid();
  if (finditer_type.tag != ValueTag::Invalid) {
    return finditer_type;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_sre")});
  attrs.push_back({"__iter__", runtime.make_native_function("_sre.FindIter.__iter__", finditer_iter)});
  attrs.push_back({"__next__", runtime.make_native_function("_sre.FindIter.__next__", finditer_next)});
  finditer_type = Value::class_object("SRE_FindIter", std::move(attrs));
  return finditer_type;
}

bool pattern_finditer(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "Pattern.finditer() expected string and optional positions";
    return false;
  }
  auto* pattern = pattern_state(args[0], error);
  if (pattern == nullptr) {
    return false;
  }
  if (!ensure_pattern_regex(*pattern, error)) {
    return false;
  }
  if (pattern_anchored_literal_miss(*pattern, args[1])) {
    auto* state = new FindIterState();
    state->pattern = args[0];
    state->subject = args[1];
    if (auto* bytearray = value_as_bytearray(state->subject)) {
      ++bytearray->buffer_exports;
      state->exports_bytearray = true;
    }
    state->cursor = 1;
    out = Value::instance(make_finditer_type(runtime));
    if (!instance_set_native_data(out, kFindIterNativeType, state, finditer_cleanup, error)) {
      finditer_cleanup(state);
      return false;
    }
    return true;
  }
  std::string text;
  bool bytes_text = false;
  if (!value_to_match_text(args[1], text, bytes_text) || bytes_text != pattern->bytes_pattern) {
    error = "expected matching string/bytes object";
    return false;
  }
  size_t pos = 0;
  if (argc >= 3 && args[2].tag == ValueTag::Int64 && args[2].as.i64 > 0) {
    pos = std::min(static_cast<size_t>(args[2].as.i64), text.size());
  }
  size_t endpos = text.size();
  if (argc >= 4 && args[3].tag == ValueTag::Int64) {
    endpos = args[3].as.i64 < 0 ? 0 : std::min(static_cast<size_t>(args[3].as.i64), text.size());
  }
  auto* state = new FindIterState();
  state->pattern = args[0];
  state->subject = args[1];
  if (auto* bytearray = value_as_bytearray(state->subject)) {
    ++bytearray->buffer_exports;
    state->exports_bytearray = true;
  }
  state->text = std::move(text);
  state->bytes_text = bytes_text;
  state->pos = pos;
  state->cursor = pos;
  state->endpos = endpos;
  out = Value::instance(make_finditer_type(runtime));
  if (!instance_set_native_data(out, kFindIterNativeType, state, finditer_cleanup, error)) {
    finditer_cleanup(state);
    return false;
  }
  return true;
}

bool pattern_finditer_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 4) {
    error = "Pattern.finditer() expected string and optional positions";
    return false;
  }
  Value values[] = {args[0], Value::invalid(), Value::int64(0), Value::invalid()};
  bool present[] = {true, false, false, false};
  for (uint32_t i = 1; i < argc; ++i) {
    value_assign_fast(values[i], args[i]);
    present[i] = true;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "Pattern.finditer() received invalid keyword argument";
      return false;
    }
    const std::string_view name(kwargs[i].name);
    const uint32_t slot = name == "string" ? 1u : name == "pos" ? 2u : name == "endpos" ? 3u : 4u;
    if (slot == 4 || present[slot]) {
      error = slot == 4 ? "Pattern.finditer() got an unexpected keyword argument"
                        : "Pattern.finditer() got multiple values for argument";
      return false;
    }
    value_assign_fast(values[slot], *kwargs[i].value);
    present[slot] = true;
  }
  if (!present[1]) {
    error = "Pattern.finditer() missing required argument 'string'";
    return false;
  }
  const uint32_t final_argc = present[3] ? 4u : present[2] ? 3u : 2u;
  return pattern_finditer(runtime, values, final_argc, out, error, nullptr);
}

bool pattern_findall(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 4) {
    error = "Pattern.findall() expected string and optional positions";
    return false;
  }
  auto* pattern = pattern_state(args[0], error);
  if (pattern == nullptr) {
    return false;
  }
  if (!ensure_pattern_regex(*pattern, error)) {
    return false;
  }
  if (pattern_anchored_literal_miss(*pattern, args[1])) {
    out = Value::list({});
    return true;
  }
  std::string text;
  bool bytes_text = false;
  if (!value_to_match_text(args[1], text, bytes_text) || bytes_text != pattern->bytes_pattern) {
    error = "expected matching string/bytes object";
    return false;
  }
  size_t cursor = 0;
  if (argc >= 3 && args[2].tag == ValueTag::Int64 && args[2].as.i64 > 0) {
    cursor = std::min(static_cast<size_t>(args[2].as.i64), text.size());
  }
  size_t endpos = text.size();
  if (argc >= 4 && args[3].tag == ValueTag::Int64) {
    endpos = args[3].as.i64 < 0 ? 0 : std::min(static_cast<size_t>(args[3].as.i64), text.size());
  }
  std::vector<Value> results;
  bool retry_nonempty_at_cursor = false;
  while (cursor <= endpos && cursor <= text.size()) {
    std::match_results<std::string::const_iterator> match;
    auto begin = text.cbegin() + static_cast<std::ptrdiff_t>(cursor);
    auto end = text.cbegin() + static_cast<std::ptrdiff_t>(endpos);
    auto search_flags = cursor == 0 ? std::regex_constants::match_default
                                    : std::regex_constants::match_prev_avail | std::regex_constants::match_not_bol;
    if (retry_nonempty_at_cursor) search_flags |= std::regex_constants::match_not_null;
    const bool found = std::regex_search(begin, end, match, pattern->regex, search_flags);
    if (!found || (retry_nonempty_at_cursor && match.position(0) != 0)) {
      if (retry_nonempty_at_cursor && cursor < endpos) {
        ++cursor;
        retry_nonempty_at_cursor = false;
        continue;
      }
      break;
    }
    const size_t start = cursor + static_cast<size_t>(match.position(0));
    const size_t match_end = start + static_cast<size_t>(match.length(0));
    if (!match_satisfies_lookbehinds(*pattern, text, match, cursor)) {
      cursor = match_end > start ? match_end : start + 1;
      continue;
    }
    if (pattern->group_count <= 0) {
      std::string found = match[0].str();
      results.push_back(bytes_text ? Value::bytes(std::move(found)) : Value::string(std::move(found)));
    } else if (pattern->group_count == 1) {
      const size_t engine_group = pattern->engine_group_for_python.size() > 1
          ? pattern->engine_group_for_python[1] : 1;
      std::string found = engine_group < match.size() && match[engine_group].matched
          ? match[engine_group].str() : std::string();
      results.push_back(bytes_text ? Value::bytes(std::move(found)) : Value::string(std::move(found)));
    } else {
      std::vector<Value> groups;
      groups.reserve(static_cast<size_t>(pattern->group_count));
      for (size_t i = 1; i <= static_cast<size_t>(pattern->group_count); ++i) {
        const size_t engine_group = i < pattern->engine_group_for_python.size()
            ? pattern->engine_group_for_python[i] : i;
        std::string found = engine_group < match.size() && match[engine_group].matched
            ? match[engine_group].str() : std::string();
        groups.push_back(bytes_text ? Value::bytes(std::move(found)) : Value::string(std::move(found)));
      }
      results.push_back(Value::tuple(std::move(groups)));
    }
    cursor = match_end;
    retry_nonempty_at_cursor = start == match_end;
  }
  out = Value::list(std::move(results));
  return true;
}

bool pattern_findall_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  Value values[4];
  uint32_t bound_argc = 0;
  if (!bind_pattern_string_positions("Pattern.findall", args, argc, kwargs, kwargc, values, bound_argc, error)) {
    return false;
  }
  return pattern_findall(runtime, values, bound_argc, out, error, nullptr);
}

std::string expand_replacement_template(
    std::string_view replacement,
    const std::match_results<std::string::const_iterator>& match);

bool pattern_sub(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 3 || argc > 4) {
    error = "Pattern.sub() expected replacement, string, and optional count";
    return false;
  }
  auto* state = pattern_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (!sre_value_is_callable(args[1])) {
    Value parser_module;
    if (!runtime.import_module("re._parser", parser_module, error)) {
      return false;
    }
    Value parse_template;
    if (!module_get_attr(parser_module, "parse_template", parse_template, error)) {
      return false;
    }
    std::string validation_text;
    bool validation_bytes = false;
    if (!value_to_match_text(args[1], validation_text, validation_bytes)) {
      error = "replacement must be matching string/bytes object";
      return false;
    }
    Value validation_replacement = validation_bytes
        ? Value::bytes(std::move(validation_text))
        : Value::string(std::move(validation_text));
    Value validation_args[] = {validation_replacement, args[0]};
    Value validated;
    error.clear();
    if (!runtime_call_callable(runtime, parse_template, validation_args, 2, validated, error)) {
      return false;
    }
    error.clear();
  }
  if (pattern_anchored_literal_miss(*state, args[2])) {
    if (user_data != nullptr) {
      out = Value::tuple({args[2], Value::int64(0)});
    } else {
      value_assign_fast(out, args[2]);
    }
    return true;
  }
  std::string text;
  bool bytes_text = false;
  if (!value_to_match_text(args[2], text, bytes_text) || bytes_text != state->bytes_pattern) {
    error = "expected matching string/bytes object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t max_count = 0;
  if (argc == 4 && !value_int_like_to_i64(args[3], max_count)) {
    error = "count must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!ensure_pattern_regex(*state, error)) {
    return false;
  }
  std::string output;
  size_t cursor = 0;
  size_t segment_start = 0;
  int64_t replacements = 0;
  bool retry_nonempty_at_cursor = false;
  const auto shared_text = std::make_shared<const std::string>(text);
  const bool ascii_text = bytes_text || std::all_of(
      text.begin(), text.end(), [](char ch) {
        return static_cast<unsigned char>(ch) < 0x80u;
      });
  std::match_results<std::string::const_iterator> match;
  while (cursor <= text.size() && (max_count <= 0 || replacements < max_count)) {
    auto search_flags = cursor == 0 ? std::regex_constants::match_default
                                    : std::regex_constants::match_prev_avail | std::regex_constants::match_not_bol;
    if (retry_nonempty_at_cursor) search_flags |= std::regex_constants::match_not_null;
    const bool found = std::regex_search(
        text.cbegin() + static_cast<std::ptrdiff_t>(cursor), text.cend(), match,
        state->regex, search_flags);
    if (!found || (retry_nonempty_at_cursor && match.position(0) != 0)) {
      if (retry_nonempty_at_cursor && cursor < text.size()) {
        ++cursor;
        retry_nonempty_at_cursor = false;
        continue;
      }
      break;
    }
    const size_t start = cursor + static_cast<size_t>(match.position(0));
    size_t end = start + static_cast<size_t>(match.length(0));
    if (!bytes_text) {
      while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0u) == 0x80u) {
        ++end;
      }
    }
    if (!match_satisfies_lookbehinds(*state, text, match, cursor)) {
      cursor = end > start ? end : start + 1;
      continue;
    }
    output.append(text, segment_start, start - segment_start);
    if (sre_value_is_callable(args[1])) {
      Value match_value = make_match(
          runtime, args[0], text, bytes_text, match, cursor, 0, text.size(),
          &args[2], shared_text, ascii_text);
      Value replacement;
      if (!runtime_call_callable(runtime, args[1], &match_value, 1, replacement, error)) {
        return false;
      }
      std::string replacement_text;
      bool replacement_bytes = false;
      if (!value_to_match_text(replacement, replacement_text, replacement_bytes) || replacement_bytes != bytes_text) {
        error = "replacement must return matching string/bytes object";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      output.append(replacement_text);
    } else {
      std::string replacement_text;
      bool replacement_bytes = false;
      if (!value_to_match_text(args[1], replacement_text, replacement_bytes) || replacement_bytes != bytes_text) {
        error = "replacement must be matching string/bytes object";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      Value match_value = make_match(runtime, args[0], text, bytes_text, match, cursor, 0, text.size());
      auto* match_data = match_state(match_value, error);
      std::string expanded;
      if (match_data == nullptr || !expand_match_template(*match_data, replacement_text, expanded, error)) {
        return false;
      }
      output.append(expanded);
    }
    segment_start = end;
    cursor = end;
    ++replacements;
    retry_nonempty_at_cursor = start == end;
  }
  output.append(text, segment_start, std::string::npos);
  Value replaced = bytes_text ? Value::bytes(std::move(output)) : Value::string(std::move(output));
  if (user_data != nullptr) {
    out = Value::tuple({std::move(replaced), Value::int64(replacements)});
  } else {
    out = std::move(replaced);
  }
  return true;
}

std::string expand_replacement_template(
    std::string_view replacement,
    const std::match_results<std::string::const_iterator>& match) {
  std::string expanded;
  expanded.reserve(replacement.size());
  for (size_t i = 0; i < replacement.size(); ++i) {
    const char ch = replacement[i];
    if (ch != '\\' || i + 1 >= replacement.size()) {
      expanded.push_back(ch);
      continue;
    }
    const char next = replacement[++i];
    if (next >= '0' && next <= '9') {
      size_t group = static_cast<size_t>(next - '0');
      while (i + 1 < replacement.size() && replacement[i + 1] >= '0' && replacement[i + 1] <= '9') {
        group = group * 10 + static_cast<size_t>(replacement[++i] - '0');
      }
      if (group < match.size() && match[group].matched) {
        expanded.append(match[group].first, match[group].second);
      }
      continue;
    }
    switch (next) {
      case 'n':
        expanded.push_back('\n');
        break;
      case 'r':
        expanded.push_back('\r');
        break;
      case 't':
        expanded.push_back('\t');
        break;
      case '\\':
        expanded.push_back('\\');
        break;
      default:
        expanded.push_back(next);
        break;
    }
  }
  return expanded;
}

bool pattern_split(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "Pattern.split() expected string and optional maxsplit";
    return false;
  }
  auto* state = pattern_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (pattern_anchored_literal_miss(*state, args[1])) {
    out = Value::list({args[1]});
    return true;
  }
  std::string text;
  bool bytes_text = false;
  if (!value_to_match_text(args[1], text, bytes_text) || bytes_text != state->bytes_pattern) {
    error = "expected matching string/bytes object";
    return false;
  }
  int64_t maxsplit = 0;
  if (argc == 3 && !value_int_like_to_i64(args[2], maxsplit)) {
    error = "maxsplit must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (!ensure_pattern_regex(*state, error)) {
    return false;
  }
  std::vector<Value> parts;
  size_t cursor = 0;
  size_t segment_start = 0;
  int64_t splits = 0;
  bool retry_nonempty_at_cursor = false;
  std::match_results<std::string::const_iterator> match;
  while (cursor <= text.size() && (maxsplit <= 0 || splits < maxsplit)) {
    auto search_flags = cursor == 0 ? std::regex_constants::match_default
                                    : std::regex_constants::match_prev_avail;
    if (retry_nonempty_at_cursor) search_flags |= std::regex_constants::match_not_null;
    const bool found = std::regex_search(
        text.cbegin() + static_cast<std::ptrdiff_t>(cursor), text.cend(), match,
        state->regex, search_flags);
    if (!found || (retry_nonempty_at_cursor && match.position(0) != 0)) {
      if (retry_nonempty_at_cursor && cursor < text.size()) {
        ++cursor;
        retry_nonempty_at_cursor = false;
        continue;
      }
      break;
    }
    const size_t start = cursor + static_cast<size_t>(match.position(0));
    size_t end = start + static_cast<size_t>(match.length(0));
    if (!match_satisfies_lookbehinds(*state, text, match, cursor)) {
      if (!regex_retry_longer_match_at_same_start(*state, text, start, end, text.size(), match)) {
        cursor = end > start ? end : start + 1;
        continue;
      }
      end = start + static_cast<size_t>(match.length(0));
    }
    std::string part = text.substr(segment_start, start - segment_start);
    parts.push_back(bytes_text ? Value::bytes(std::move(part)) : Value::string(std::move(part)));
    for (size_t i = 1; i <= static_cast<size_t>(state->group_count); ++i) {
      const size_t engine_group = i < state->engine_group_for_python.size()
          ? state->engine_group_for_python[i] : i;
      if (engine_group < match.size() && match[engine_group].matched) {
        std::string group = match[engine_group].str();
        parts.push_back(bytes_text ? Value::bytes(std::move(group)) : Value::string(std::move(group)));
      } else {
        parts.push_back(Value::none());
      }
    }
    segment_start = end;
    cursor = end;
    ++splits;
    if (start == end) {
      retry_nonempty_at_cursor = true;
    } else {
      retry_nonempty_at_cursor = false;
    }
  }
  std::string tail = text.substr(segment_start);
  parts.push_back(bytes_text ? Value::bytes(std::move(tail)) : Value::string(std::move(tail)));
  out = Value::list(std::move(parts));
  return true;
}

bool pattern_split_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1 || argc > 3) {
    error = "Pattern.split() expected string and optional maxsplit";
    return false;
  }
  Value values[] = {args[0], Value::invalid(), Value::int64(0)};
  bool present[] = {true, false, false};
  for (uint32_t i = 1; i < argc; ++i) {
    value_assign_fast(values[i], args[i]);
    present[i] = true;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      error = "Pattern.split() received invalid keyword argument";
      return false;
    }
    const std::string_view name(kwargs[i].name);
    const uint32_t slot = name == "string" ? 1u : name == "maxsplit" ? 2u : 3u;
    if (slot == 3 || present[slot]) {
      error = slot == 3 ? "Pattern.split() got an unexpected keyword argument '" + std::string(name) + "'"
                        : "Pattern.split() got multiple values for argument '" + std::string(name) + "'";
      return false;
    }
    value_assign_fast(values[slot], *kwargs[i].value);
    present[slot] = true;
  }
  if (!present[1]) {
    error = "Pattern.split() missing required argument 'string'";
    return false;
  }
  return pattern_split(runtime, values, present[2] ? 3u : 2u, out, error, nullptr);
}

std::string pattern_text_repr(const PatternState& state) {
  const char quote = state.pattern.find('\'') != std::string::npos && state.pattern.find('"') == std::string::npos ? '"' : '\'';
  std::string text = state.bytes_pattern ? "b" : "";
  text.push_back(quote);
  for (const unsigned char ch : state.pattern) {
    if (ch == '\\' || ch == static_cast<unsigned char>(quote)) {
      text.push_back('\\');
      text.push_back(static_cast<char>(ch));
    } else if (ch == '\n') {
      text += "\\n";
    } else if (ch == '\r') {
      text += "\\r";
    } else if (ch == '\t') {
      text += "\\t";
    } else if (ch < 32 || (state.bytes_pattern && ch >= 127)) {
      constexpr char hex[] = "0123456789abcdef";
      text += "\\x";
      text.push_back(hex[ch >> 4]);
      text.push_back(hex[ch & 15]);
    } else {
      text.push_back(static_cast<char>(ch));
    }
  }
  text.push_back(quote);
  if (text.size() > 214) {
    text.resize(211);
    text += "...";
  }
  return text;
}

bool pattern_repr(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Pattern.__repr__() expected no arguments";
    return false;
  }
  auto* state = pattern_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string text = "re.compile(" + pattern_text_repr(*state);
  int64_t remaining = state->flags;
  if (!state->bytes_pattern) remaining &= ~int64_t{32};
  std::vector<std::string> names;
  const std::pair<int64_t, const char*> known[] = {
      {2, "re.IGNORECASE"}, {4, "re.LOCALE"}, {8, "re.MULTILINE"},
      {16, "re.DOTALL"}, {64, "re.VERBOSE"}, {128, "re.DEBUG"}, {256, "re.ASCII"},
  };
  for (const auto& [flag, name] : known) {
    if ((remaining & flag) != 0) {
      names.emplace_back(name);
      remaining &= ~flag;
    }
  }
  if (remaining != 0) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%llx", static_cast<unsigned long long>(remaining));
    names.emplace_back(buffer);
  }
  if (!names.empty()) {
    text += ", ";
    for (size_t i = 0; i < names.size(); ++i) {
      if (i != 0) text.push_back('|');
      text += names[i];
    }
  }
  text.push_back(')');
  out = Value::string(std::move(text));
  return true;
}

bool pattern_copy(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "Pattern copy expected optional memo";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool pattern_compare(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "Pattern comparison expected one argument";
    return false;
  }
  auto* lhs = pattern_state(args[0], error);
  if (lhs == nullptr) {
    return false;
  }
  std::string ignored;
  auto* rhs = pattern_state(args[1], ignored);
  const bool equal = rhs != nullptr && lhs->pattern == rhs->pattern &&
      lhs->bytes_pattern == rhs->bytes_pattern && lhs->flags == rhs->flags;
  out = Value::boolean(user_data == nullptr ? equal : !equal);
  return true;
}

bool pattern_hash(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Pattern.__hash__() expected no arguments";
    return false;
  }
  auto* state = pattern_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  size_t hash = std::hash<std::string>{}(state->pattern);
  hash ^= std::hash<int64_t>{}(state->flags) + size_t{0x9e3779b9} + (hash << 6) + (hash >> 2);
  hash ^= std::hash<bool>{}(state->bytes_pattern) + size_t{0x9e3779b9} + (hash << 6) + (hash >> 2);
  out = Value::int64(static_cast<int64_t>(hash));
  return true;
}

bool pattern_reduce(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "Pattern.__reduce__() expected no arguments";
    return false;
  }
  auto* state = pattern_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  Value re_module;
  if (!runtime.import_module("re", re_module, error)) {
    return false;
  }
  Value compile;
  if (!module_get_attr(re_module, "_compile", compile, error)) {
    return false;
  }
  Value source = state->bytes_pattern ? Value::bytes(state->pattern) : Value::string(state->pattern);
  out = Value::tuple({compile, Value::tuple({source, Value::int64(state->flags)})});
  return true;
}

Value make_pattern_type(Runtime& runtime) {
  static Value pattern_type = Value::invalid();
  if (pattern_type.tag != ValueTag::Invalid) {
    return pattern_type;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_sre")});
  attrs.push_back({"__repr__", runtime.make_native_function("_sre.Pattern.__repr__", pattern_repr)});
  attrs.push_back({"__eq__", runtime.make_native_function("_sre.Pattern.__eq__", pattern_compare)});
  attrs.push_back({"__ne__", runtime.make_native_function("_sre.Pattern.__ne__", pattern_compare, reinterpret_cast<void*>(1))});
  attrs.push_back({"__hash__", runtime.make_native_function("_sre.Pattern.__hash__", pattern_hash)});
  attrs.push_back({"__reduce__", runtime.make_native_function("_sre.Pattern.__reduce__", pattern_reduce)});
  attrs.push_back({"__copy__", runtime.make_native_function("_sre.Pattern.__copy__", pattern_copy)});
  attrs.push_back({"__deepcopy__", runtime.make_native_function("_sre.Pattern.__deepcopy__", pattern_copy)});
  attrs.push_back({"match", runtime.make_native_function("_sre.Pattern.match", pattern_match,
      nullptr, nullptr, nullptr, false, pattern_match_kw)});
  attrs.push_back({"search", runtime.make_native_function("_sre.Pattern.search", pattern_search,
      reinterpret_cast<void*>(1), nullptr, nullptr, false, pattern_match_kw)});
  attrs.push_back({"fullmatch", runtime.make_native_function("_sre.Pattern.fullmatch", pattern_fullmatch,
      reinterpret_cast<void*>(2), nullptr, nullptr, false, pattern_match_kw)});
  attrs.push_back({"finditer", runtime.make_native_function("_sre.Pattern.finditer", pattern_finditer,
      nullptr, nullptr, nullptr, false, pattern_finditer_kw)});
  attrs.push_back({"scanner", runtime.make_native_function("_sre.Pattern.scanner", pattern_scanner,
      nullptr, nullptr, nullptr, false, pattern_scanner_kw)});
  attrs.push_back({"findall", runtime.make_native_function("_sre.Pattern.findall", pattern_findall,
      nullptr, nullptr, nullptr, false, pattern_findall_kw)});
  attrs.push_back({"sub", runtime.make_native_function("_sre.Pattern.sub", pattern_sub)});
  attrs.push_back({"subn", runtime.make_native_function("_sre.Pattern.subn", pattern_sub, reinterpret_cast<void*>(1))});
  attrs.push_back({"split", runtime.make_native_function("_sre.Pattern.split", pattern_split,
      nullptr, nullptr, nullptr, false, pattern_split_kw)});
  pattern_type = Value::class_object("SRE_Pattern", std::move(attrs));
  return pattern_type;
}

std::string sre_code_literal(uint32_t codepoint) {
  static constexpr char digits[] = "0123456789abcdef";
  if (codepoint <= 0xffu) {
    std::string escaped = "\\x00";
    escaped[2] = digits[(codepoint >> 4u) & 0x0fu];
    escaped[3] = digits[codepoint & 0x0fu];
    return escaped;
  }
  return std::string();
}

bool translate_sre_code_sequence(
    const std::vector<uint32_t>& code,
    size_t begin,
    size_t end,
    std::string& out,
    std::string& error);

bool translate_sre_code_in(
    const std::vector<uint32_t>& code,
    size_t begin,
    size_t end,
    std::string& out,
    std::string& error) {
  if (begin >= end) {
    error = "empty SRE character set";
    return false;
  }
  if (code[begin] == 8 && begin + 1 < end) { // CATEGORY
    switch (code[begin + 1]) {
      case 0: out += "\\d"; return true; // CATEGORY_DIGIT
      case 2: out += "\\s"; return true; // CATEGORY_SPACE
      case 4: out += "\\w"; return true; // CATEGORY_WORD
      default: break;
    }
  }
  if (code[begin] == 9 && begin + 9 <= end) { // CHARSET
    out.push_back('[');
    for (uint32_t ch = 0; ch < 256; ++ch) {
      const uint32_t word = code[begin + 1 + ch / 32u];
      if ((word & (uint32_t{1} << (ch % 32u))) != 0) {
        out += sre_code_literal(ch);
      }
    }
    out.push_back(']');
    return true;
  }
  error = "unsupported SRE character-set opcode";
  return false;
}

bool translate_sre_code_sequence(
    const std::vector<uint32_t>& code,
    size_t begin,
    size_t end,
    std::string& out,
    std::string& error) {
  size_t i = begin;
  while (i < end) {
    const uint32_t opcode = code[i];
    if (opcode == 17) { // MARK
      i += 2;
      continue;
    }
    if (opcode == 16 && i + 1 < end) { // LITERAL
      std::string literal = sre_code_literal(code[i + 1]);
      if (literal.empty()) {
        error = "unsupported non-ASCII SRE literal opcode";
        return false;
      }
      out += literal;
      i += 2;
      continue;
    }
    if (opcode == 13 && i + 1 < end) { // IN
      const size_t next = i + 1 + code[i + 1];
      if (next > end || next < i + 3 || !translate_sre_code_in(code, i + 2, next - 1, out, error)) {
        return false;
      }
      i = next;
      continue;
    }
    if (opcode == 24 && i + 4 < end) { // REPEAT_ONE
      const size_t next = i + 1 + code[i + 1];
      if (next > end || next < i + 6) {
        error = "invalid SRE repeat opcode";
        return false;
      }
      std::string repeated;
      if (!translate_sre_code_sequence(code, i + 4, next - 1, repeated, error)) {
        return false;
      }
      out += "(?:" + repeated + ")";
      const uint32_t minimum = code[i + 2];
      const uint32_t maximum = code[i + 3];
      if (minimum == 0 && maximum == 0xffffffffu) out.push_back('*');
      else if (minimum == 1 && maximum == 0xffffffffu) out.push_back('+');
      else if (minimum == 0 && maximum == 1) out.push_back('?');
      else {
        out += "{" + std::to_string(minimum);
        if (maximum != minimum) {
          out.push_back(',');
          if (maximum != 0xffffffffu) out += std::to_string(maximum);
        }
        out.push_back('}');
      }
      i = next;
      continue;
    }
    if (opcode == 1) { // SUCCESS
      ++i;
      continue;
    }
    error = "unsupported SRE opcode " + std::to_string(opcode);
    return false;
  }
  return true;
}

bool translate_scanner_sre_code(const Value& code_value, std::string& out, std::string& error) {
  auto* list = value_as_list(code_value);
  if (list == nullptr) {
    error = "_sre.compile() code argument must be a list";
    return false;
  }
  std::vector<uint32_t> code;
  code.reserve(list->items.size());
  for (const Value& item : list->items) {
    int64_t opcode = 0;
    if (!value_int_like_to_i64(item, opcode) || opcode < 0 || opcode > 0xffffffffll) {
      error = "_sre.compile() code list contains a non-integer opcode";
      return false;
    }
    code.push_back(static_cast<uint32_t>(opcode));
  }
  size_t i = 0;
  if (code.size() >= 2 && code[0] == 14) { // INFO
    i = 1 + code[1];
  }
  if (i >= code.size() || code[i] != 7) { // BRANCH
    error = "unsupported constructed SRE program";
    return false;
  }
  ++i;
  bool first = true;
  while (i < code.size() && code[i] != 0) { // FAILURE terminates branches
    const size_t next = i + code[i];
    if (next > code.size() || next <= i + 1) {
      error = "invalid SRE branch opcode";
      return false;
    }
    size_t branch_end = next;
    if (branch_end >= 2 && code[branch_end - 2] == 15) { // JUMP
      branch_end -= 2;
    }
    std::string branch;
    if (!translate_sre_code_sequence(code, i + 1, branch_end, branch, error)) {
      return false;
    }
    if (!first) out.push_back('|');
    out += "(" + branch + ")";
    first = false;
    i = next;
  }
  return !first;
}

bool sre_compile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 6) {
    error = "_sre.compile() expected pattern, flags, code, groups, groupindex, indexgroup";
    return false;
  }
  std::string pattern;
  bool bytes_pattern = false;
  if (!value_to_pattern_text(args[0], pattern, bytes_pattern)) {
    if (args[0].tag != ValueTag::None || !translate_scanner_sre_code(args[2], pattern, error)) {
      if (error.empty()) error = "_sre.compile() requires a string or bytes pattern";
      return false;
    }
  }
  int64_t flags = args[1].tag == ValueTag::Int64 ? args[1].as.i64 : 0;
  std::unordered_map<std::string, int64_t> group_names;
  if (auto* groupindex = value_as_dict(args[4])) {
    for (const auto& entry : groupindex->entries) {
      auto* key = value_as_string(entry.first);
      if (key != nullptr && entry.second.tag == ValueTag::Int64) {
        group_names[string_object_to_string(*key)] = entry.second.as.i64;
      }
    }
  }
  size_t dot_repeat_lookbehind_width = 0;
  bool dot_repeat_lookbehind_positive = false;
  const bool dot_repeat_lookbehind = regex_dot_repeat_lookbehind_width(
      pattern, dot_repeat_lookbehind_width, dot_repeat_lookbehind_positive);
  const bool unsupported = regex_has_unsupported_std_construct(pattern) && !dot_repeat_lookbehind;
  std::string engine_pattern = unsupported || dot_repeat_lookbehind ? std::string() : pattern;
  std::vector<LookbehindAssertion> lookbehinds;
  std::vector<BoundaryAssertion> boundaries;
  bool requires_absolute_start = false;
  bool requires_absolute_end = false;
  if (!unsupported) {
    engine_pattern = normalize_std_regex_pattern(engine_pattern, &group_names, &lookbehinds,
                                                 (flags & kFlagDotAll) != 0, (flags & kFlagIgnoreCase) != 0,
                                                 (flags & kFlagVerbose) != 0, (flags & kFlagMultiline) != 0,
                                                 !bytes_pattern, !bytes_pattern && (flags & 256) == 0,
                                                 &requires_absolute_start, &requires_absolute_end, &boundaries);
  }
  std::regex::flag_type regex_flags = std::regex::ECMAScript;
  if ((flags & kFlagIgnoreCase) != 0 && pattern.find("(?-i:") == std::string::npos) {
    regex_flags |= std::regex::icase;
  }
  if ((flags & kFlagMultiline) != 0) {
    regex_flags |= std::regex_constants::multiline;
  }
  auto* state = new PatternState();
  state->pattern = pattern;
  state->engine_pattern = std::move(engine_pattern);
  state->bytes_pattern = bytes_pattern;
  state->flags = flags;
  state->regex_available = !unsupported;
  state->regex_flags = regex_flags;
  state->group_count = args[3].tag == ValueTag::Int64 ? args[3].as.i64 : 0;
  state->engine_group_for_python.resize(
      static_cast<size_t>(std::max<int64_t>(0, state->group_count)) + 1);
  for (size_t python_group = 0; python_group < state->engine_group_for_python.size(); ++python_group) {
    size_t engine_group = python_group;
    for (const auto& assertion : lookbehinds) {
      if (assertion.marker_group > 0 &&
          assertion.captures_before_assertion < static_cast<int64_t>(python_group)) {
        ++engine_group;
      }
    }
    state->engine_group_for_python[python_group] = engine_group;
  }
  state->group_close_order = regex_group_close_order(pattern);
  state->fast_kind = detect_fast_ordered_suffix(pattern, state->fast_literals)
      ? FastRegexKind::OrderedSuffix
      : detect_fast_regex(state->engine_pattern, state->fast_literal);
  state->group_names = std::move(group_names);
  state->lookbehinds = std::move(lookbehinds);
  state->boundaries = std::move(boundaries);
  state->requires_absolute_start = requires_absolute_start;
  state->requires_absolute_end = requires_absolute_end;
  if (dot_repeat_lookbehind && dot_repeat_lookbehind_positive) {
    state->minimum_match_start = dot_repeat_lookbehind_width;
  }
  try {
    out = Value::instance(make_pattern_type(runtime));
    std::string native_error;
    if (!instance_set_native_data(out, kPatternNativeType, state, pattern_cleanup, native_error)) {
      delete state;
      error = native_error;
      return false;
    }
    (void)object_set_attr(out, "pattern", args[0].tag == ValueTag::None ? Value::string(pattern) : args[0], native_error);
    (void)object_set_attr(out, "flags", Value::int64(flags), native_error);
    (void)object_set_attr(out, "groups", args[3], native_error);
    (void)object_set_attr(out, "groupindex", mapping_proxy(args[4]), native_error);
    return true;
  } catch (const std::regex_error& exc) {
    error = std::string("bad regex pattern '") + pattern + "': " + exc.what();
    return false;
  }
}

bool sre_template(Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
  out = Value::none();
  return true;
}

bool sre_ascii_iscased(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "_sre.ascii_iscased() expected int";
    return false;
  }
  const unsigned char ch = static_cast<unsigned char>(args[0].as.i64);
  value_set_bool(out, std::isalpha(ch) != 0);
  return true;
}

bool sre_ascii_tolower(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1 || args[0].tag != ValueTag::Int64) {
    error = "_sre.ascii_tolower() expected int";
    return false;
  }
  value_set_int64(out, std::tolower(static_cast<unsigned char>(args[0].as.i64)));
  return true;
}

bool sre_unicode_iscased(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  return sre_ascii_iscased(runtime, args, argc, out, error, data);
}

bool sre_unicode_tolower(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  return sre_ascii_tolower(runtime, args, argc, out, error, data);
}

} // namespace

void register_sre_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_sre");
  builder.value("MAGIC", Value::int64(kSreMagic))
      .value("CODESIZE", Value::int64(kSreCodeSize))
      .value("MAXREPEAT", Value::int64(4294967295LL))
      .value("MAXGROUPS", Value::int64(1073741823))
      .function("compile", sre_compile)
      .function("template", sre_template)
      .function("ascii_iscased", sre_ascii_iscased)
      .function("ascii_tolower", sre_ascii_tolower)
      .function("unicode_iscased", sre_unicode_iscased)
      .function("unicode_tolower", sre_unicode_tolower);
  runtime.register_module("_sre", builder.finish());
}

} // namespace xlang3
