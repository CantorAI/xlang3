/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
*/
#include "xlang3/builtins.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace xlang3 {
namespace {

struct CsvState {
  Runtime* runtime = nullptr;
  Value dialect_class;
  Value reader_class;
  Value writer_class;
  Value error_class;
  std::unordered_map<std::string, Value> dialects;
  int64_t field_limit = 131072;
};

struct CsvDialect {
  std::string delimiter = ",";
  std::string quotechar = "\"";
  std::string escapechar;
  std::string lineterminator = "\r\n";
  bool doublequote = true;
  bool skipinitialspace = false;
  bool strict = false;
  int64_t quoting = 0;
};

bool string_value(const Value& value, std::string& out) {
  if (auto* text = value_as_string(value)) {
    out = string_object_to_string(*text);
    return true;
  }
  return false;
}

bool optional_char(Runtime& runtime, const Value& value, const char* name, std::string& out, std::string& error) {
  if (value.tag == ValueTag::None && std::string_view(name) != "delimiter") {
    out.clear();
    return true;
  }
  if (!string_value(value, out) || utf8_codepoint_count(out) != 1) {
    error = "\"" + std::string(name) + "\" must be a unicode character";
    if (std::string_view(name) != "delimiter") error += " or None";
    error += ", not ";
    if (value_as_string(value) != nullptr) {
      error += "a string of length " + std::to_string(utf8_codepoint_count(out));
    } else {
      error += value_binary_type_name(value);
    }
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool dialect_attr(const Value& object, const char* name, Value& out) {
  std::string ignored;
  return object_get_attr(object, name, out, ignored);
}

bool parse_dialect(Runtime& runtime, const Value& object, CsvDialect& dialect, std::string& error) {
  Value value;
  if (dialect_attr(object, "delimiter", value) &&
      !optional_char(runtime, value, "delimiter", dialect.delimiter, error)) return false;
  if (dialect.delimiter.empty()) {
    error = "delimiter must be a 1-character string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (dialect_attr(object, "quotechar", value) &&
      !optional_char(runtime, value, "quotechar", dialect.quotechar, error)) return false;
  if (dialect_attr(object, "escapechar", value) &&
      !optional_char(runtime, value, "escapechar", dialect.escapechar, error)) return false;
  if (dialect_attr(object, "lineterminator", value)) {
    if (!string_value(value, dialect.lineterminator)) {
      error = "\"lineterminator\" must be a string, not " +
          std::string(value_binary_type_name(value));
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  if (dialect_attr(object, "doublequote", value)) dialect.doublequote = value_truthy(value);
  if (dialect_attr(object, "skipinitialspace", value)) dialect.skipinitialspace = value_truthy(value);
  if (dialect_attr(object, "strict", value)) dialect.strict = value_truthy(value);
  if (dialect_attr(object, "quoting", value) && !value_int_like_to_i64(value, dialect.quoting)) {
    error = "quoting must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (dialect.quoting < 0 || dialect.quoting > 5) {
    error = "bad \"quoting\" value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (dialect.quoting != 3 && dialect.quotechar.empty()) {
    error = "quotechar must be set if quoting enabled";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto invalid_special = [&](const std::string& character) {
    return character == "\r" || character == "\n" ||
        (!character.empty() && dialect.lineterminator.find(character) != std::string::npos);
  };
  if (invalid_special(dialect.delimiter) || invalid_special(dialect.quotechar) ||
      invalid_special(dialect.escapechar) ||
      (!dialect.escapechar.empty() && dialect.delimiter == dialect.escapechar) ||
      (!dialect.quotechar.empty() && dialect.delimiter == dialect.quotechar) ||
      (!dialect.escapechar.empty() && dialect.escapechar == dialect.quotechar) ||
      (dialect.skipinitialspace &&
       (dialect.quotechar == " " || dialect.escapechar == " "))) {
    error = "bad dialect value";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

Value make_dialect_value(CsvState& state, const CsvDialect& dialect) {
  Value result = Value::instance(state.dialect_class);
  std::string ignored;
  object_set_attr(result, "delimiter", Value::string(dialect.delimiter), ignored);
  object_set_attr(result, "quotechar", dialect.quotechar.empty() ? Value::none() : Value::string(dialect.quotechar), ignored);
  object_set_attr(result, "escapechar", dialect.escapechar.empty() ? Value::none() : Value::string(dialect.escapechar), ignored);
  object_set_attr(result, "doublequote", Value::boolean(dialect.doublequote), ignored);
  object_set_attr(result, "skipinitialspace", Value::boolean(dialect.skipinitialspace), ignored);
  object_set_attr(result, "lineterminator", Value::string(dialect.lineterminator), ignored);
  object_set_attr(result, "quoting", Value::int64(dialect.quoting), ignored);
  object_set_attr(result, "strict", Value::boolean(dialect.strict), ignored);
  return result;
}

bool resolve_dialect(CsvState& state, const Value& source, CsvDialect& parsed, Value& value, std::string& error) {
  if (auto* text = value_as_string(source)) {
    const auto name = string_object_to_string(*text);
    auto found = state.dialects.find(name);
    if (found == state.dialects.end()) {
      error = "unknown dialect";
      state.runtime->set_pending_exception(state.runtime->make_exception_from_class(state.error_class, error));
      return false;
    }
    value_assign_fast(value, found->second);
  } else {
    value_assign_fast(value, source);
  }
  return parse_dialect(*state.runtime, value, parsed, error);
}

bool apply_format_keywords(Runtime& runtime, CsvDialect& dialect, const NativeKeywordArg* kwargs, uint32_t kwargc, std::string& error) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string name = kwargs[i].name == nullptr ? "" : kwargs[i].name;
    const Value& value = *kwargs[i].value;
    if (name == "delimiter") { if (!optional_char(runtime, value, "delimiter", dialect.delimiter, error)) return false; }
    else if (name == "quotechar") { if (!optional_char(runtime, value, "quotechar", dialect.quotechar, error)) return false; }
    else if (name == "escapechar") { if (!optional_char(runtime, value, "escapechar", dialect.escapechar, error)) return false; }
    else if (name == "lineterminator") { if (!string_value(value, dialect.lineterminator)) { error = "\"lineterminator\" must be a string, not " + std::string(value_binary_type_name(value)); runtime.raise_class_error("TypeError", error); return false; } }
    else if (name == "doublequote") dialect.doublequote = value_truthy(value);
    else if (name == "skipinitialspace") dialect.skipinitialspace = value_truthy(value);
    else if (name == "strict") dialect.strict = value_truthy(value);
    else if (name == "quoting") { if (!value_int_like_to_i64(value, dialect.quoting)) { error = "quoting must be an integer"; runtime.raise_class_error("TypeError", error); return false; } }
    else { error = "this function got an unexpected keyword argument '" + name + "'"; runtime.raise_class_error("TypeError", error); return false; }
  }
  return true;
}

bool dialect_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) { error = "Dialect expected 1 argument"; runtime.raise_class_error("TypeError", error); return false; }
  CsvDialect parsed;
  if (!parse_dialect(runtime, args[1], parsed, error)) return false;
  Value normalized = make_dialect_value(*static_cast<CsvState*>(user_data), parsed);
  auto* src = value_as_instance(normalized);
  auto* dst = value_as_instance(args[0]);
  if (src != nullptr && dst != nullptr) dst->attrs = src->attrs;
  value_set_none(out);
  return true;
}

bool dialect_readonly(Runtime& runtime, const Value*, uint32_t argc, Value&, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "attribute name expected";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  error = "attribute is read-only";
  runtime.raise_class_error("AttributeError", error);
  return false;
}

bool dialect_reduce(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "cannot pickle 'Dialect' instances";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool reader_iter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "reader.__iter__ expected no arguments"; return false; }
  value_assign_fast(out, args[0]);
  return true;
}

bool parse_record(CsvState& state, const CsvDialect& dialect, std::string& buffer,
                  bool at_eof, std::vector<Value>& fields, bool& complete, std::string& error) {
  fields.clear();
  if (buffer.empty()) { complete = true; return true; }
  std::string field;
  bool quoted = false;
  bool field_was_quoted = false;
  bool at_field_start = true;
  bool after_quote = false;
  auto append_field = [&]() -> bool {
    if (!field_was_quoted && field.empty() &&
        (dialect.quoting == 4 || dialect.quoting == 5)) {
      fields.push_back(Value::none());
      return true;
    }
    if (!field_was_quoted && !field.empty() &&
        (dialect.quoting == 2 || dialect.quoting == 4)) {
      const Value* float_function = state.runtime->find_builtin("float");
      if (float_function == nullptr) { error = "float builtin unavailable"; return false; }
      Value text = Value::string(field);
      Value number;
      if (!runtime_call_callable(*state.runtime, *float_function, &text, 1,
                                 number, error)) return false;
      fields.push_back(std::move(number));
      return true;
    }
    fields.push_back(Value::string(field));
    return true;
  };
  for (size_t i = 0; i < buffer.size(); ++i) {
    const char ch = buffer[i];
    if (quoted) {
      if (!dialect.escapechar.empty() && ch == dialect.escapechar[0]) {
        if (i + 1 < buffer.size()) field.push_back(buffer[++i]);
        else { complete = false; return true; }
      } else if (!dialect.quotechar.empty() && ch == dialect.quotechar[0]) {
        if (dialect.doublequote && i + 1 < buffer.size() && buffer[i + 1] == ch) { field.push_back(ch); ++i; }
        else { quoted = false; after_quote = true; }
      } else field.push_back(ch);
      continue;
    }
    if (after_quote && dialect.strict && ch != dialect.delimiter[0] &&
        ch != '\r' && ch != '\n') {
      error = "'" + dialect.delimiter + "' expected after '" +
          dialect.quotechar + "'";
      return false;
    }
    if (at_field_start && dialect.skipinitialspace && ch == ' ') continue;
    if (at_field_start && !dialect.quotechar.empty() && ch == dialect.quotechar[0] && dialect.quoting != 3) {
      quoted = true; field_was_quoted = true; at_field_start = false; continue;
    }
    if (!field_was_quoted && !dialect.escapechar.empty() &&
        ch == dialect.escapechar[0]) {
      if (i + 1 < buffer.size()) {
        const char escaped = buffer[++i];
        field.push_back(escaped);
        if ((escaped == '\r' || escaped == '\n') &&
            i + 1 == buffer.size() && !at_eof) {
          complete = false;
          return true;
        }
      }
      else { complete = false; return true; }
      at_field_start = false;
      continue;
    }
    if (ch == dialect.delimiter[0]) {
      if (!append_field()) return false;
      field.clear(); field_was_quoted = false; at_field_start = true;
      after_quote = false; continue;
    }
    if (ch == '\r' || ch == '\n') {
      if (ch == '\r' && i + 1 < buffer.size() && buffer[i + 1] == '\n') ++i;
      if (i + 1 != buffer.size()) {
        error = "new-line character seen in unquoted field - do you need to open the file with newline=''?";
        return false;
      }
      if (fields.empty() && field.empty() && at_field_start) {
        complete = true;
        return true;
      }
      if (!append_field()) return false;
      complete = true; return true;
    }
    field.push_back(ch); at_field_start = false;
    if (static_cast<int64_t>(field.size()) > state.field_limit) { error = "field larger than field limit"; return false; }
  }
  if (quoted && !at_eof) { complete = false; return true; }
  if (!append_field()) return false;
  complete = true;
  return true;
}

bool reader_next(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) { error = "reader.__next__ expected no arguments"; return false; }
  auto& state = *static_cast<CsvState*>(user_data);
  Value busy;
  std::string ignored;
  if (object_get_attr(args[0], "_busy", busy, ignored) && value_truthy(busy)) {
    error = "reentrant call inside _csv.reader";
    runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error));
    return false;
  }
  Value self = args[0];
  if (!object_set_attr(self, "_busy", Value::boolean(true), error)) return false;
  struct BusyGuard {
    Value reader;
    ~BusyGuard() {
      std::string ignored_error;
      object_set_attr(reader, "_busy", Value::boolean(false), ignored_error);
    }
  } busy_guard{self};
  Value iterator, dialect_value, line_value;
  if (!object_get_attr(args[0], "_iterator", iterator, error) || !object_get_attr(args[0], "dialect", dialect_value, error)) return false;
  CsvDialect dialect;
  if (!parse_dialect(runtime, dialect_value, dialect, error)) return false;
  int64_t line_num = 0;
  if (object_get_attr(args[0], "line_num", line_value, error)) value_int_like_to_i64(line_value, line_num);
  std::string buffer;
  while (true) {
    bool done = false;
    Value line;
    if (!sequence_iter_next(iterator, done, line, error)) return false;
    if (done) {
      if (buffer.empty()) { runtime.raise_class_error("StopIteration", ""); return false; }
      if (dialect.strict) { error = "unexpected end of data"; runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error)); return false; }
      if (!dialect.escapechar.empty() && buffer.size() >= dialect.escapechar.size() &&
          buffer.compare(buffer.size() - dialect.escapechar.size(),
                         dialect.escapechar.size(), dialect.escapechar) == 0) {
        buffer.push_back('\n');
      }
    } else {
      std::string text;
      if (!string_value(line, text)) { error = "iterator should return strings, not non-string"; runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error)); return false; }
      buffer += text;
      ++line_num;
      std::string ignored;
      Value self = args[0];
      object_set_attr(self, "line_num", Value::int64(line_num), ignored);
    }
    std::vector<Value> fields;
    bool complete = false;
    if (!parse_record(state, dialect, buffer, done, fields, complete, error)) {
      Value pending;
      if (runtime.take_pending_exception(pending)) runtime.set_pending_exception(std::move(pending));
      else runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error));
      return false;
    }
    if (complete) { out = Value::list(std::move(fields)); return true; }
  }
}

bool reader_kw(Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out, std::string& error, void* user_data) {
  auto& state = *static_cast<CsvState*>(user_data);
  if (argc < 1 || argc > 2) { error = "reader() expected iterator and optional dialect"; runtime.raise_class_error("TypeError", error); return false; }
  Value iterator;
  if (!runtime_get_iter(runtime, args[0], iterator, error)) {
    Value pending;
    if (!runtime.take_pending_exception(pending)) {
      if (args[0].tag == ValueTag::None) error = "'NoneType' object is not iterable";
      runtime.raise_class_error("TypeError", error);
    }
    else runtime.set_pending_exception(std::move(pending));
    return false;
  }
  Value source = argc == 2 ? args[1] : Value::string("excel");
  std::vector<NativeKeywordArg> format_keywords;
  bool explicit_dialect = argc == 2;
  bool quotechar_none = false;
  bool explicit_quoting = false;
  for (uint32_t index = 0; index < kwargc; ++index) {
    if (std::string_view(kwargs[index].name) == "dialect") {
      if (argc == 2) {
        error = "argument for function given by name ('dialect') and position (1)";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      source = *kwargs[index].value;
      explicit_dialect = true;
    } else {
      if (std::string_view(kwargs[index].name) == "quotechar" &&
          kwargs[index].value->tag == ValueTag::None) quotechar_none = true;
      if (std::string_view(kwargs[index].name) == "quoting") explicit_quoting = true;
      format_keywords.push_back(kwargs[index]);
    }
  }
  CsvDialect dialect;
  Value dialect_value;
  if (!resolve_dialect(state, source, dialect, dialect_value, error) ||
      !apply_format_keywords(runtime, dialect, format_keywords.data(),
                             static_cast<uint32_t>(format_keywords.size()), error)) return false;
  if (!explicit_dialect && quotechar_none && !explicit_quoting) dialect.quoting = 3;
  dialect_value = make_dialect_value(state, dialect);
  if (!parse_dialect(runtime, dialect_value, dialect, error)) return false;
  out = Value::instance(state.reader_class);
  object_set_attr(out, "_iterator", iterator, error);
  object_set_attr(out, "dialect", dialect_value, error);
  object_set_attr(out, "line_num", Value::int64(0), error);
  return true;
}

bool reader_fn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  return reader_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

bool register_dialect_kw(Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out, std::string& error, void* user_data) {
  auto& state = *static_cast<CsvState*>(user_data);
  if (argc < 1 || argc > 2 || value_as_string(args[0]) == nullptr) { error = "register_dialect() expected name and optional dialect"; runtime.raise_class_error("TypeError", error); return false; }
  CsvDialect dialect;
  if (argc == 2 && !parse_dialect(runtime, args[1], dialect, error)) return false;
  if (!apply_format_keywords(runtime, dialect, kwargs, kwargc, error)) return false;
  state.dialects[string_object_to_string(*value_as_string(args[0]))] = make_dialect_value(state, dialect);
  value_set_none(out);
  return true;
}

bool register_dialect_fn(Runtime& r, const Value* a, uint32_t n, Value& o, std::string& e, void* d) { return register_dialect_kw(r,a,n,nullptr,0,o,e,d); }

bool get_dialect(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto& state = *static_cast<CsvState*>(user_data);
  if (argc != 1) { error = "get_dialect() expected one argument"; runtime.raise_class_error("TypeError", error); return false; }
  if (value_as_string(args[0]) == nullptr) { error = "unknown dialect"; runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error)); return false; }
  auto found = state.dialects.find(string_object_to_string(*value_as_string(args[0])));
  if (found == state.dialects.end()) { error = "unknown dialect"; runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error)); return false; }
  value_assign_fast(out, found->second); return true;
}

bool unregister_dialect(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto& state = *static_cast<CsvState*>(user_data);
  if (argc != 1) { error = "unregister_dialect() expected one argument"; runtime.raise_class_error("TypeError", error); return false; }
  if (value_as_string(args[0]) == nullptr) { error = "unknown dialect"; runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error)); return false; }
  if (state.dialects.erase(string_object_to_string(*value_as_string(args[0]))) == 0) { error = "unknown dialect"; runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error)); return false; }
  value_set_none(out); return true;
}

bool list_dialects(Runtime& runtime, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 0) { error = "_csv.list_dialects() takes no arguments (" + std::to_string(argc) + " given)"; runtime.raise_class_error("TypeError", error); return false; }
  std::vector<Value> names;
  for (const auto& entry : static_cast<CsvState*>(user_data)->dialects) names.push_back(Value::string(entry.first));
  out = Value::list(std::move(names)); return true;
}

bool field_size_limit(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto& state = *static_cast<CsvState*>(user_data);
  if (argc > 1) { error = "field_size_limit() expected at most 1 argument"; runtime.raise_class_error("TypeError", error); return false; }
  const int64_t old = state.field_limit;
  if (argc == 1 && !value_int_like_to_i64(args[0], state.field_limit)) { error = "limit must be an integer"; runtime.raise_class_error("TypeError", error); return false; }
  value_set_int64(out, old); return true;
}

bool writer_error(CsvState& state, std::string message, std::string& error) {
  error = std::move(message);
  state.runtime->set_pending_exception(
      state.runtime->make_exception_from_class(state.error_class, error));
  return false;
}

bool csv_callable(const Value& value) {
  if (value_as_function(value) != nullptr || value_as_native_function(value) != nullptr ||
      value_as_bound_method(value) != nullptr || value_as_class(value) != nullptr) {
    return true;
  }
  Value call;
  std::string ignored;
  return object_get_attr(value, "__call__", call, ignored);
}

bool writer_field_string(
    Runtime& runtime, const Value& field, std::string& text, std::string& error) {
  if (field.tag == ValueTag::None) {
    text.clear();
    return true;
  }
  if (string_value(field, text)) return true;
  const Value* str_type = runtime.find_builtin("str");
  if (str_type == nullptr) {
    error = "str is not available";
    return false;
  }
  Value converted;
  if (!runtime_call_callable(runtime, *str_type, &field, 1, converted, error)) {
    return false;
  }
  if (!string_value(converted, text)) {
    error = "__str__ returned non-string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return true;
}

bool writer_field_numeric(const Value& field) {
  if (field.tag == ValueTag::Bool || field.tag == ValueTag::Int64 ||
      field.tag == ValueTag::Double || value_as_bigint(field) != nullptr ||
      value_as_complex(field) != nullptr) {
    return true;
  }
  Value method;
  std::string ignored;
  return object_get_attr(field, "__float__", method, ignored) ||
         object_get_attr(field, "__int__", method, ignored) ||
         object_get_attr(field, "__index__", method, ignored);
}

bool terminator_contains(const CsvDialect& dialect, std::string_view character) {
  for (size_t index = 0; index < dialect.lineterminator.size();) {
    const size_t width = utf8_codepoint_width(
        static_cast<unsigned char>(dialect.lineterminator[index]));
    if (std::string_view(dialect.lineterminator).substr(index, width) == character) {
      return true;
    }
    index += width;
  }
  return false;
}

bool writer_append_field(
    CsvState& state,
    const CsvDialect& dialect,
    const Value& field,
    size_t field_count,
    std::string& record,
    std::string& error) {
  std::string text;
  if (!writer_field_string(*state.runtime, field, text, error)) return false;
  const bool nonnull = field.tag != ValueTag::None;
  bool quoted = dialect.quoting == 1 ||
      (dialect.quoting == 2 && !writer_field_numeric(field)) ||
      (dialect.quoting == 4 && value_as_string(field) != nullptr) ||
      (dialect.quoting == 5 && nonnull);
  std::string encoded;
  encoded.reserve(text.size());
  for (size_t index = 0; index < text.size();) {
    const size_t width = utf8_codepoint_width(static_cast<unsigned char>(text[index]));
    const std::string_view character(text.data() + index, width);
    index += width;
    const bool is_quote = !dialect.quotechar.empty() && character == dialect.quotechar;
    const bool is_escape = !dialect.escapechar.empty() && character == dialect.escapechar;
    const bool is_special = character == dialect.delimiter || character == "\r" ||
        character == "\n" || terminator_contains(dialect, character);
    if (is_escape) {
      encoded += dialect.escapechar;
    } else if (is_quote && dialect.quoting != 3) {
      if (dialect.doublequote) {
        quoted = true;
        encoded += dialect.quotechar;
      } else {
        if (dialect.escapechar.empty()) {
          return writer_error(state, "need to escape, but no escapechar set", error);
        }
        encoded += dialect.escapechar;
      }
    } else if (is_quote || is_special) {
      if (dialect.quoting == 3) {
        if (dialect.escapechar.empty()) {
          return writer_error(state, "need to escape, but no escapechar set", error);
        }
        encoded += dialect.escapechar;
      } else {
        quoted = true;
      }
    }
    encoded.append(character.data(), character.size());
  }
  if (field_count == 1 && text.empty() && !quoted) {
    if (dialect.quoting == 3 ||
        (field.tag == ValueTag::None && (dialect.quoting == 4 || dialect.quoting == 5))) {
      return writer_error(state, "single empty field record must be quoted", error);
    }
    quoted = true;
  }
  if (text.empty() && dialect.delimiter == " " && dialect.skipinitialspace && !quoted) {
    if (dialect.quoting == 3 ||
        (field.tag == ValueTag::None && (dialect.quoting == 4 || dialect.quoting == 5))) {
      return writer_error(state, "empty field must be quoted if delimiter is a space", error);
    }
    quoted = true;
  }
  if (quoted) record += dialect.quotechar;
  record += encoded;
  if (quoted) record += dialect.quotechar;
  return true;
}

bool writerow_impl(
    CsvState& state, const Value& self, const Value& row, Value& out, std::string& error) {
  Value iterator;
  if (!runtime_get_iter(*state.runtime, row, iterator, error)) {
    Value pending;
    if (state.runtime->take_pending_exception(pending)) {
      state.runtime->set_pending_exception(std::move(pending));
      return false;
    }
    return writer_error(
        state, "iterable expected, not " + std::string(value_binary_type_name(row)), error);
  }
  std::vector<Value> fields;
  for (;;) {
    bool done = false;
    Value field;
    if (!sequence_iter_next(iterator, done, field, error)) return false;
    if (done) break;
    fields.push_back(std::move(field));
  }
  Value dialect_value;
  Value write;
  if (!object_get_attr(self, "dialect", dialect_value, error) ||
      !object_get_attr(self, "_write", write, error)) {
    return false;
  }
  CsvDialect dialect;
  if (!parse_dialect(*state.runtime, dialect_value, dialect, error)) return false;
  std::string record;
  for (size_t index = 0; index < fields.size(); ++index) {
    if (index != 0) record += dialect.delimiter;
    if (!writer_append_field(state, dialect, fields[index], fields.size(), record, error)) {
      return false;
    }
  }
  record += dialect.lineterminator;
  Value argument = Value::string(std::move(record));
  return runtime_call_callable(*state.runtime, write, &argument, 1, out, error);
}

bool writerow(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "writerow() takes exactly one argument";
    static_cast<CsvState*>(user_data)->runtime->raise_class_error("TypeError", error);
    return false;
  }
  return writerow_impl(*static_cast<CsvState*>(user_data), args[0], args[1], out, error);
}

bool writerows(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 2) {
    error = "writerows() takes exactly one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value iterator;
  if (!runtime_get_iter(runtime, args[1], iterator, error)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      runtime.set_pending_exception(std::move(pending));
      return false;
    }
    error = "'" + std::string(value_binary_type_name(args[1])) +
        "' object is not iterable";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (;;) {
    bool done = false;
    Value row;
    if (!sequence_iter_next(iterator, done, row, error)) return false;
    if (done) break;
    Value ignored;
    if (!writerow_impl(*static_cast<CsvState*>(user_data), args[0], row, ignored, error)) {
      return false;
    }
  }
  value_set_none(out);
  return true;
}

bool writer_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc,
    Value& out, std::string& error, void* user_data) {
  auto& state = *static_cast<CsvState*>(user_data);
  if (argc < 1 || argc > 2) {
    error = "writer() expected output and optional dialect";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value write;
  const Value* getattr_function = runtime.find_builtin("getattr");
  Value getattr_args[] = {args[0], Value::string("write")};
  if (getattr_function == nullptr ||
      !runtime_call_callable(runtime, *getattr_function, getattr_args, 2, write, error) ||
      !csv_callable(write)) {
    Value pending;
    if (runtime.take_pending_exception(pending)) {
      Value type = runtime.exception_type(pending);
      if (auto* klass = value_as_class(type); klass != nullptr &&
          klass->name != "AttributeError") {
        runtime.set_pending_exception(std::move(pending));
        return false;
      }
    }
    error = "argument 1 must have a \"write\" method";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value source = argc == 2 ? args[1] : Value::string("excel");
  std::vector<NativeKeywordArg> format_keywords;
  for (uint32_t index = 0; index < kwargc; ++index) {
    if (std::string_view(kwargs[index].name) == "dialect") {
      if (argc == 2) {
        error = "argument for function given by name ('dialect') and position (1)";
        runtime.raise_class_error("TypeError", error);
        return false;
      }
      source = *kwargs[index].value;
    } else {
      format_keywords.push_back(kwargs[index]);
    }
  }
  CsvDialect dialect;
  Value dialect_value;
  if (!resolve_dialect(state, source, dialect, dialect_value, error) ||
      !apply_format_keywords(runtime, dialect, format_keywords.data(),
                             static_cast<uint32_t>(format_keywords.size()), error)) {
    return false;
  }
  dialect_value = make_dialect_value(state, dialect);
  if (!parse_dialect(runtime, dialect_value, dialect, error)) return false;
  out = Value::instance(state.writer_class);
  if (!object_set_attr(out, "_write", write, error) ||
      !object_set_attr(out, "dialect", dialect_value, error)) {
    return false;
  }
  return true;
}

bool writer_fn(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  return writer_kw(runtime, args, argc, nullptr, 0, out, error, data);
}

} // namespace

void register_csv_module(Runtime& runtime) {
  auto* state = new CsvState();
  state->runtime = &runtime;
  const Value* object = runtime.find_builtin("object");
  const Value* exception = runtime.find_builtin("Exception");
  const Value base = object == nullptr ? Value::invalid() : *object;
  state->error_class = Value::class_object("Error", {{"__module__", Value::string("_csv")}}, exception == nullptr ? base : *exception);
  state->dialect_class = Value::class_object("Dialect", {
      {"__module__", Value::string("_csv")},
      {"__setattr__", runtime.make_native_function("_csv.Dialect.__setattr__", dialect_readonly)},
      {"__delattr__", runtime.make_native_function("_csv.Dialect.__delattr__", dialect_readonly)},
      {"__reduce__", runtime.make_native_function("_csv.Dialect.__reduce__", dialect_reduce)},
      {"__reduce_ex__", runtime.make_native_function("_csv.Dialect.__reduce_ex__", dialect_reduce)},
  }, base);
  value_as_class(state->dialect_class)->attrs["__init__"] = runtime.make_native_function("_csv.Dialect.__init__", dialect_init, state);
  state->reader_class = Value::class_object("reader", {{"__module__", Value::string("_csv")}}, base);
  value_as_class(state->reader_class)->attrs["__iter__"] = runtime.make_native_function("_csv.reader.__iter__", reader_iter);
  value_as_class(state->reader_class)->attrs["__next__"] = runtime.make_native_function("_csv.reader.__next__", reader_next, state);
  state->writer_class = Value::class_object("writer", {{"__module__", Value::string("_csv")}}, base);
  value_as_class(state->writer_class)->attrs["writerow"] = runtime.make_native_function("_csv.writer.writerow", writerow, state);
  value_as_class(state->writer_class)->attrs["writerows"] = runtime.make_native_function("_csv.writer.writerows", writerows, state);
  runtime.register_native_package_cleanup(state, [](void* data) { delete static_cast<CsvState*>(data); });

  CsvDialect excel;
  state->dialects["excel"] = make_dialect_value(*state, excel);

  NativeModuleBuilder builder(runtime, "_csv");
  builder.value("Error", state->error_class)
      .value("Dialect", state->dialect_class)
      .value("__version__", Value::string("1.0"))
      .value("QUOTE_MINIMAL", Value::int64(0)).value("QUOTE_ALL", Value::int64(1))
      .value("QUOTE_NONNUMERIC", Value::int64(2)).value("QUOTE_NONE", Value::int64(3))
      .value("QUOTE_STRINGS", Value::int64(4)).value("QUOTE_NOTNULL", Value::int64(5))
      .value("reader", runtime.make_native_function("_csv.reader", reader_fn, state, nullptr, nullptr, false, reader_kw, false))
      .value("writer", runtime.make_native_function("_csv.writer", writer_fn, state, nullptr, nullptr, false, writer_kw, false))
      .value("register_dialect", runtime.make_native_function("_csv.register_dialect", register_dialect_fn, state, nullptr, nullptr, false, register_dialect_kw, false))
      .value("unregister_dialect", runtime.make_native_function("_csv.unregister_dialect", unregister_dialect, state))
      .value("get_dialect", runtime.make_native_function("_csv.get_dialect", get_dialect, state))
      .value("list_dialects", runtime.make_native_function("_csv.list_dialects", list_dialects, state))
      .value("field_size_limit", runtime.make_native_function("_csv.field_size_limit", field_size_limit, state));
  runtime.register_module("_csv", builder.finish());
}

} // namespace xlang3
