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
  if (value.tag == ValueTag::None) {
    out.clear();
    return true;
  }
  if (!string_value(value, out) || out.size() != 1) {
    error = std::string(name) + " must be a 1-character string";
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
      error = "lineterminator must be a string";
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
    error = "bad quoting value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (dialect.quoting != 3 && dialect.quotechar.empty()) {
    error = "quotechar must be set if quoting enabled";
    runtime.raise_class_error("TypeError", error);
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
    else if (name == "lineterminator") { if (!string_value(value, dialect.lineterminator)) { error = "lineterminator must be a string"; runtime.raise_class_error("TypeError", error); return false; } }
    else if (name == "doublequote") dialect.doublequote = value_truthy(value);
    else if (name == "skipinitialspace") dialect.skipinitialspace = value_truthy(value);
    else if (name == "strict") dialect.strict = value_truthy(value);
    else if (name == "quoting") { if (!value_int_like_to_i64(value, dialect.quoting)) { error = "quoting must be an integer"; runtime.raise_class_error("TypeError", error); return false; } }
    else { error = "invalid keyword argument for this function"; runtime.raise_class_error("TypeError", error); return false; }
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

bool reader_iter(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "reader.__iter__ expected no arguments"; return false; }
  value_assign_fast(out, args[0]);
  return true;
}

bool parse_record(CsvState& state, const CsvDialect& dialect, std::string& buffer, std::vector<Value>& fields, bool& complete, std::string& error) {
  fields.clear();
  std::string field;
  bool quoted = false;
  bool field_was_quoted = false;
  bool at_field_start = true;
  for (size_t i = 0; i < buffer.size(); ++i) {
    const char ch = buffer[i];
    if (quoted) {
      if (!dialect.escapechar.empty() && ch == dialect.escapechar[0]) {
        if (i + 1 < buffer.size()) field.push_back(buffer[++i]);
        else field.push_back(ch);
      } else if (!dialect.quotechar.empty() && ch == dialect.quotechar[0]) {
        if (dialect.doublequote && i + 1 < buffer.size() && buffer[i + 1] == ch) { field.push_back(ch); ++i; }
        else quoted = false;
      } else field.push_back(ch);
      continue;
    }
    if (at_field_start && dialect.skipinitialspace && ch == ' ') continue;
    if (at_field_start && !dialect.quotechar.empty() && ch == dialect.quotechar[0] && dialect.quoting != 3) {
      quoted = true; field_was_quoted = true; at_field_start = false; continue;
    }
    if (!dialect.escapechar.empty() && ch == dialect.escapechar[0]) {
      if (i + 1 < buffer.size()) field.push_back(buffer[++i]);
      else field.push_back(ch);
      at_field_start = false;
      continue;
    }
    if (ch == dialect.delimiter[0]) {
      fields.push_back(Value::string(field)); field.clear(); field_was_quoted = false; at_field_start = true; continue;
    }
    if (ch == '\r' || ch == '\n') {
      if (ch == '\r' && i + 1 < buffer.size() && buffer[i + 1] == '\n') ++i;
      if (i + 1 != buffer.size()) { error = "new-line character seen in unquoted field"; return false; }
      fields.push_back(Value::string(field)); complete = true; return true;
    }
    field.push_back(ch); at_field_start = false;
    if (static_cast<int64_t>(field.size()) > state.field_limit) { error = "field larger than field limit"; return false; }
  }
  if (quoted) { complete = false; return true; }
  fields.push_back(Value::string(field));
  complete = true;
  return true;
}

bool reader_next(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 1) { error = "reader.__next__ expected no arguments"; return false; }
  auto& state = *static_cast<CsvState*>(user_data);
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
      buffer.push_back('\n');
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
    if (!parse_record(state, dialect, buffer, fields, complete, error)) { runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error)); return false; }
    if (complete) { out = Value::list(std::move(fields)); return true; }
  }
}

bool reader_kw(Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out, std::string& error, void* user_data) {
  auto& state = *static_cast<CsvState*>(user_data);
  if (argc < 1 || argc > 2) { error = "reader() expected iterator and optional dialect"; runtime.raise_class_error("TypeError", error); return false; }
  Value iterator;
  if (!runtime_get_iter(runtime, args[0], iterator, error)) return false;
  Value source = argc == 2 ? args[1] : Value::string("excel");
  CsvDialect dialect;
  Value dialect_value;
  if (!resolve_dialect(state, source, dialect, dialect_value, error) || !apply_format_keywords(runtime, dialect, kwargs, kwargc, error)) return false;
  dialect_value = make_dialect_value(state, dialect);
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
  if (argc != 1 || value_as_string(args[0]) == nullptr) { error = "get_dialect() expected a string"; runtime.raise_class_error("TypeError", error); return false; }
  auto found = state.dialects.find(string_object_to_string(*value_as_string(args[0])));
  if (found == state.dialects.end()) { error = "unknown dialect"; runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error)); return false; }
  value_assign_fast(out, found->second); return true;
}

bool unregister_dialect(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  auto& state = *static_cast<CsvState*>(user_data);
  if (argc != 1 || value_as_string(args[0]) == nullptr) { error = "unregister_dialect() expected a string"; runtime.raise_class_error("TypeError", error); return false; }
  if (state.dialects.erase(string_object_to_string(*value_as_string(args[0]))) == 0) { error = "unknown dialect"; runtime.set_pending_exception(runtime.make_exception_from_class(state.error_class, error)); return false; }
  value_set_none(out); return true;
}

bool list_dialects(Runtime&, const Value*, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc != 0) { error = "list_dialects() expected no arguments"; return false; }
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

bool writer_unavailable(Runtime& runtime, const Value*, uint32_t, Value&, std::string& error, void*) {
  error = "writer support is not available";
  runtime.raise_class_error("NotImplementedError", error);
  return false;
}

} // namespace

void register_csv_module(Runtime& runtime) {
  auto* state = new CsvState();
  state->runtime = &runtime;
  const Value* object = runtime.find_builtin("object");
  const Value* exception = runtime.find_builtin("Exception");
  const Value base = object == nullptr ? Value::invalid() : *object;
  state->error_class = Value::class_object("Error", {{"__module__", Value::string("_csv")}}, exception == nullptr ? base : *exception);
  state->dialect_class = Value::class_object("Dialect", {{"__module__", Value::string("_csv")}}, base);
  value_as_class(state->dialect_class)->attrs["__init__"] = runtime.make_native_function("_csv.Dialect.__init__", dialect_init, state);
  state->reader_class = Value::class_object("reader", {{"__module__", Value::string("_csv")}}, base);
  value_as_class(state->reader_class)->attrs["__iter__"] = runtime.make_native_function("_csv.reader.__iter__", reader_iter);
  value_as_class(state->reader_class)->attrs["__next__"] = runtime.make_native_function("_csv.reader.__next__", reader_next, state);
  state->writer_class = Value::class_object("writer", {{"__module__", Value::string("_csv")}}, base);
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
      .value("writer", runtime.make_native_function("_csv.writer", writer_unavailable, state))
      .value("register_dialect", runtime.make_native_function("_csv.register_dialect", register_dialect_fn, state, nullptr, nullptr, false, register_dialect_kw, false))
      .value("unregister_dialect", runtime.make_native_function("_csv.unregister_dialect", unregister_dialect, state))
      .value("get_dialect", runtime.make_native_function("_csv.get_dialect", get_dialect, state))
      .value("list_dialects", runtime.make_native_function("_csv.list_dialects", list_dialects, state))
      .value("field_size_limit", runtime.make_native_function("_csv.field_size_limit", field_size_limit, state));
  runtime.register_module("_csv", builder.finish());
}

} // namespace xlang3
