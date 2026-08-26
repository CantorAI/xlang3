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
#include "xlang3/parser.h"

#include "source_encoding.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kTokenizerIterNativeType = "_tokenize.TokenizerIter";

enum TokenizeNumber : int64_t {
  TokEndMarker = 0,
  TokName = 1,
  TokNumber = 2,
  TokString = 3,
  TokNewline = 4,
  TokIndent = 5,
  TokDedent = 6,
  TokOp = 55,
  TokComment = 65,
  TokNl = 66,
  TokEncoding = 68,
};

struct TokenizerToken {
  int64_t type = TokEndMarker;
  std::string text;
  uint32_t line = 0;
  uint32_t column = 0;
  std::string line_text;
};

struct TokenizerIterState {
  std::vector<TokenizerToken> tokens;
  size_t index = 0;
};

void tokenizer_iter_state_cleanup(void* data) {
  delete static_cast<TokenizerIterState*>(data);
}

bool token_kind_to_tokenize_number(TokenKind kind, int64_t& out) {
  switch (kind) {
  case TokenKind::End:
    out = TokEndMarker;
    return true;
  case TokenKind::Newline:
    out = TokNewline;
    return true;
  case TokenKind::Indent:
    out = TokIndent;
    return true;
  case TokenKind::Dedent:
    out = TokDedent;
    return true;
  case TokenKind::Identifier:
  case TokenKind::KwDef:
  case TokenKind::KwClass:
  case TokenKind::KwReturn:
  case TokenKind::KwIf:
  case TokenKind::KwElif:
  case TokenKind::KwElse:
  case TokenKind::KwTry:
  case TokenKind::KwExcept:
  case TokenKind::KwCase:
  case TokenKind::KwFinally:
  case TokenKind::KwRaise:
  case TokenKind::KwWith:
  case TokenKind::KwWhile:
  case TokenKind::KwFor:
  case TokenKind::KwIn:
  case TokenKind::KwImport:
  case TokenKind::KwFrom:
  case TokenKind::KwAs:
  case TokenKind::KwGlobal:
  case TokenKind::KwNonlocal:
  case TokenKind::KwBreak:
  case TokenKind::KwContinue:
  case TokenKind::KwPass:
  case TokenKind::KwDel:
  case TokenKind::KwAssert:
  case TokenKind::KwMatch:
  case TokenKind::KwTrue:
  case TokenKind::KwFalse:
  case TokenKind::KwNone:
  case TokenKind::KwAnd:
  case TokenKind::KwOr:
  case TokenKind::KwNot:
  case TokenKind::KwIs:
  case TokenKind::KwAsync:
  case TokenKind::KwAwait:
  case TokenKind::KwLambda:
  case TokenKind::KwYield:
    out = TokName;
    return true;
  case TokenKind::Integer:
  case TokenKind::Double:
    out = TokNumber;
    return true;
  case TokenKind::String:
  case TokenKind::Bytes:
  case TokenKind::FString:
    out = TokString;
    return true;
  case TokenKind::LParen:
  case TokenKind::RParen:
  case TokenKind::LBracket:
  case TokenKind::RBracket:
  case TokenKind::LBrace:
  case TokenKind::RBrace:
  case TokenKind::Ellipsis:
  case TokenKind::Dot:
  case TokenKind::Comma:
  case TokenKind::Semicolon:
  case TokenKind::Colon:
  case TokenKind::ColonEqual:
  case TokenKind::At:
  case TokenKind::Arrow:
  case TokenKind::Assign:
  case TokenKind::PlusAssign:
  case TokenKind::MinusAssign:
  case TokenKind::StarAssign:
  case TokenKind::DoubleStarAssign:
  case TokenKind::SlashAssign:
  case TokenKind::DoubleSlashAssign:
  case TokenKind::PercentAssign:
  case TokenKind::AmpAssign:
  case TokenKind::PipeAssign:
  case TokenKind::CaretAssign:
  case TokenKind::LeftShiftAssign:
  case TokenKind::RightShiftAssign:
  case TokenKind::Plus:
  case TokenKind::Minus:
  case TokenKind::Star:
  case TokenKind::DoubleStar:
  case TokenKind::Slash:
  case TokenKind::DoubleSlash:
  case TokenKind::Percent:
  case TokenKind::Amp:
  case TokenKind::Pipe:
  case TokenKind::Caret:
  case TokenKind::Tilde:
  case TokenKind::LeftShift:
  case TokenKind::RightShift:
  case TokenKind::EqualEqual:
  case TokenKind::NotEqual:
  case TokenKind::Less:
  case TokenKind::LessEqual:
  case TokenKind::Greater:
  case TokenKind::GreaterEqual:
    out = TokOp;
    return true;
  }
  return false;
}

bool value_to_source_text(const Value& value, std::string& out, bool& bytes_like) {
  if (StringObject* string_value = value_as_string(value)) {
    out = string_object_to_string(*string_value);
    bytes_like = false;
    return true;
  }
  if (BytesObject* bytes_value = value_as_bytes(value)) {
    out = std::string(bytes_object_view(*bytes_value));
    bytes_like = true;
    return true;
  }
  if (ByteArrayObject* bytearray_value = value_as_bytearray(value)) {
    out = bytearray_value->value;
    bytes_like = true;
    return true;
  }
  return false;
}

bool read_source_from_readline(
    Runtime& runtime,
    const Value& readline,
    const std::string* requested_encoding,
    std::string& source,
    std::string& encoding,
    std::string& error) {
  source.clear();
  encoding = "utf-8";
  bool saw_bytes = false;
  for (;;) {
    Value line;
    if (!runtime_call_callable(runtime, readline, nullptr, 0, line, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) {
        if (auto* klass = value_as_class(runtime.exception_type(pending)); klass != nullptr && klass->name == "StopIteration") {
          error.clear();
          return true;
        }
        runtime.set_pending_exception(std::move(pending));
      }
      return false;
    }
    std::string text;
    bool bytes_like = false;
    if (!value_to_source_text(line, text, bytes_like)) {
      error = "TokenizerIter readline must return bytes or str";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (text.empty()) {
      if (saw_bytes) {
        PythonSourceText decoded;
        const bool decoded_ok = requested_encoding == nullptr
                                    ? decode_python_source_bytes(source, decoded, error)
                                    : decode_python_source_bytes_as(source, *requested_encoding, decoded, error);
        if (!decoded_ok) {
          runtime.raise_class_error("SyntaxError", error);
          return false;
        }
        source = std::move(decoded.text);
        encoding = std::move(decoded.encoding);
      }
      return true;
    }
    saw_bytes = saw_bytes || bytes_like;
    source += text;
  }
}

std::vector<std::string> split_source_lines(std::string_view source) {
  std::vector<std::string> lines;
  size_t start = 0;
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\n') {
      lines.emplace_back(source.substr(start, i - start + 1));
      start = i + 1;
    }
  }
  if (start < source.size()) {
    lines.emplace_back(source.substr(start));
  }
  return lines;
}

std::string line_text_at(const std::vector<std::string>& lines, uint32_t one_based_line) {
  if (one_based_line == 0 || one_based_line > lines.size()) {
    return {};
  }
  return lines[one_based_line - 1];
}

size_t comment_start_in_line(std::string_view line) {
  char quote = '\0';
  bool escaped = false;
  for (size_t i = 0; i < line.size(); ++i) {
    const char ch = line[i];
    if (quote != '\0') {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == quote) {
        quote = '\0';
      }
      continue;
    }
    if (ch == '\'' || ch == '"') {
      quote = ch;
      continue;
    }
    if (ch == '#') {
      return i;
    }
  }
  return std::string_view::npos;
}

std::vector<TokenizerToken> collect_comment_and_nl_tokens(const std::vector<std::string>& lines) {
  std::vector<TokenizerToken> out;
  for (uint32_t line_index = 0; line_index < lines.size(); ++line_index) {
    const uint32_t line_no = line_index + 1;
    const std::string& line = lines[line_index];
    const size_t newline_size = !line.empty() && line.back() == '\n' ? 1 : 0;
    const std::string_view content(line.data(), line.size() - newline_size);
    const size_t first = content.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
      out.push_back({TokNl, std::string(line.substr(content.size())), line_no, static_cast<uint32_t>(content.size()), line});
      continue;
    }
    const size_t comment = comment_start_in_line(content);
    if (comment != std::string_view::npos) {
      out.push_back({TokComment, std::string(content.substr(comment)), line_no, static_cast<uint32_t>(comment), line});
      if (first == comment) {
        out.push_back({TokNl, std::string(line.substr(content.size())), line_no, static_cast<uint32_t>(content.size()), line});
      }
    }
  }
  return out;
}

Value make_position_tuple(uint32_t line, uint32_t column) {
  std::vector<Value> items;
  items.reserve(2);
  items.push_back(Value::int64(line));
  items.push_back(Value::int64(column));
  return Value::tuple(std::move(items));
}

Value make_token_tuple(const TokenizerToken& token) {
  const uint32_t end_column = token.column + static_cast<uint32_t>(token.text.size());
  std::vector<Value> items;
  items.reserve(5);
  items.push_back(Value::int64(token.type));
  items.push_back(Value::string(token.text));
  items.push_back(make_position_tuple(token.line, token.column));
  items.push_back(make_position_tuple(token.line, end_column));
  items.push_back(Value::string(token.line_text));
  return Value::tuple(std::move(items));
}

bool tokenizer_iter_init_impl(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const std::string* requested_encoding,
    bool emit_encoding_token,
    Value& out,
    std::string& error) {
  if (argc < 2) {
    error = "TokenizerIter.__init__() missing readline";
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  std::string source;
  std::string encoding;
  if (!read_source_from_readline(runtime, args[1], requested_encoding, source, encoding, error)) {
    return false;
  }

  Lexer lexer(source);
  LexResult lex = lexer.tokenize();
  if (!lex.errors.empty()) {
    error = lex.errors.front();
    runtime.raise_class_error("SyntaxError", error);
    return false;
  }

  auto* state = new TokenizerIterState();
  if (emit_encoding_token) {
    state->tokens.push_back({TokEncoding, encoding, 0, 0, ""});
  }

  const std::vector<std::string> lines = split_source_lines(source);
  std::vector<TokenizerToken> extra_tokens = collect_comment_and_nl_tokens(lines);
  size_t extra_index = 0;
  auto append_extra_before = [&](uint32_t line, uint32_t column) {
    while (extra_index < extra_tokens.size()) {
      const auto& extra = extra_tokens[extra_index];
      if (extra.line > line || (extra.line == line && extra.column >= column)) {
        break;
      }
      state->tokens.push_back(extra);
      ++extra_index;
    }
  };
  bool saw_end = false;
  for (const Token& token : lex.tokens) {
    int64_t type = TokOp;
    if (!token_kind_to_tokenize_number(token.kind, type)) {
      continue;
    }
    if (token.kind == TokenKind::End) {
      saw_end = true;
    }
    const uint32_t zero_based_column = token.column > 0 ? token.column - 1 : 0;
    append_extra_before(
        token.kind == TokenKind::End ? static_cast<uint32_t>(lines.size() + 1) : token.line,
        token.kind == TokenKind::End ? 0 : zero_based_column);
    state->tokens.push_back(
        {type,
         std::string(token.text),
         token.kind == TokenKind::End ? static_cast<uint32_t>(lines.size() + 1) : token.line,
         token.kind == TokenKind::End ? 0 : zero_based_column,
         token.kind == TokenKind::End ? std::string() : line_text_at(lines, token.line)});
  }
  while (extra_index < extra_tokens.size()) {
    state->tokens.push_back(extra_tokens[extra_index++]);
  }
  if (!saw_end) {
    state->tokens.push_back({TokEndMarker, "", static_cast<uint32_t>(lines.size() + 1), 0, ""});
  }

  if (!instance_set_native_data(args[0], kTokenizerIterNativeType, state, tokenizer_iter_state_cleanup, error)) {
    delete state;
    runtime.raise_class_error("TypeError", error);
    return false;
  }

  value_set_none(out);
  return true;
}

bool tokenizer_iter_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return tokenizer_iter_init_impl(runtime, args, argc, nullptr, true, out, error);
}

bool tokenizer_iter_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  std::string requested_encoding;
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    if (name != "encoding" && name != "extra_tokens") {
      error = "TokenizerIter.__init__() got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (name == "encoding") {
      if (auto* str = value_as_string(*kwargs[i].value)) {
        requested_encoding = string_object_to_string(*str);
      } else if (kwargs[i].value->tag != ValueTag::None) {
        error = "TokenizerIter.__init__() encoding must be str or None";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
    }
  }
  const std::string* encoding_ptr = requested_encoding.empty() ? nullptr : &requested_encoding;
  return tokenizer_iter_init_impl(runtime, args, argc, encoding_ptr, encoding_ptr == nullptr, out, error);
}

bool tokenizer_iter_self(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "TokenizerIter.__iter__() expected no arguments";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool tokenizer_iter_next(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "TokenizerIter.__next__() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = static_cast<TokenizerIterState*>(instance_get_native_data(args[0], kTokenizerIterNativeType));
  if (state == nullptr) {
    error = "TokenizerIter is not initialized";
    runtime.raise_class_error("RuntimeError", error);
    return false;
  }
  if (state->index >= state->tokens.size()) {
    runtime.raise_class_error("StopIteration", "");
    return false;
  }
  out = make_token_tuple(state->tokens[state->index++]);
  return true;
}

Value make_tokenizer_iter_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__",
                   runtime.make_native_function(
                       "_tokenize.TokenizerIter.__init__",
                       tokenizer_iter_init,
                       nullptr,
                       nullptr,
                       nullptr,
                       false,
                       tokenizer_iter_init_kw)});
  attrs.push_back({"__iter__", runtime.make_native_function("_tokenize.TokenizerIter.__iter__", tokenizer_iter_self)});
  attrs.push_back({"__next__", runtime.make_native_function("_tokenize.TokenizerIter.__next__", tokenizer_iter_next)});
  return Value::class_object("TokenizerIter", std::move(attrs));
}

} // namespace

void register_tokenize_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_tokenize");
  builder.value("TokenizerIter", make_tokenizer_iter_class(runtime));
  runtime.register_module("_tokenize", builder.finish());
}

} // namespace xlang3
