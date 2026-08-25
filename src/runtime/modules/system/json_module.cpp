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
#include "xlang3/sequence.h"

#include <algorithm>
#include <json.hpp>
#include <string>

namespace xlang3 {

namespace {

using Json = nlohmann::ordered_json;

constexpr const char* kJsonDecoderNativeType = "json.JSONDecoder";

struct JsonDecoderState {
  Value object_hook;
  Value object_pairs_hook;
  Value parse_float;
  Value parse_int;
};

void json_decoder_cleanup(void* data) {
  delete static_cast<JsonDecoderState*>(data);
}

struct JsonDumpOptions {
  int indent = -1;
  bool sort_keys = false;
  bool ensure_ascii = true;
  bool skipkeys = false;
  std::string item_separator = ", ";
  std::string key_separator = ": ";
  Value default_callable;
};

Value kw_value(const NativeKeywordArg* kwargs, uint32_t kwargc, const char* name, Value fallback = Value::none()) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name != nullptr && std::string(kwargs[i].name) == name && kwargs[i].value != nullptr) {
      return *kwargs[i].value;
    }
  }
  return fallback;
}

bool is_truthy_value(const Value& value) {
  if (value.tag == ValueTag::Bool) {
    return value.as.b;
  }
  if (value.tag == ValueTag::None || value.tag == ValueTag::Invalid) {
    return false;
  }
  if (value.tag == ValueTag::Int64) {
    return value.as.i64 != 0;
  }
  if (value.tag == ValueTag::Double) {
    return value.as.f64 != 0.0;
  }
  return true;
}

bool value_to_json_key(const Value& value, std::string& out) {
  if (auto* text = value_as_string(value)) {
    out = string_object_to_string(*text);
    return true;
  }
  if (value.tag == ValueTag::Int64 || value.tag == ValueTag::Double || value.tag == ValueTag::Bool ||
      value.tag == ValueTag::None) {
    out = value_to_string(value);
    return true;
  }
  return false;
}

void append_json_escaped_string(std::string& out, std::string_view text, bool ensure_ascii) {
  out.push_back('"');
  for (unsigned char ch : text) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (ch < 0x20 || (ensure_ascii && ch >= 0x80)) {
          static constexpr char hex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(hex[(ch >> 4u) & 0xfu]);
          out.push_back(hex[ch & 0xfu]);
        } else {
          out.push_back(static_cast<char>(ch));
        }
        break;
    }
  }
  out.push_back('"');
}

void append_indent(std::string& out, int level, int indent) {
  if (indent < 0) {
    return;
  }
  out.append(static_cast<size_t>(level * indent), ' ');
}

bool dump_value(Runtime& runtime, const Value& value, const JsonDumpOptions& options, int depth, std::string& out, std::string& error);

bool dump_sequence(
    Runtime& runtime,
    const std::vector<Value>& items,
    const JsonDumpOptions& options,
    int depth,
    std::string& out,
    std::string& error) {
  out.push_back('[');
  const bool pretty = options.indent >= 0 && !items.empty();
  for (size_t i = 0; i < items.size(); ++i) {
    if (i != 0) {
      out += options.item_separator;
    }
    if (pretty) {
      out.push_back('\n');
      append_indent(out, depth + 1, options.indent);
    }
    if (!dump_value(runtime, items[i], options, depth + 1, out, error)) {
      return false;
    }
  }
  if (pretty) {
    out.push_back('\n');
    append_indent(out, depth, options.indent);
  }
  out.push_back(']');
  return true;
}

bool dump_dict(
    Runtime& runtime,
    DictObject& dict,
    const JsonDumpOptions& options,
    int depth,
    std::string& out,
    std::string& error) {
  std::vector<std::pair<std::string, Value>> entries;
  entries.reserve(dict.entries.size());
  for (const auto& entry : dict.entries) {
    std::string key;
    if (!value_to_json_key(entry.first, key)) {
      if (options.skipkeys) {
        continue;
      }
      error = "keys must be str, int, float, bool or None";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    entries.push_back({std::move(key), entry.second});
  }
  if (options.sort_keys) {
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
      return left.first < right.first;
    });
  }
  out.push_back('{');
  const bool pretty = options.indent >= 0 && !entries.empty();
  for (size_t i = 0; i < entries.size(); ++i) {
    if (i != 0) {
      out += options.item_separator;
    }
    if (pretty) {
      out.push_back('\n');
      append_indent(out, depth + 1, options.indent);
    }
    append_json_escaped_string(out, entries[i].first, options.ensure_ascii);
    out += options.key_separator;
    if (!dump_value(runtime, entries[i].second, options, depth + 1, out, error)) {
      return false;
    }
  }
  if (pretty) {
    out.push_back('\n');
    append_indent(out, depth, options.indent);
  }
  out.push_back('}');
  return true;
}

bool dump_value(
    Runtime& runtime,
    const Value& value,
    const JsonDumpOptions& options,
    int depth,
    std::string& out,
    std::string& error) {
  switch (value.tag) {
    case ValueTag::None:
      out += "null";
      return true;
    case ValueTag::Bool:
      out += value.as.b ? "true" : "false";
      return true;
    case ValueTag::Int64:
      out += std::to_string(value.as.i64);
      return true;
    case ValueTag::Double:
      out += std::to_string(value.as.f64);
      return true;
    case ValueTag::Invalid:
      out += "null";
      return true;
    case ValueTag::Object:
      break;
  }

  if (auto* text = value_as_string(value)) {
    append_json_escaped_string(out, string_object_view(*text), options.ensure_ascii);
    return true;
  }
  if (auto* list = value_as_list(value)) {
    return dump_sequence(runtime, list->items, options, depth, out, error);
  }
  if (auto* tuple = value_as_tuple(value)) {
    return dump_sequence(runtime, tuple->items, options, depth, out, error);
  }
  if (auto* dict = value_as_dict(value)) {
    return dump_dict(runtime, *dict, options, depth, out, error);
  }
  if (auto* instance = value_as_instance(value)) {
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      return dump_value(runtime, instance->mapping_storage, options, depth, out, error);
    }
  }
  if (options.default_callable.tag != ValueTag::None) {
    Value converted;
    if (!runtime_call_callable(runtime, options.default_callable, &value, 1, converted, error)) {
      return false;
    }
    return dump_value(runtime, converted, options, depth, out, error);
  }
  error = "object is not JSON serializable";
  runtime.raise_class_error("TypeError", error);
  return false;
}

Value json_to_value(
    Runtime& runtime,
    const Json& json,
    const Value* object_hook,
    const Value* object_pairs_hook,
    const Value* parse_float,
    const Value* parse_int,
    std::string& error) {
  if (json.is_null()) {
    return Value::none();
  }
  if (json.is_boolean()) {
    return Value::boolean(json.get<bool>());
  }
  if (json.is_number_integer()) {
    if (parse_int != nullptr && parse_int->tag != ValueTag::None) {
      Value text = Value::string(json.dump());
      Value parsed;
      if (!runtime_call_callable(runtime, *parse_int, &text, 1, parsed, error)) {
        return Value::invalid();
      }
      return parsed;
    }
    return Value::int64(json.get<int64_t>());
  }
  if (json.is_number_unsigned()) {
    if (parse_int != nullptr && parse_int->tag != ValueTag::None) {
      Value text = Value::string(json.dump());
      Value parsed;
      if (!runtime_call_callable(runtime, *parse_int, &text, 1, parsed, error)) {
        return Value::invalid();
      }
      return parsed;
    }
    return Value::int64(static_cast<int64_t>(json.get<uint64_t>()));
  }
  if (json.is_number_float()) {
    if (parse_float != nullptr && parse_float->tag != ValueTag::None) {
      Value text = Value::string(json.dump());
      Value parsed;
      if (!runtime_call_callable(runtime, *parse_float, &text, 1, parsed, error)) {
        return Value::invalid();
      }
      return parsed;
    }
    return Value::number(json.get<double>());
  }
  if (json.is_string()) {
    return Value::string(json.get<std::string>());
  }
  if (json.is_array()) {
    std::vector<Value> items;
    items.reserve(json.size());
    for (const auto& item : json) {
      items.push_back(json_to_value(runtime, item, object_hook, object_pairs_hook, parse_float, parse_int, error));
      if (!error.empty()) {
        return Value::invalid();
      }
    }
    return Value::list(std::move(items));
  }
  if (json.is_object()) {
    std::vector<std::pair<Value, Value>> entries;
    entries.reserve(json.size());
    for (auto it = json.begin(); it != json.end(); ++it) {
      entries.push_back(
          {Value::string(it.key()), json_to_value(runtime, it.value(), object_hook, object_pairs_hook, parse_float, parse_int, error)});
      if (!error.empty()) {
        return Value::invalid();
      }
    }
    if (object_pairs_hook != nullptr && object_pairs_hook->tag != ValueTag::None) {
      std::vector<Value> pairs;
      pairs.reserve(entries.size());
      for (const auto& entry : entries) {
        pairs.push_back(Value::tuple({entry.first, entry.second}));
      }
      Value pair_list = Value::list(std::move(pairs));
      Value hooked;
      if (!runtime_call_callable(runtime, *object_pairs_hook, &pair_list, 1, hooked, error)) {
        return Value::invalid();
      }
      return hooked;
    }
    Value object = Value::dict(std::move(entries));
    if (object_hook != nullptr && object_hook->tag != ValueTag::None) {
      Value hooked;
      if (!runtime_call_callable(runtime, *object_hook, &object, 1, hooked, error)) {
        return Value::invalid();
      }
      return hooked;
    }
    return object;
  }
  return Value::none();
}

bool json_arg_to_text(const Value& value, std::string& out) {
  if (auto* text = value_as_string(value)) {
    out = string_object_to_string(*text);
    return true;
  }
  return false;
}

bool json_dump_options_from_kwargs(
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    JsonDumpOptions& options,
    std::string& error) {
  options.default_callable = Value::none();
  const Value indent = kw_value(kwargs, kwargc, "indent");
  if (indent.tag == ValueTag::Int64) {
    options.indent = static_cast<int>(indent.as.i64);
    options.item_separator = ",";
    options.key_separator = ": ";
  }
  const Value sort_keys = kw_value(kwargs, kwargc, "sort_keys");
  if (sort_keys.tag != ValueTag::None) {
    options.sort_keys = is_truthy_value(sort_keys);
  }
  const Value ensure_ascii = kw_value(kwargs, kwargc, "ensure_ascii");
  if (ensure_ascii.tag != ValueTag::None) {
    options.ensure_ascii = is_truthy_value(ensure_ascii);
  }
  const Value skipkeys = kw_value(kwargs, kwargc, "skipkeys");
  if (skipkeys.tag != ValueTag::None) {
    options.skipkeys = is_truthy_value(skipkeys);
  }
  const Value default_fn = kw_value(kwargs, kwargc, "default");
  if (default_fn.tag != ValueTag::None) {
    value_assign_fast(options.default_callable, default_fn);
  }
  const Value separators = kw_value(kwargs, kwargc, "separators");
  if (separators.tag != ValueTag::None) {
    auto* tuple = value_as_tuple(separators);
    auto* list = value_as_list(separators);
    if (tuple != nullptr && tuple->items.size() == 2) {
      if (!json_arg_to_text(tuple->items[0], options.item_separator) ||
          !json_arg_to_text(tuple->items[1], options.key_separator)) {
        error = "json.dumps() separators must contain strings";
        return false;
      }
    } else if (list != nullptr && list->items.size() == 2) {
      if (!json_arg_to_text(list->items[0], options.item_separator) ||
          !json_arg_to_text(list->items[1], options.key_separator)) {
        error = "json.dumps() separators must contain strings";
        return false;
      }
    } else {
      error = "json.dumps() separators must be a 2-item sequence";
      return false;
    }
  }
  return true;
}

bool json_dumps_common(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (argc < 1) {
    error = "json.dumps() expected object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  try {
    JsonDumpOptions options;
    if (!json_dump_options_from_kwargs(kwargs, kwargc, options, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    std::string text;
    if (!dump_value(runtime, args[0], options, 0, text, error)) {
      return false;
    }
    out = Value::string(std::move(text));
    return true;
  } catch (const std::exception& ex) {
    error = std::string("json.dumps() failed: ") + ex.what();
    runtime.raise_class_error("TypeError", error);
    return false;
  }
}

bool json_dumps(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return json_dumps_common(runtime, args, argc, nullptr, 0, out, error);
}

bool json_dumps_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return json_dumps_common(runtime, args, argc, kwargs, kwargc, out, error);
}

bool json_loads_common(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    const Value* decoder_object_hook,
    Value& out,
    std::string& error) {
  if (argc != 1) {
    error = "json.loads() expected string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string text;
  if (!json_arg_to_text(args[0], text)) {
    error = "json.loads() argument must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value object_hook = Value::none();
  Value object_pairs_hook = Value::none();
  Value parse_float = Value::none();
  Value parse_int = Value::none();
  if (decoder_object_hook != nullptr) {
    value_assign_fast(object_hook, *decoder_object_hook);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (std::string(kwargs[i].name) == "object_hook") {
      value_assign_fast(object_hook, *kwargs[i].value);
    } else if (std::string(kwargs[i].name) == "object_pairs_hook") {
      value_assign_fast(object_pairs_hook, *kwargs[i].value);
    } else if (std::string(kwargs[i].name) == "parse_float") {
      value_assign_fast(parse_float, *kwargs[i].value);
    } else if (std::string(kwargs[i].name) == "parse_int") {
      value_assign_fast(parse_int, *kwargs[i].value);
    }
  }
  try {
    out = json_to_value(runtime, Json::parse(text), &object_hook, &object_pairs_hook, &parse_float, &parse_int, error);
    if (!error.empty()) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    error = std::string("json.loads() parse error: ") + ex.what();
    runtime.raise_class_error("ValueError", error);
    return false;
  }
}

bool json_loads(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return json_loads_common(runtime, args, argc, nullptr, 0, nullptr, out, error);
}

bool json_loads_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  return json_loads_common(runtime, args, argc, kwargs, kwargc, nullptr, out, error);
}

bool json_load(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "json.load() expected file";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value read;
  if (!object_get_attr(args[0], "read", read, error)) {
    return false;
  }
  Value text;
  if (!runtime_call_callable(runtime, read, nullptr, 0, text, error)) {
    return false;
  }
  return json_loads(runtime, &text, 1, out, error, nullptr);
}

bool json_dump(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2) {
    error = "json.dump() expected object and file";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value text;
  if (!json_dumps(runtime, args, 1, text, error, nullptr)) {
    return false;
  }
  Value write;
  if (!object_get_attr(args[1], "write", write, error)) {
    return false;
  }
  Value ignored;
  if (!runtime_call_callable(runtime, write, &text, 1, ignored, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool json_dump_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2) {
    error = "json.dump() expected object and file";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value text;
  if (!json_dumps_common(runtime, args, 1, kwargs, kwargc, text, error)) {
    return false;
  }
  Value write;
  if (!object_get_attr(args[1], "write", write, error)) {
    return false;
  }
  Value ignored;
  if (!runtime_call_callable(runtime, write, &text, 1, ignored, error)) {
    return false;
  }
  value_set_none(out);
  return true;
}

bool json_encoder_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "JSONEncoder.__init__ expected self";
    return false;
  }
  value_set_none(out);
  return true;
}

bool json_encoder_init_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg*,
    uint32_t,
    Value& out,
    std::string& error,
    void* user_data) {
  return json_encoder_init(runtime, args, argc, out, error, user_data);
}

bool json_decoder_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1) {
    error = "JSONDecoder.__init__ expected self";
    return false;
  }
  auto* state = new JsonDecoderState();
  state->object_hook = Value::none();
  state->object_pairs_hook = Value::none();
  state->parse_float = Value::none();
  state->parse_int = Value::none();
  if (!instance_set_native_data(args[0], kJsonDecoderNativeType, state, json_decoder_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool json_decoder_init_kw(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 1) {
    error = "JSONDecoder.__init__ expected self";
    return false;
  }
  auto* state = new JsonDecoderState();
  state->object_hook = Value::none();
  state->object_pairs_hook = Value::none();
  state->parse_float = Value::none();
  state->parse_int = Value::none();
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (std::string(kwargs[i].name) == "object_hook") {
      value_assign_fast(state->object_hook, *kwargs[i].value);
    } else if (std::string(kwargs[i].name) == "object_pairs_hook") {
      value_assign_fast(state->object_pairs_hook, *kwargs[i].value);
    } else if (std::string(kwargs[i].name) == "parse_float") {
      value_assign_fast(state->parse_float, *kwargs[i].value);
    } else if (std::string(kwargs[i].name) == "parse_int") {
      value_assign_fast(state->parse_int, *kwargs[i].value);
    }
  }
  if (!instance_set_native_data(args[0], kJsonDecoderNativeType, state, json_decoder_cleanup, error)) {
    delete state;
    return false;
  }
  value_set_none(out);
  return true;
}

bool json_encoder_encode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "JSONEncoder.encode() expected object";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  return json_dumps(runtime, args + 1, 1, out, error, nullptr);
}

bool json_encoder_iterencode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (!json_encoder_encode(runtime, args, argc, out, error, nullptr)) {
    return false;
  }
  out = Value::list({out});
  return true;
}

bool json_decoder_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "JSONDecoder.decode() expected string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value object_hook = Value::none();
  Value object_pairs_hook = Value::none();
  Value parse_float = Value::none();
  Value parse_int = Value::none();
  if (auto* state = static_cast<JsonDecoderState*>(instance_get_native_data(args[0], kJsonDecoderNativeType))) {
    value_assign_fast(object_hook, state->object_hook);
    value_assign_fast(object_pairs_hook, state->object_pairs_hook);
    value_assign_fast(parse_float, state->parse_float);
    value_assign_fast(parse_int, state->parse_int);
  }
  NativeKeywordArg hook_kwargs[] = {
      {"object_pairs_hook", &object_pairs_hook},
      {"parse_float", &parse_float},
      {"parse_int", &parse_int},
  };
  return json_loads_common(runtime, args + 1, 1, hook_kwargs, 3, &object_hook, out, error);
}

Value make_json_encoder_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function(
                                     "json.JSONEncoder.__init__",
                                     json_encoder_init,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     false,
                                     json_encoder_init_kw)});
  attrs.push_back({"encode", runtime.make_native_function("json.JSONEncoder.encode", json_encoder_encode)});
  attrs.push_back({"iterencode", runtime.make_native_function("json.JSONEncoder.iterencode", json_encoder_iterencode)});
  return Value::class_object("JSONEncoder", std::move(attrs));
}

Value make_json_decoder_class(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__init__", runtime.make_native_function(
                                     "json.JSONDecoder.__init__",
                                     json_decoder_init,
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     false,
                                     json_decoder_init_kw)});
  attrs.push_back({"decode", runtime.make_native_function("json.JSONDecoder.decode", json_decoder_decode)});
  return Value::class_object("JSONDecoder", std::move(attrs));
}

} // namespace

void register_json_module(Runtime& runtime) {
  Value value_error = runtime.find_builtin("ValueError") != nullptr ? *runtime.find_builtin("ValueError") : Value::invalid();
  Value json_decode_error = Value::class_object("JSONDecodeError", {}, value_error);
  NativeModuleBuilder builder(runtime, "json");
  builder.function("loads", json_loads, nullptr, false, json_loads_kw)
      .function("load", json_load)
      .function("dump", json_dump, nullptr, false, json_dump_kw)
      .function("dumps", json_dumps, nullptr, false, json_dumps_kw)
      .value("JSONDecodeError", json_decode_error)
      .value("JSONEncoder", make_json_encoder_class(runtime))
      .value("JSONDecoder", make_json_decoder_class(runtime));
  runtime.register_module("json", builder.finish());
}

} // namespace xlang3
