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

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <regex>
#include <string>

namespace xlang3 {

namespace {

constexpr const char* kPatternType = "re.Pattern";
constexpr const char* kMatchType = "re.Match";

struct PatternState {
  std::string pattern;
  std::regex regex;
};

struct MatchState {
  std::string text;
  std::vector<std::string> groups;
  std::vector<std::pair<int64_t, int64_t>> spans;
};

void pattern_cleanup(void* data) {
  delete static_cast<PatternState*>(data);
}

void match_cleanup(void* data) {
  delete static_cast<MatchState*>(data);
}

bool regex_match_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data);

bool value_to_regex_string(const Value& value, std::string& out, std::string& error) {
  if (auto* string = value_as_string(value)) {
    out = string_object_to_string(*string);
    return true;
  }
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_to_string(*bytes);
    return true;
  }
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = "regular expression argument must be str";
    return false;
  }
  out = value_to_string(value);
  return true;
}

int64_t match_group_index(const Value* args, uint32_t argc) {
  if (argc < 2 || args[1].tag == ValueTag::None) {
    return 0;
  }
  return args[1].tag == ValueTag::Int64 ? args[1].as.i64 : 0;
}

bool match_group(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "Match.group() expected optional index";
    return false;
  }
  auto* state = static_cast<MatchState*>(instance_get_native_data(args[0], kMatchType));
  if (state == nullptr) {
    error = "invalid match object";
    return false;
  }
  const int64_t index = match_group_index(args, argc);
  if (index < 0 || static_cast<size_t>(index) >= state->groups.size()) {
    error = "no such group";
    return false;
  }
  out = Value::string(state->groups[static_cast<size_t>(index)]);
  return true;
}

bool match_start_end(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 1 || argc > 2) {
    error = "Match position method expected optional index";
    return false;
  }
  auto* state = static_cast<MatchState*>(instance_get_native_data(args[0], kMatchType));
  if (state == nullptr) {
    error = "invalid match object";
    return false;
  }
  const int64_t index = match_group_index(args, argc);
  if (index < 0 || static_cast<size_t>(index) >= state->spans.size()) {
    error = "no such group";
    return false;
  }
  const bool want_end = user_data != nullptr;
  value_set_int64(out, want_end ? state->spans[static_cast<size_t>(index)].second : state->spans[static_cast<size_t>(index)].first);
  return true;
}

bool match_span(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  Value start;
  Value end;
  if (!match_start_end(runtime, args, argc, start, error, nullptr) ||
      !match_start_end(runtime, args, argc, end, error, reinterpret_cast<void*>(1))) {
    return false;
  }
  out = Value::tuple({start, end});
  return true;
}

bool match_groups(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "Match.groups() expected optional default";
    return false;
  }
  auto* state = static_cast<MatchState*>(instance_get_native_data(args[0], kMatchType));
  if (state == nullptr) {
    error = "invalid match object";
    return false;
  }
  std::vector<Value> values;
  for (size_t i = 1; i < state->groups.size(); ++i) {
    values.push_back(Value::string(state->groups[i]));
  }
  out = Value::tuple(std::move(values));
  return true;
}

Value make_match(Runtime& runtime, const std::string& text, const std::smatch& match, std::string& error) {
  auto* state = new MatchState();
  state->text = text;
  state->groups.reserve(match.size());
  state->spans.reserve(match.size());
  for (size_t i = 0; i < match.size(); ++i) {
    state->groups.push_back(match[i].str());
    if (match[i].matched) {
      const int64_t start = static_cast<int64_t>(match.position(i));
      state->spans.push_back({start, start + static_cast<int64_t>(match.length(i))});
    } else {
      state->spans.push_back({-1, -1});
    }
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__bool__", runtime.make_native_function("re.Match.__bool__", [](Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
                     out = Value::boolean(true);
                     return true;
                   })});
  attrs.push_back({"group", runtime.make_native_function("re.Match.group", match_group)});
  attrs.push_back({"groups", runtime.make_native_function("re.Match.groups", match_groups)});
  attrs.push_back({"start", runtime.make_native_function("re.Match.start", match_start_end)});
  attrs.push_back({"end", runtime.make_native_function("re.Match.end", match_start_end, reinterpret_cast<void*>(1))});
  attrs.push_back({"span", runtime.make_native_function("re.Match.span", match_span)});
  Value instance = Value::instance(Value::class_object("Match", std::move(attrs)));
  if (!instance_set_native_data(instance, kMatchType, state, match_cleanup, error)) {
    delete state;
  }
  return instance;
}

bool compile_pattern(Runtime& runtime, const std::string& pattern, Value& out, std::string& error) {
  auto* state = new PatternState();
  state->pattern = pattern;
  try {
    state->regex = std::regex(pattern);
  } catch (const std::regex_error&) {
    delete state;
    error = "invalid regular expression";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"pattern", Value::string(pattern)});
  attrs.push_back({"match", runtime.make_native_function("re.Pattern.match", regex_match_entry)});
  attrs.push_back({"search", runtime.make_native_function("re.Pattern.search", regex_match_entry, reinterpret_cast<void*>(1))});
  attrs.push_back({"fullmatch", runtime.make_native_function("re.Pattern.fullmatch", regex_match_entry, reinterpret_cast<void*>(2))});
  Value klass = Value::class_object("Pattern", std::move(attrs));
  out = Value::instance(klass);
  if (!instance_set_native_data(out, kPatternType, state, pattern_cleanup, error)) {
    delete state;
    return false;
  }
  return true;
}

bool regex_compile(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "compile() expected pattern";
    return false;
  }
  std::string pattern;
  if (!value_to_regex_string(args[0], pattern, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return compile_pattern(runtime, pattern, out, error);
}

bool run_match(Runtime& runtime, const std::regex& regex, const std::string& text, int mode, Value& out, std::string& error) {
  try {
    std::smatch match;
    const bool ok = mode == 2 ? std::regex_match(text, match, regex) :
        mode == 0 ? std::regex_search(text, match, regex, std::regex_constants::match_continuous) :
        std::regex_search(text, match, regex);
    if (ok) {
      out = make_match(runtime, text, match, error);
    } else {
      value_set_none(out);
    }
    return true;
  } catch (const std::regex_error&) {
    error = "regular expression match failed";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
}

bool regex_match_entry(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc < 2) {
    error = "match() expected pattern and string";
    return false;
  }
  std::string text;
  if (!value_to_regex_string(args[1], text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* pattern_state = static_cast<PatternState*>(instance_get_native_data(args[0], kPatternType));
  if (pattern_state != nullptr) {
    const int mode = user_data == reinterpret_cast<void*>(2) ? 2 : user_data == nullptr ? 0 : 1;
    return run_match(runtime, pattern_state->regex, text, mode, out, error);
  }
  std::string pattern;
  if (!value_to_regex_string(args[0], pattern, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  try {
    std::regex regex(pattern);
    const int mode = user_data == reinterpret_cast<void*>(2) ? 2 : user_data == nullptr ? 0 : 1;
    return run_match(runtime, regex, text, mode, out, error);
  } catch (const std::regex_error&) {
    error = "invalid regular expression";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
}

bool regex_escape(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "escape() expected pattern";
    return false;
  }
  std::string text;
  if (!value_to_regex_string(args[0], text, error)) {
    return false;
  }
  std::string escaped;
  escaped.reserve(text.size() * 2);
  static constexpr const char* special = R"(\.^$|()[]{}*+?)";
  for (const char ch : text) {
    if (std::string(special).find(ch) != std::string::npos) {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  out = Value::string(escaped);
  return true;
}

bool regex_findall(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "findall() expected pattern and string";
    return false;
  }
  std::string pattern;
  std::string text;
  if (!value_to_regex_string(args[0], pattern, error) || !value_to_regex_string(args[1], text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  try {
    std::regex regex(pattern);
    std::vector<Value> values;
    for (std::sregex_iterator it(text.begin(), text.end(), regex), end; it != end; ++it) {
      values.push_back(Value::string((*it).str()));
    }
    out = Value::list(std::move(values));
    return true;
  } catch (const std::regex_error&) {
    error = "invalid regular expression";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
}

bool regex_split(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "split() expected pattern and string";
    return false;
  }
  std::string pattern;
  std::string text;
  if (!value_to_regex_string(args[0], pattern, error) || !value_to_regex_string(args[1], text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  try {
    std::regex regex(pattern);
    std::vector<Value> values;
    for (std::sregex_token_iterator it(text.begin(), text.end(), regex, -1), end; it != end; ++it) {
      values.push_back(Value::string(it->str()));
    }
    out = Value::list(std::move(values));
    return true;
  } catch (const std::regex_error&) {
    error = "invalid regular expression";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
}

bool regex_sub(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 3) {
    error = "sub() expected pattern, repl, and string";
    return false;
  }
  std::string pattern;
  std::string repl;
  std::string text;
  if (!value_to_regex_string(args[0], pattern, error) ||
      !value_to_regex_string(args[1], repl, error) ||
      !value_to_regex_string(args[2], text, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  try {
    out = Value::string(std::regex_replace(text, std::regex(pattern), repl));
    return true;
  } catch (const std::regex_error&) {
    error = "invalid regular expression";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
}

} // namespace

void register_re_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "re");
  builder.function("compile", regex_compile)
      .function("match", regex_match_entry)
      .value("search", runtime.make_native_function("re.search", regex_match_entry, reinterpret_cast<void*>(1)))
      .value("fullmatch", runtime.make_native_function("re.fullmatch", regex_match_entry, reinterpret_cast<void*>(2)))
      .function("findall", regex_findall)
      .function("split", regex_split)
      .function("sub", regex_sub)
      .function("escape", regex_escape)
      .value("NOFLAG", Value::int64(0))
      .value("ASCII", Value::int64(256))
      .value("A", Value::int64(256))
      .value("IGNORECASE", Value::int64(2))
      .value("I", Value::int64(2))
      .value("LOCALE", Value::int64(4))
      .value("L", Value::int64(4))
      .value("MULTILINE", Value::int64(8))
      .value("M", Value::int64(8))
      .value("DOTALL", Value::int64(16))
      .value("S", Value::int64(16))
      .value("VERBOSE", Value::int64(64))
      .value("X", Value::int64(64))
      .value("DEBUG", Value::int64(128))
      .value("RegexFlag", Value::class_object("RegexFlag", {}))
      .value("Pattern", Value::class_object("Pattern", {}))
      .value("Match", Value::class_object("Match", {}));
  runtime.register_module("re", builder.finish());
}

} // namespace xlang3
