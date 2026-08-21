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

void pattern_cleanup(void* data) {
  delete static_cast<PatternState*>(data);
}

bool value_to_regex_string(const Value& value, std::string& out, std::string& error) {
  if (value.tag != ValueTag::Object || value.as.obj == nullptr || value.as.obj->kind != ObjectKind::String) {
    error = "regular expression argument must be str";
    return false;
  }
  out = value_to_string(value);
  return true;
}

Value make_match(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__bool__", runtime.make_native_function("re.Match.__bool__", [](Runtime&, const Value*, uint32_t, Value& out, std::string&, void*) {
                     out = Value::boolean(true);
                     return true;
                   })});
  return Value::instance(Value::class_object("Match", std::move(attrs)));
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

bool run_match(Runtime& runtime, const std::regex& regex, const std::string& text, bool full, Value& out, std::string& error) {
  try {
    const bool ok = full ? std::regex_match(text, regex) : std::regex_search(text, regex);
    if (ok) {
      out = make_match(runtime);
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
    return run_match(runtime, pattern_state->regex, text, false, out, error);
  }
  std::string pattern;
  if (!value_to_regex_string(args[0], pattern, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  try {
    std::regex regex(pattern);
    return run_match(runtime, regex, text, user_data != nullptr, out, error);
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

} // namespace

void register_re_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "re");
  builder.function("compile", regex_compile)
      .function("match", regex_match_entry)
      .value("search", runtime.make_native_function("re.search", regex_match_entry, reinterpret_cast<void*>(1)))
      .function("fullmatch", regex_match_entry)
      .function("escape", regex_escape)
      .value("IGNORECASE", Value::int64(2))
      .value("MULTILINE", Value::int64(8))
      .value("DOTALL", Value::int64(16))
      .value("Pattern", Value::class_object("Pattern", {}))
      .value("Match", Value::class_object("Match", {}));
  runtime.register_module("re", builder.finish());
}

} // namespace xlang3
