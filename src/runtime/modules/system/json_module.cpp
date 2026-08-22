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

#include <fstream>
#include <json.hpp>
#include <sstream>
#include <string>

namespace xlang3 {

namespace {

using Json = nlohmann::json;

constexpr const char* kJsonDecoderNativeType = "json.JSONDecoder";

struct JsonDecoderState {
  Value object_hook;
};

void json_decoder_cleanup(void* data) {
  delete static_cast<JsonDecoderState*>(data);
}

Json value_to_json(const Value& value) {
  switch (value.tag) {
    case ValueTag::None:
      return nullptr;
    case ValueTag::Bool:
      return value.as.b;
    case ValueTag::Int64:
      return value.as.i64;
    case ValueTag::Double:
      return value.as.f64;
    case ValueTag::Invalid:
      return nullptr;
    case ValueTag::Object:
      break;
  }

  if (auto* text = value_as_string(value)) {
    return string_object_to_string(*text);
  }
  if (auto* list = value_as_list(value)) {
    Json out = Json::array();
    for (const auto& item : list->items) {
      out.push_back(value_to_json(item));
    }
    return out;
  }
  if (value.tag == ValueTag::Object && value.as.obj != nullptr && value.as.obj->kind == ObjectKind::Tuple) {
    auto* tuple = reinterpret_cast<TupleObject*>(value.as.obj);
    Json out = Json::array();
    for (const auto& item : tuple->items) {
      out.push_back(value_to_json(item));
    }
    return out;
  }
  if (auto* dict = value_as_dict(value)) {
    Json out = Json::object();
    for (const auto& entry : dict->entries) {
      std::string key;
      if (auto* key_text = value_as_string(entry.first)) {
        key = string_object_to_string(*key_text);
      } else {
        key = value_to_string(entry.first);
      }
      out[key] = value_to_json(entry.second);
    }
    return out;
  }
  if (auto* instance = value_as_instance(value)) {
    if (value_as_dict(instance->mapping_storage) != nullptr) {
      return value_to_json(instance->mapping_storage);
    }
  }
  return value_to_string(value);
}

Value json_to_value(Runtime& runtime, const Json& json, const Value* object_hook, std::string& error) {
  if (json.is_null()) {
    return Value::none();
  }
  if (json.is_boolean()) {
    return Value::boolean(json.get<bool>());
  }
  if (json.is_number_integer()) {
    return Value::int64(json.get<int64_t>());
  }
  if (json.is_number_unsigned()) {
    return Value::int64(static_cast<int64_t>(json.get<uint64_t>()));
  }
  if (json.is_number_float()) {
    return Value::number(json.get<double>());
  }
  if (json.is_string()) {
    return Value::string(json.get<std::string>());
  }
  if (json.is_array()) {
    std::vector<Value> items;
    items.reserve(json.size());
    for (const auto& item : json) {
      items.push_back(json_to_value(runtime, item, object_hook, error));
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
      entries.push_back({Value::string(it.key()), json_to_value(runtime, it.value(), object_hook, error)});
      if (!error.empty()) {
        return Value::invalid();
      }
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

int json_indent_from_kwargs(const NativeKeywordArg* kwargs, uint32_t kwargc) {
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (std::string(kwargs[i].name) == "indent" && kwargs[i].value->tag == ValueTag::Int64) {
      return static_cast<int>(kwargs[i].value->as.i64);
    }
  }
  return -1;
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
    const int indent = json_indent_from_kwargs(kwargs, kwargc);
    out = Value::string(value_to_json(args[0]).dump(indent));
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
  if (decoder_object_hook != nullptr) {
    value_assign_fast(object_hook, *decoder_object_hook);
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (std::string(kwargs[i].name) == "object_hook") {
      value_assign_fast(object_hook, *kwargs[i].value);
    }
  }
  try {
    out = json_to_value(runtime, Json::parse(text), &object_hook, error);
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
    error = "json.load() expected path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string path;
  if (!json_arg_to_text(args[0], path)) {
    error = "json.load() path must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = "json.load() cannot open " + path;
    runtime.raise_class_error("FileNotFoundError", error);
    return false;
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  Value text = Value::string(buffer.str());
  return json_loads(runtime, &text, 1, out, error, nullptr);
}

bool json_save(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "json.save() expected object and path";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  std::string path;
  if (!json_arg_to_text(args[1], path)) {
    error = "json.save() path must be str";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value text;
  if (!json_dumps(runtime, args, 1, text, error, nullptr)) {
    return false;
  }
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    error = "json.save() cannot open " + path;
    runtime.raise_class_error("OSError", error);
    return false;
  }
  file << value_to_string(text);
  out = Value::boolean(true);
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
  for (uint32_t i = 0; i < kwargc; ++i) {
    if (kwargs[i].name == nullptr || kwargs[i].value == nullptr) {
      continue;
    }
    if (std::string(kwargs[i].name) == "object_hook") {
      value_assign_fast(state->object_hook, *kwargs[i].value);
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

bool json_decoder_decode(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "JSONDecoder.decode() expected string";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value object_hook = Value::none();
  if (auto* state = static_cast<JsonDecoderState*>(instance_get_native_data(args[0], kJsonDecoderNativeType))) {
    value_assign_fast(object_hook, state->object_hook);
  }
  return json_loads_common(runtime, args + 1, 1, nullptr, 0, &object_hook, out, error);
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
  NativeModuleBuilder builder(runtime, "json");
  builder.function("loads", json_loads, nullptr, false, json_loads_kw)
      .function("load", json_load)
      .function("save", json_save)
      .function("dumps", json_dumps, nullptr, false, json_dumps_kw)
      .value("JSONEncoder", make_json_encoder_class(runtime))
      .value("JSONDecoder", make_json_decoder_class(runtime));
  runtime.register_module("json", builder.finish());
}

} // namespace xlang3
