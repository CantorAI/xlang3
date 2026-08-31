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

struct TokenizerState {
  Value source;
  std::string encoding;
  bool has_encoding = false;
  bool extra_tokens = false;
  bool built = false;
  size_t index = 0;
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

Value position_value(size_t row, size_t col) {
  return Value::tuple({Value::int64(static_cast<int64_t>(row)), Value::int64(static_cast<int64_t>(col))});
}

Value token_value(int64_t type, std::string text, size_t srow, size_t scol, size_t erow, size_t ecol, const std::string& line) {
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
    const std::string& line) {
  state.tokens.push_back(token_value(type, std::move(text), srow, scol, erow, ecol, line));
}

bool starts_with(std::string_view text, size_t pos, std::string_view needle) {
  return pos + needle.size() <= text.size() && text.substr(pos, needle.size()) == needle;
}

size_t scan_string_literal(std::string_view line, size_t pos) {
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
        return cursor + 3;
      }
    } else if (ch == delimiter) {
      return cursor + 1;
    }
    ++cursor;
  }
  return line.size();
}

std::string scan_operator(std::string_view line, size_t pos, size_t& width) {
  static constexpr std::string_view kThree[] = {"//=", "**=", "<<=", ">>=", "..."};
  static constexpr std::string_view kTwo[] = {
      "!=", "%=", "&=", "*=", "+=", "-=", "->", "//", ":=", "<<", "<=", "==", ">=", ">>", "@=", "^=", "|="};
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

void emit_indent_tokens(TokenizerState& state, size_t line_no, size_t indent, const std::string& line) {
  const int current = state.indent_stack.empty() ? 0 : state.indent_stack.back();
  if (static_cast<int>(indent) > current) {
    state.indent_stack.push_back(static_cast<int>(indent));
    push_token(state, kTokenIndent, line.substr(0, indent), line_no, 0, line_no, indent, line);
    return;
  }
  while (!state.indent_stack.empty() && static_cast<int>(indent) < state.indent_stack.back()) {
    state.indent_stack.pop_back();
    push_token(state, kTokenDedent, "", line_no, indent, line_no, indent, line);
  }
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

  size_t indent = 0;
  while (indent < logical_end && (line[indent] == ' ' || line[indent] == '\t')) {
    indent += line[indent] == '\t' ? 8 : 1;
  }

  if (indent >= logical_end) {
    if (line_size > logical_end) {
      push_token(state, kTokenNl, line.substr(logical_end), line_no, logical_end, line_no, line_size, line);
    }
    return;
  }

  if (line[indent] != '#') {
    emit_indent_tokens(state, line_no, indent, line);
  }

  size_t pos = indent;
  bool emitted_statement_token = false;
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
    if (is_identifier_start(ch)) {
      const size_t start = pos++;
      while (pos < logical_end && is_identifier_continue(static_cast<unsigned char>(line[pos]))) {
        ++pos;
      }
      push_token(state, kTokenName, line.substr(start, pos - start), line_no, start, line_no, pos, line);
      emitted_statement_token = true;
      continue;
    }
    if (std::isdigit(ch) != 0) {
      const size_t start = pos++;
      while (pos < logical_end &&
             (std::isalnum(static_cast<unsigned char>(line[pos])) != 0 || line[pos] == '_' || line[pos] == '.')) {
        ++pos;
      }
      push_token(state, kTokenNumber, line.substr(start, pos - start), line_no, start, line_no, pos, line);
      emitted_statement_token = true;
      continue;
    }
    const size_t literal_end = scan_string_literal(line, pos);
    if (literal_end > pos) {
      push_token(state, kTokenString, line.substr(pos, literal_end - pos), line_no, pos, line_no, literal_end, line);
      pos = literal_end;
      emitted_statement_token = true;
      continue;
    }
    size_t width = 0;
    std::string op = scan_operator(line, pos, width);
    push_token(state, kTokenOp, std::move(op), line_no, pos, line_no, pos + width, line);
    pos += width;
    emitted_statement_token = true;
  }

  if (line_size > logical_end) {
    push_token(state, emitted_statement_token ? kTokenNewline : kTokenNl, line.substr(logical_end), line_no, logical_end, line_no, line_size, line);
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

    std::string line;
    if (!line_text_from_value(raw_line, state.encoding, state.has_encoding, line_no == 1, line, error)) {
      return false;
    }
    if (line.empty()) {
      break;
    }
    tokenize_line(state, line, line_no);
    if (line.back() != '\n' && line.back() != '\r') {
      break;
    }
  }

  const size_t end_line = state.tokens.empty() ? 1 : state.tokens.size() + 1;
  while (state.indent_stack.size() > 1) {
    state.indent_stack.pop_back();
    push_token(state, kTokenDedent, "", end_line, 0, end_line, 0, "");
  }
  push_token(state, kTokenEndMarker, "", end_line, 0, end_line, 0, "");
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
