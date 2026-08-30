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
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include <cctype>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kPatternNativeType = "_sre.Pattern";
constexpr const char* kMatchNativeType = "_sre.Match";
constexpr int64_t kSreMagic = 20230612;
constexpr int64_t kSreCodeSize = 4;
constexpr int64_t kFlagIgnoreCase = 2;
constexpr int64_t kFlagMultiline = 8;
constexpr int64_t kFlagDotAll = 16;
constexpr int64_t kFlagVerbose = 64;

struct PatternState {
  std::string pattern;
  bool bytes_pattern = false;
  int64_t flags = 0;
  std::regex regex;
};

struct MatchGroup {
  int64_t start = -1;
  int64_t end = -1;
  bool matched = false;
};

struct MatchState {
  Value pattern;
  std::string text;
  bool bytes_text = false;
  std::vector<MatchGroup> groups;
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
  return false;
}

bool value_to_match_text(const Value& value, std::string& out, bool& is_bytes) {
  return value_to_pattern_text(value, out, is_bytes);
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

Value match_group_value(const MatchState& state, size_t index) {
  if (index >= state.groups.size() || !state.groups[index].matched) {
    return Value::none();
  }
  const auto& group = state.groups[index];
  const size_t start = static_cast<size_t>(group.start);
  const size_t count = static_cast<size_t>(group.end - group.start);
  std::string text = state.text.substr(start, count);
  return state.bytes_text ? Value::bytes(std::move(text)) : Value::string(std::move(text));
}

Value make_match_type(Runtime& runtime) {
  static Value match_type = Value::invalid();
  if (match_type.tag != ValueTag::Invalid) {
    return match_type;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_sre")});
  attrs.push_back({"group", runtime.make_native_function("_sre.Match.group", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) {
      error = "Match.group() expected optional group index";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    int64_t index = 0;
    if (argc == 2) {
      if (args[1].tag != ValueTag::Int64) {
        error = "group index must be int";
        return false;
      }
      index = args[1].as.i64;
    }
    if (index < 0 || static_cast<size_t>(index) >= state->groups.size()) {
      error = "no such group";
      return false;
    }
    out = match_group_value(*state, static_cast<size_t>(index));
    return true;
  })});
  attrs.push_back({"groups", runtime.make_native_function("_sre.Match.groups", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc != 1) {
      error = "Match.groups() expected no arguments";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    std::vector<Value> groups;
    for (size_t i = 1; i < state->groups.size(); ++i) {
      groups.push_back(match_group_value(*state, i));
    }
    out = Value::tuple(std::move(groups));
    return true;
  })});
  attrs.push_back({"start", runtime.make_native_function("_sre.Match.start", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) {
      error = "Match.start() expected optional group index";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    int64_t index = argc == 2 && args[1].tag == ValueTag::Int64 ? args[1].as.i64 : 0;
    if (index < 0 || static_cast<size_t>(index) >= state->groups.size()) {
      error = "no such group";
      return false;
    }
    value_set_int64(out, state->groups[static_cast<size_t>(index)].start);
    return true;
  })});
  attrs.push_back({"end", runtime.make_native_function("_sre.Match.end", [](Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
    if (argc < 1 || argc > 2) {
      error = "Match.end() expected optional group index";
      return false;
    }
    auto* state = match_state(args[0], error);
    if (state == nullptr) {
      return false;
    }
    int64_t index = argc == 2 && args[1].tag == ValueTag::Int64 ? args[1].as.i64 : 0;
    if (index < 0 || static_cast<size_t>(index) >= state->groups.size()) {
      error = "no such group";
      return false;
    }
    value_set_int64(out, state->groups[static_cast<size_t>(index)].end);
    return true;
  })});
  match_type = Value::class_object("SRE_Match", std::move(attrs));
  return match_type;
}

Value make_pattern_type(Runtime& runtime);

Value make_match(Runtime& runtime, const Value& pattern, const std::string& text, bool bytes_text, const std::match_results<std::string::const_iterator>& match, size_t base) {
  Value value = Value::instance(make_match_type(runtime));
  auto* state = new MatchState();
  state->pattern = pattern;
  state->text = text;
  state->bytes_text = bytes_text;
  state->groups.reserve(match.size());
  for (size_t i = 0; i < match.size(); ++i) {
    MatchGroup group;
    group.matched = match[i].matched;
    if (group.matched) {
      group.start = static_cast<int64_t>(base + static_cast<size_t>(match.position(i)));
      group.end = group.start + static_cast<int64_t>(match.length(i));
    }
    state->groups.push_back(group);
  }
  std::string error;
  (void)instance_set_native_data(value, kMatchNativeType, state, match_cleanup, error);
  return value;
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
  std::string text;
  bool bytes_text = false;
  if (!value_to_match_text(args[1], text, bytes_text) || bytes_text != state->bytes_pattern) {
    error = "expected matching string/bytes object";
    return false;
  }
  size_t pos = 0;
  if (argc >= 3 && args[2].tag == ValueTag::Int64 && args[2].as.i64 > 0) {
    pos = static_cast<size_t>(args[2].as.i64);
  }
  if (pos > text.size()) {
    value_set_none(out);
    return true;
  }
  std::match_results<std::string::const_iterator> match;
  auto begin = text.cbegin() + static_cast<std::ptrdiff_t>(pos);
  const auto flags = continuous ? std::regex_constants::match_continuous : std::regex_constants::match_default;
  if (!std::regex_search(begin, text.cend(), match, state->regex, flags)) {
    value_set_none(out);
    return true;
  }
  if (full && static_cast<size_t>(match.position(0)) + match.length(0) != text.size() - pos) {
    value_set_none(out);
    return true;
  }
  out = make_match(runtime, args[0], text, bytes_text, match, pos);
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

bool pattern_sub(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3 || argc > 4) {
    error = "Pattern.sub() expected replacement, string, and optional count";
    return false;
  }
  auto* state = pattern_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  std::string text;
  bool bytes_text = false;
  if (!value_to_match_text(args[2], text, bytes_text) || bytes_text != state->bytes_pattern) {
    error = "expected matching string/bytes object";
    return false;
  }
  int64_t max_count = 0;
  if (argc == 4 && args[3].tag == ValueTag::Int64) {
    max_count = args[3].as.i64;
  }
  std::string output;
  size_t cursor = 0;
  int64_t replacements = 0;
  std::match_results<std::string::const_iterator> match;
  while ((max_count <= 0 || replacements < max_count) &&
         std::regex_search(text.cbegin() + static_cast<std::ptrdiff_t>(cursor), text.cend(), match, state->regex)) {
    const size_t start = cursor + static_cast<size_t>(match.position(0));
    const size_t end = start + static_cast<size_t>(match.length(0));
    output.append(text, cursor, start - cursor);
    if (sre_value_is_callable(args[1])) {
      Value match_value = make_match(runtime, args[0], text, bytes_text, match, cursor);
      Value replacement;
      if (!runtime_call_callable(runtime, args[1], &match_value, 1, replacement, error)) {
        return false;
      }
      std::string replacement_text;
      bool replacement_bytes = false;
      if (!value_to_match_text(replacement, replacement_text, replacement_bytes) || replacement_bytes != bytes_text) {
        error = "replacement must return matching string/bytes object";
        return false;
      }
      output.append(replacement_text);
    } else {
      std::string replacement_text;
      bool replacement_bytes = false;
      if (!value_to_match_text(args[1], replacement_text, replacement_bytes) || replacement_bytes != bytes_text) {
        error = "replacement must be matching string/bytes object";
        return false;
      }
      output.append(replacement_text);
    }
    cursor = end;
    ++replacements;
    if (start == end) {
      if (cursor >= text.size()) {
        break;
      }
      output.push_back(text[cursor]);
      ++cursor;
    }
  }
  output.append(text, cursor, std::string::npos);
  out = bytes_text ? Value::bytes(std::move(output)) : Value::string(std::move(output));
  return true;
}

Value make_pattern_type(Runtime& runtime) {
  static Value pattern_type = Value::invalid();
  if (pattern_type.tag != ValueTag::Invalid) {
    return pattern_type;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_sre")});
  attrs.push_back({"match", runtime.make_native_function("_sre.Pattern.match", pattern_match)});
  attrs.push_back({"search", runtime.make_native_function("_sre.Pattern.search", pattern_search)});
  attrs.push_back({"fullmatch", runtime.make_native_function("_sre.Pattern.fullmatch", pattern_fullmatch)});
  attrs.push_back({"sub", runtime.make_native_function("_sre.Pattern.sub", pattern_sub)});
  pattern_type = Value::class_object("SRE_Pattern", std::move(attrs));
  return pattern_type;
}

bool sre_compile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 6) {
    error = "_sre.compile() expected pattern, flags, code, groups, groupindex, indexgroup";
    return false;
  }
  std::string pattern;
  bool bytes_pattern = false;
  if (!value_to_pattern_text(args[0], pattern, bytes_pattern)) {
    error = "_sre.compile() requires a string or bytes pattern";
    return false;
  }
  int64_t flags = args[1].tag == ValueTag::Int64 ? args[1].as.i64 : 0;
  std::string engine_pattern = (flags & kFlagVerbose) != 0 ? strip_verbose_regex(pattern) : pattern;
  std::regex::flag_type regex_flags = std::regex::ECMAScript;
  if ((flags & kFlagIgnoreCase) != 0) {
    regex_flags |= std::regex::icase;
  }
  try {
    auto* state = new PatternState{pattern, bytes_pattern, flags, std::regex(engine_pattern, regex_flags)};
    out = Value::instance(make_pattern_type(runtime));
    std::string native_error;
    if (!instance_set_native_data(out, kPatternNativeType, state, pattern_cleanup, native_error)) {
      delete state;
      error = native_error;
      return false;
    }
    (void)object_set_attr(out, "pattern", args[0], native_error);
    (void)object_set_attr(out, "flags", Value::int64(flags), native_error);
    (void)object_set_attr(out, "groups", args[3], native_error);
    (void)object_set_attr(out, "groupindex", args[4], native_error);
    return true;
  } catch (const std::regex_error& exc) {
    error = std::string("bad regex pattern: ") + exc.what();
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
