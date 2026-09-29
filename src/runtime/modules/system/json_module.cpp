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

#include "xlang3/attribute.h"
#include "xlang3/builtin_methods.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"
#include "xlang3/value.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <vector>

namespace xlang3 {
namespace {

bool call_python_json_helper(
    Runtime& runtime,
    const char* module_name,
    const char* helper_name,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  Value module;
  Value helper;
  if (!runtime.import_module(module_name, module, error) ||
      !module_get_attr(module, helper_name, helper, error)) {
    return false;
  }
  std::vector<std::pair<std::string, Value>> keyword_values;
  keyword_values.reserve(kwargc);
  for (uint32_t index = 0; index < kwargc; ++index) {
    if (kwargs[index].name != nullptr && kwargs[index].value != nullptr) {
      keyword_values.emplace_back(kwargs[index].name, *kwargs[index].value);
    }
  }
  return runtime_call_callable_kw(runtime, helper, args, argc, keyword_values, out, error);
}

bool json_encode_basestring(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  return call_python_json_helper(
      runtime, "json.encoder", "py_encode_basestring", args, argc,
      nullptr, 0, out, error);
}

bool json_encode_basestring_ascii(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  return call_python_json_helper(
      runtime, "json.encoder", "py_encode_basestring_ascii", args, argc,
      nullptr, 0, out, error);
}

bool json_scanstring(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  return call_python_json_helper(
      runtime, "json.decoder", "py_scanstring", args, argc,
      nullptr, 0, out, error);
}

bool json_scanstring_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out,
    std::string& error, void*) {
  return call_python_json_helper(
      runtime, "json.decoder", "py_scanstring", args, argc,
      kwargs, kwargc, out, error);
}

struct JsonScannerState {
  Value memo;
  bool strict = true;
};

void json_scanner_state_cleanup(void* data) {
  delete static_cast<JsonScannerState*>(data);
}

class JsonBuiltinParser {
public:
  // Keep the default JSONDecoder path in one native recursive-descent pass:
  // per-token Python calls dominate small JSON documents. Custom hooks still
  // use json.scanner.py_make_scanner, preserving the stdlib extension points.
  JsonBuiltinParser(Runtime& runtime, std::string_view source, size_t start, bool strict, Value& memo)
      : runtime_(runtime), source_(source), pos_(start), strict_(strict), memo_(memo) {}

  bool parse(Value& out, size_t& end) {
    if (!parse_value(out, 0)) return false;
    end = pos_;
    return true;
  }

  size_t position() const { return pos_; }

private:
  void whitespace() {
    while (pos_ < source_.size()) {
      const char c = source_[pos_];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
      ++pos_;
    }
  }

  bool hex4(uint32_t& value) {
    if (source_.size() - pos_ < 4) return false;
    value = 0;
    for (size_t i = 0; i < 4; ++i) {
      const char c = source_[pos_++];
      uint32_t digit;
      if (c >= '0' && c <= '9') digit = static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') digit = static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') digit = static_cast<uint32_t>(c - 'A' + 10);
      else return false;
      value = (value << 4) | digit;
    }
    return true;
  }

  static void append_utf8(uint32_t cp, std::string& out) {
    if (cp <= 0x7f) out.push_back(static_cast<char>(cp));
    else if (cp <= 0x7ff) {
      out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
      out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
  }

  bool parse_string(Value& out) {
    if (pos_ >= source_.size() || source_[pos_] != '"') return false;
    ++pos_;
    std::string decoded;
    const size_t content_start = pos_;
    while (pos_ < source_.size()) {
      const unsigned char c = static_cast<unsigned char>(source_[pos_++]);
      if (c == '"') {
        if (decoded.empty() && pos_ - content_start >= 1) {
          out = Value::string(std::string(source_.substr(content_start, pos_ - content_start - 1)));
        } else {
          out = Value::string(std::move(decoded));
        }
        return true;
      }
      if (c >= 0x80) {
        if (!decoded.empty()) decoded.push_back(static_cast<char>(c));
        continue;
      }
      if (c < 0x20 && strict_) return false;
      if (c != '\\') {
        if (!decoded.empty()) decoded.push_back(static_cast<char>(c));
        continue;
      }
      if (decoded.empty()) {
        decoded.assign(source_.substr(content_start, pos_ - content_start - 1));
      }
      if (pos_ >= source_.size()) return false;
      const char escape = source_[pos_++];
      switch (escape) {
        case '"': decoded.push_back('"'); break;
        case '\\': decoded.push_back('\\'); break;
        case '/': decoded.push_back('/'); break;
        case 'b': decoded.push_back('\b'); break;
        case 'f': decoded.push_back('\f'); break;
        case 'n': decoded.push_back('\n'); break;
        case 'r': decoded.push_back('\r'); break;
        case 't': decoded.push_back('\t'); break;
        case 'u': {
          uint32_t cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xd800 && cp <= 0xdbff) {
            if (source_.size() - pos_ < 6 || source_[pos_] != '\\' || source_[pos_ + 1] != 'u') return false;
            pos_ += 2;
            uint32_t low;
            if (!hex4(low) || low < 0xdc00 || low > 0xdfff) return false;
            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
          } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            return false; // Preserve isolated-surrogate behavior through json.decoder.
          }
          append_utf8(cp, decoded);
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  bool memoize_key(Value& key) {
    Value existing;
    std::string ignored;
    if (mapping_get_item(memo_, key, existing, ignored)) {
      key = std::move(existing);
      return true;
    }
    ignored.clear();
    return mapping_set_item(memo_, key, key, ignored);
  }

  bool parse_number(Value& out) {
    const size_t begin = pos_;
    if (source_[pos_] == '-') ++pos_;
    if (pos_ >= source_.size()) return false;
    if (source_[pos_] == '0') ++pos_;
    else {
      if (source_[pos_] < '1' || source_[pos_] > '9') return false;
      while (pos_ < source_.size() && source_[pos_] >= '0' && source_[pos_] <= '9') ++pos_;
    }
    bool floating = false;
    if (pos_ < source_.size() && source_[pos_] == '.') {
      floating = true; ++pos_;
      const size_t digits = pos_;
      while (pos_ < source_.size() && source_[pos_] >= '0' && source_[pos_] <= '9') ++pos_;
      if (digits == pos_) return false;
    }
    if (pos_ < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E')) {
      floating = true; ++pos_;
      if (pos_ < source_.size() && (source_[pos_] == '+' || source_[pos_] == '-')) ++pos_;
      const size_t digits = pos_;
      while (pos_ < source_.size() && source_[pos_] >= '0' && source_[pos_] <= '9') ++pos_;
      if (digits == pos_) return false;
    }
    const auto token = source_.substr(begin, pos_ - begin);
    if (floating) {
      double number = 0.0;
      const auto result = std::from_chars(token.data(), token.data() + token.size(), number, std::chars_format::general);
      if (result.ptr != token.data() + token.size()) return false;
      if (result.ec == std::errc::result_out_of_range) {
        // from_chars reports both overflow and underflow as out_of_range. Let
        // the configured default float implementation resolve only these rare
        // boundary cases; ordinary JSON floats stay entirely on the native path.
        const Value* float_class = runtime_.find_builtin("float");
        Value parsed;
        Value text_value = Value::string(std::string(token));
        std::string error;
        if (float_class == nullptr ||
            !runtime_call_callable(runtime_, *float_class, &text_value, 1, parsed, error) ||
            parsed.tag != ValueTag::Double) {
          return false;
        }
        number = parsed.as.f64;
      } else if (result.ec != std::errc{}) {
        return false;
      }
      out = Value::number(number);
      return true;
    }
    int64_t integer = 0;
    const auto result = std::from_chars(token.data(), token.data() + token.size(), integer);
    if (result.ec == std::errc{} && result.ptr == token.data() + token.size()) {
      out = Value::int64(integer);
      return true;
    }
    std::string error;
    out = value_bigint_from_decimal(token, 10, error);
    return out.tag != ValueTag::Invalid;
  }

  bool parse_array(Value& out, size_t depth) {
    ++pos_;
    whitespace();
    if (pos_ < source_.size() && source_[pos_] == ']') {
      ++pos_; out = Value::list({}); return true;
    }
    std::vector<Value> values;
    while (true) {
      Value item;
      if (!parse_value(item, depth + 1)) return false;
      values.push_back(std::move(item));
      whitespace();
      if (pos_ >= source_.size()) return false;
      const char separator = source_[pos_++];
      if (separator == ']') break;
      if (separator != ',') return false;
      whitespace();
    }
    out = Value::list(std::move(values));
    return true;
  }

  bool parse_object(Value& out, size_t depth) {
    ++pos_;
    whitespace();
    out = Value::dict({});
    if (pos_ < source_.size() && source_[pos_] == '}') { ++pos_; return true; }
    while (true) {
      Value key;
      if (!parse_string(key) || !memoize_key(key)) return false;
      whitespace();
      if (pos_ >= source_.size() || source_[pos_++] != ':') return false;
      whitespace();
      Value item;
      if (!parse_value(item, depth + 1)) return false;
      std::string error;
      if (!mapping_set_item(out, key, item, error)) return false;
      whitespace();
      if (pos_ >= source_.size()) return false;
      const char separator = source_[pos_++];
      if (separator == '}') break;
      if (separator != ',') return false;
      whitespace();
    }
    return true;
  }

  bool parse_value(Value& out, size_t depth) {
    if (depth > 900) return false;
    whitespace();
    if (pos_ >= source_.size()) return false;
    const char c = source_[pos_];
    if (c == '"') return parse_string(out);
    if (c == '[') return parse_array(out, depth);
    if (c == '{') return parse_object(out, depth);
    if (c == 'n' && source_.substr(pos_, 4) == "null") { pos_ += 4; out = Value::none(); return true; }
    if (c == 't' && source_.substr(pos_, 4) == "true") { pos_ += 4; out = Value::boolean(true); return true; }
    if (c == 'f' && source_.substr(pos_, 5) == "false") { pos_ += 5; out = Value::boolean(false); return true; }
    if (c == '-' || (c >= '0' && c <= '9')) return parse_number(out);
    return false;
  }

  std::string_view source_;
  size_t pos_ = 0;
  bool strict_ = true;
  Runtime& runtime_;
  Value& memo_;
};

bool json_scanner_call(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* data) {
  auto* state = static_cast<JsonScannerState*>(data);
  if (argc != 2 || value_as_string(args[0]) == nullptr || args[1].tag != ValueTag::Int64 || args[1].as.i64 < 0) {
    error = "JSON scanner expected a string and integer index";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto text = string_object_view(*value_as_string(args[0]));
  const size_t start = utf8_byte_offset(text, static_cast<size_t>(args[1].as.i64));
  if (static_cast<size_t>(args[1].as.i64) > utf8_codepoint_count(text)) {
    error = "JSON scanner index out of range";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  JsonBuiltinParser parser(runtime, text, start, state->strict, state->memo);
  Value result;
  size_t end_byte = 0;
  if (!parser.parse(result, end_byte)) {
    const size_t error_index = utf8_codepoint_count(text.substr(0, parser.position()));
    std::string ignored;
    (void)mapping_clear(state->memo, ignored);
    const Value* stop_iteration = runtime.find_builtin("StopIteration");
    Value stop = stop_iteration != nullptr
        ? runtime.make_exception_from_class(*stop_iteration, std::to_string(error_index))
        : runtime.make_exception("StopIteration", std::to_string(error_index));
    const Value index = Value::int64(static_cast<int64_t>(error_index));
    std::string attr_error;
    (void)object_set_attr(stop, "value", index, attr_error);
    (void)object_set_attr(stop, "args", Value::tuple({index}), attr_error);
    runtime.set_pending_exception(std::move(stop));
    error = "JSON scanner stopped at invalid input";
    return false;
  }
  std::string ignored;
  (void)mapping_clear(state->memo, ignored);
  const size_t end = utf8_codepoint_count(text.substr(0, end_byte));
  out = Value::tuple({std::move(result), Value::int64(static_cast<int64_t>(end))});
  return true;
}

bool json_make_scanner(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  auto python_fallback = [&]() {
    return call_python_json_helper(
        runtime, "json.scanner", "py_make_scanner", args, argc,
        nullptr, 0, out, error);
  };
  if (argc != 1) return python_fallback();
  Value strict;
  Value parse_float;
  Value parse_int;
  Value object_hook;
  Value object_pairs_hook;
  Value memo;
  const Value* float_class = runtime.find_builtin("float");
  const Value* int_class = runtime.find_builtin("int");
  bool strict_enabled = false;
  const bool native_eligible =
      float_class != nullptr && int_class != nullptr &&
      object_get_attr(args[0], "strict", strict, error) &&
      runtime_truthy(runtime, strict, strict_enabled, error) && strict_enabled &&
      object_get_attr(args[0], "parse_float", parse_float, error) && value_is(parse_float, *float_class) &&
      object_get_attr(args[0], "parse_int", parse_int, error) && value_is(parse_int, *int_class) &&
      object_get_attr(args[0], "object_hook", object_hook, error) && object_hook.tag == ValueTag::None &&
      object_get_attr(args[0], "object_pairs_hook", object_pairs_hook, error) && object_pairs_hook.tag == ValueTag::None &&
      object_get_attr(args[0], "memo", memo, error) && value_as_dict(memo) != nullptr;
  if (!native_eligible) {
    error.clear();
    return python_fallback();
  }
  auto* state = new JsonScannerState{std::move(memo), true};
  out = runtime.make_native_function(
      "_json.Scanner.__call__", json_scanner_call, state,
      json_scanner_state_cleanup, nullptr, false, nullptr, false);
  return true;
}

struct JsonFloatState {
  Value allow_nan;
};

bool json_floatstr(Runtime& runtime, const Value* args, uint32_t argc,
                   Value& out, std::string& error, void* data);

struct JsonEncoderState {
  Value iterencode;
  bool native_fast_path = false;
  Value bound[9];
};

bool initialize_json_python_encoder(Runtime& runtime, JsonEncoderState& state,
                                    std::string& error);

// The default json.dumps hot path is deliberately handled here in C++: this is
// the _json accelerator, not a replacement for json.encoder. Keep the Python
// fallback below for custom objects/options so the stdlib remains the source
// of truth for extensible behavior.
void append_json_ascii_string(std::string_view input, std::string& out) {
  static constexpr char hex[] = "0123456789abcdef";
  out.push_back('"');
  for (size_t i = 0; i < input.size();) {
    const auto c = static_cast<uint8_t>(input[i]);
    if (c == '"' || c == '\\') {
      out.push_back('\\'); out.push_back(static_cast<char>(c)); ++i;
    } else if (c < 0x20) {
      switch (c) {
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
          out += "\\u00"; out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]);
      }
      ++i;
    } else if (c < 0x80) {
      out.push_back(static_cast<char>(c)); ++i;
    } else {
      uint32_t cp = 0; size_t width = 0;
      if ((c & 0xe0) == 0xc0 && i + 1 < input.size()) { cp = c & 0x1f; width = 2; }
      else if ((c & 0xf0) == 0xe0 && i + 2 < input.size()) { cp = c & 0x0f; width = 3; }
      else if ((c & 0xf8) == 0xf0 && i + 3 < input.size()) { cp = c & 0x07; width = 4; }
      if (!width) { out.push_back(static_cast<char>(c)); ++i; continue; }
      for (size_t j = 1; j < width; ++j) cp = (cp << 6) | (static_cast<uint8_t>(input[i + j]) & 0x3f);
      if (cp < 0x10000) {
        out += "\\u";
        for (int shift = 12; shift >= 0; shift -= 4) out.push_back(hex[(cp >> shift) & 15]);
      } else {
        cp -= 0x10000;
        const uint32_t hi = 0xd800 + (cp >> 10), lo = 0xdc00 + (cp & 0x3ff);
        for (uint32_t unit : {hi, lo}) {
          out += "\\u";
          for (int shift = 12; shift >= 0; shift -= 4) out.push_back(hex[(unit >> shift) & 15]);
        }
      }
      i += width;
    }
  }
  out.push_back('"');
}

struct JsonActiveStack {
  // Most JSON graphs are shallow. Keep the cycle-detection path in the
  // encoder's stack frame so each json.dumps call avoids heap allocation; only
  // unusually deep input spills to the vector.
  std::array<Object*, 32> inline_items{};
  size_t size = 0;
  std::vector<Object*> overflow;

  bool contains(Object* object) const {
    const size_t inline_size = std::min(size, inline_items.size());
    if (std::find(inline_items.begin(), inline_items.begin() + inline_size,
                  object) != inline_items.begin() + inline_size) {
      return true;
    }
    return std::find(overflow.begin(), overflow.end(), object) != overflow.end();
  }

  void push(Object* object) {
    if (size < inline_items.size()) inline_items[size] = object;
    else overflow.push_back(object);
    ++size;
  }

  void pop() {
    if (size > inline_items.size()) overflow.pop_back();
    --size;
  }
};

bool append_json_builtin(const Value& value, std::string& out,
                         JsonActiveStack& active, bool& supported,
                         size_t depth = 0) {
  // Match the interpreter's recursion safety by falling back before native C++
  // recursion can exhaust the process stack on deeply nested input.
  if (depth > 900) { supported = false; return false; }
  switch (value.tag) {
    case ValueTag::None: out += "null"; return true;
    case ValueTag::Bool: out += value.as.b ? "true" : "false"; return true;
    case ValueTag::Int64: {
      char buffer[32]; auto result = std::to_chars(buffer, buffer + sizeof(buffer), value.as.i64);
      out.append(buffer, result.ptr); return true;
    }
    case ValueTag::Double:
      if (std::isnan(value.as.f64)) out += "NaN";
      else if (std::isinf(value.as.f64)) out += value.as.f64 < 0 ? "-Infinity" : "Infinity";
      else out += value_to_repr(value);
      return true;
    default: break;
  }
  if (value_as_bigint(value) != nullptr) {
    out += value_bigint_to_string(value); return true;
  }
  if (auto* string = value_as_string(value)) {
    append_json_ascii_string(string_object_view(*string), out); return true;
  }
  auto* list = value_as_list(value);
  auto* tuple = value_as_tuple(value);
  auto* dict = value_as_dict(value);
  if (list || tuple) {
    Object* identity = value.as.obj;
    // JSON only needs to detect cycles on the current recursion path. A small
    // stack avoids hashing and allocating a node for every container in large
    // acyclic graphs (the common pyperformance json_dumps workload).
    if (active.contains(identity)) {
      supported = false;
      return false;
    }
    active.push(identity);
    out.push_back('[');
    const size_t count = list ? list->items.size() : tuple->items.size();
    for (size_t i = 0; i < count; ++i) {
      if (i) out += ", ";
      const Value& item = list ? list->items[i] : tuple->items[i];
      if (!append_json_builtin(item, out, active, supported, depth + 1)) { active.pop(); return false; }
    }
    out.push_back(']'); active.pop(); return true;
  }
  if (dict) {
    Object* identity = value.as.obj;
    if (active.contains(identity)) {
      supported = false;
      return false;
    }
    active.push(identity);
    out.push_back('{'); bool first = true;
    for (const auto& [key, item] : dict->entries) {
      auto* key_string = value_as_string(key);
      if (!key_string) { supported = false; active.pop(); return false; }
      if (!first) out += ", "; first = false;
      append_json_ascii_string(string_object_view(*key_string), out); out += ": ";
      if (!append_json_builtin(item, out, active, supported, depth + 1)) { active.pop(); return false; }
    }
    out.push_back('}'); active.pop(); return true;
  }
  supported = false; return false;
}

void json_float_state_cleanup(void* data) {
  delete static_cast<JsonFloatState*>(data);
}

bool initialize_json_python_encoder(Runtime& runtime, JsonEncoderState& state,
                                    std::string& error) {
  auto* float_state = new JsonFloatState{state.bound[8]};
  Value floatstr = runtime.make_native_function(
      "_json._floatstr", json_floatstr, float_state, json_float_state_cleanup,
      nullptr, false, nullptr, false);
  Value helper_args[] = {
      state.bound[0], state.bound[1], state.bound[2], state.bound[3], floatstr,
      state.bound[4], state.bound[5], state.bound[6], state.bound[7], Value::boolean(true)};
  return call_python_json_helper(
      runtime, "json.encoder", "_make_iterencode", helper_args, 10,
      nullptr, 0, state.iterencode, error);
}

void json_encoder_state_cleanup(void* data) {
  delete static_cast<JsonEncoderState*>(data);
}

bool json_encoder_python_fallback(
    Runtime& runtime, const Value* args, JsonEncoderState& state,
    Value& out, std::string& error) {
  if (state.iterencode.tag == ValueTag::Invalid &&
      !initialize_json_python_encoder(runtime, state, error)) {
    return false;
  }
  Value call_args[] = {args[0], args[1].as.i64 < 0 ? Value::int64(0) : args[1]};
  Value generated;
  if (!runtime_call_callable(runtime, state.iterencode, call_args, 2, generated, error)) {
    return false;
  }
  const Value* list_class = runtime.find_builtin("list");
  if (list_class == nullptr) {
    error = "list constructor is unavailable";
    return false;
  }
  Value chunks;
  if (!runtime_call_callable(runtime, *list_class, &generated, 1, chunks, error)) {
    return false;
  }
  Value empty = Value::string("");
  Value join;
  if (!attribute_get(empty, "join", join, error)) {
    return false;
  }
  Value joined;
  if (!runtime_call_callable(runtime, join, &chunks, 1, joined, error)) {
    return false;
  }
  out = Value::list({joined});
  return true;
}

bool json_encoder_call(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* data) {
  if (argc != 2) {
    error = "_iterencode() takes exactly 2 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (args[1].tag != ValueTag::Int64) {
    error = "_current_indent_level must be an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (data == nullptr) {
    // The registered default encoder has no captured options, so it can be
    // reused by every default JSONEncoder.iterencode call. Custom encoders
    // keep their own state and use the ordinary Python fallback.
    std::string encoded;
    JsonActiveStack active;
    bool supported = true;
    if (append_json_builtin(args[0], encoded, active, supported)) {
      out = Value::list({Value::string(std::move(encoded))});
      return true;
    }
    // Construct fallback state only for unsupported values. The frequent
    // built-in path avoids initializing the Python encoder options.
    JsonEncoderState fallback;
    fallback.bound[0] = Value::dict({});
    fallback.bound[1] = Value::none();
    const Value* ascii_encoder = runtime.find_native_symbol("_json.encode_basestring_ascii");
    if (ascii_encoder == nullptr) {
      error = "JSON ASCII encoder is unavailable";
      return false;
    }
    fallback.bound[2] = *ascii_encoder;
    fallback.bound[3] = Value::none();
    fallback.bound[4] = Value::string(": ");
    fallback.bound[5] = Value::string(", ");
    fallback.bound[6] = Value::boolean(false);
    fallback.bound[7] = Value::boolean(false);
    fallback.bound[8] = Value::boolean(true);
    return json_encoder_python_fallback(runtime, args, fallback, out, error);
  }
  auto& state = *static_cast<JsonEncoderState*>(data);
  if (state.native_fast_path) {
    std::string encoded;
    JsonActiveStack active;
    bool supported = true;
    if (append_json_builtin(args[0], encoded, active, supported)) {
      out = Value::list({Value::string(std::move(encoded))});
      return true;
    }
  }
  return json_encoder_python_fallback(runtime, args, state, out, error);
}

bool json_floatstr(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void* data) {
  if (argc != 1) {
    error = "JSON float encoder expected one float argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value numeric = args[0];
  if (numeric.tag != ValueTag::Double && value_as_instance(numeric) != nullptr) {
    Value stored;
    std::string ignored;
    if (object_get_attr(numeric, "_value_", stored, ignored) && stored.tag == ValueTag::Double) {
      numeric = std::move(stored);
    }
  }
  if (numeric.tag != ValueTag::Double) {
    error = "JSON float encoder expected one float argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const double number = numeric.as.f64;
  const char* special = nullptr;
  if (std::isnan(number)) special = "NaN";
  else if (std::isinf(number)) special = number < 0 ? "-Infinity" : "Infinity";
  if (special == nullptr) {
    if (const Value* repr = runtime.find_builtin("repr")) {
      return runtime_call_callable(runtime, *repr, &numeric, 1, out, error);
    }
    out = Value::string(value_to_repr(args[0]));
    return true;
  }
  auto* state = static_cast<JsonFloatState*>(data);
  if (!value_truthy(state->allow_nan)) {
    error = "Out of range float values are not JSON compliant: " + value_to_repr(args[0]);
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  out = Value::string(special);
  return true;
}

bool bind_make_encoder_args(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc,
    Value (&bound)[9], std::string& error) {
  static constexpr const char* names[] = {
      "markers", "default", "encoder", "indent", "key_separator",
      "item_separator", "sort_keys", "skipkeys", "allow_nan"};
  if (argc > 9) {
    error = "make_encoder() takes at most 9 arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  bool assigned[9]{};
  for (uint32_t index = 0; index < argc; ++index) {
    bound[index] = args[index];
    assigned[index] = true;
  }
  for (uint32_t kwindex = 0; kwindex < kwargc; ++kwindex) {
    size_t slot = 9;
    for (size_t index = 0; index < 9; ++index) {
      if (kwargs[kwindex].name != nullptr &&
          std::string_view(kwargs[kwindex].name) == names[index]) {
        slot = index;
        break;
      }
    }
    if (slot == 9) {
      error = "make_encoder() got an unexpected keyword argument";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (assigned[slot]) {
      error = std::string("make_encoder() got multiple values for argument '") + names[slot] + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (kwargs[kwindex].value != nullptr) bound[slot] = *kwargs[kwindex].value;
    assigned[slot] = true;
  }
  for (size_t index = 0; index < 9; ++index) {
    if (!assigned[index]) {
      error = std::string("make_encoder() missing required argument '") + names[index] + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return true;
}

bool json_make_encoder_impl(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out,
    std::string& error) {
  Value bound[9];
  if (!bind_make_encoder_args(runtime, args, argc, kwargs, kwargc, bound, error)) {
    return false;
  }
  if (bound[0].tag != ValueTag::None && value_as_dict(bound[0]) == nullptr) {
    error = "make_encoder() argument 1 must be dict or None, not " +
        std::string(value_binary_type_name(bound[0]));
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  for (const size_t index : {size_t{6}, size_t{7}, size_t{8}}) {
    bool ignored = false;
    if (!runtime_truthy(runtime, bound[index], ignored, error)) return false;
  }
  const auto string_equals = [](const Value& value, std::string_view expected) {
    auto* string = value_as_string(value);
    return string && string_object_view(*string) == expected;
  };
  const auto* encoder_native = value_as_native_function(bound[2]);
  const bool standard_ascii_encoder =
      encoder_native != nullptr &&
      encoder_native->callback == json_encode_basestring_ascii;
  // CPython passes a fresh empty marker dict for normal circular checking;
  // our active recursion set provides the same detection without per-item VM
  // dictionary traffic.
  const bool standard_markers = bound[0].tag == ValueTag::None ||
      (value_as_dict(bound[0]) != nullptr && value_as_dict(bound[0])->entries.empty());
  const bool native_fast_path = standard_markers &&
      bound[3].tag == ValueTag::None && string_equals(bound[4], ": ") &&
      string_equals(bound[5], ", ") && !value_truthy(bound[6]) && !value_truthy(bound[7]) &&
      value_truthy(bound[8]) &&
      standard_ascii_encoder;
  if (native_fast_path) {
    // _json.make_encoder is called once for each default dumps() invocation.
    // Reuse a stateless native callable so shallow inputs avoid allocating an
    // Encoder object and heap state for every call.
    if (const Value* cached = runtime.find_native_symbol("_json._default_encoder")) {
      out = *cached;
      return true;
    }
  }
  // Avoid allocating the Python recursive encoder closure per json.dumps call
  // when the native path owns the whole default built-in data graph. Keep the
  // bound arguments so custom and unsupported values can initialize it lazily.
  auto* state = new JsonEncoderState{};
  state->native_fast_path = native_fast_path;
  for (size_t index = 0; index < 9; ++index) state->bound[index] = bound[index];
  out = runtime.make_native_function(
      "_json.Encoder.__call__", json_encoder_call, state,
      json_encoder_state_cleanup, nullptr, false, nullptr, false);
  return true;
}

bool json_make_encoder(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  return json_make_encoder_impl(runtime, args, argc, nullptr, 0, out, error);
}

bool json_make_encoder_kw(
    Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out,
    std::string& error, void*) {
  return json_make_encoder_impl(runtime, args, argc, kwargs, kwargc, out, error);
}

} // namespace

void register_json_module(Runtime& runtime) {
  NativeModuleBuilder builder(runtime, "_json");
  builder.value("__doc__", Value::string("JSON accelerator runtime primitives."))
      .function("encode_basestring", json_encode_basestring)
      .function(
          "encode_basestring_ascii",
          json_encode_basestring_ascii,
          builtin_fast_adapter<json_encode_basestring_ascii, 1>,
          true)
      .function("scanstring", json_scanstring, builtin_fast_adapter<json_scanstring, 4>, false, json_scanstring_kw)
      .function("make_scanner", json_make_scanner)
      .function(
          "make_encoder", json_make_encoder,
          builtin_fast_adapter<json_make_encoder, 9>, false,
          json_make_encoder_kw);
  builder.value(
      "_default_encoder",
      runtime.make_native_function(
          "_json._default_encoder", json_encoder_call, nullptr, nullptr,
          builtin_fast_adapter<json_encoder_call, 2>, false, nullptr, false));
  runtime.register_module("_json", builder.finish());
}

} // namespace xlang3
