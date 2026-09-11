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
#include "xlang3/functional_iterators.h"
#include "xlang3/mapping.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/value.h"

#include <cmath>
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

bool json_make_scanner(
    Runtime& runtime, const Value* args, uint32_t argc, Value& out,
    std::string& error, void*) {
  if (argc == 1) {
    Value strict;
    if (!object_get_attr(args[0], "strict", strict, error)) {
      runtime.raise_class_error("AttributeError", error);
      return false;
    }
    bool ignored = false;
    if (!runtime_truthy(runtime, strict, ignored, error)) return false;
  }
  return call_python_json_helper(
      runtime, "json.scanner", "py_make_scanner", args, argc,
      nullptr, 0, out, error);
}

struct JsonFloatState {
  Value allow_nan;
};

struct JsonEncoderState {
  Value iterencode;
};

void json_float_state_cleanup(void* data) {
  delete static_cast<JsonFloatState*>(data);
}

void json_encoder_state_cleanup(void* data) {
  delete static_cast<JsonEncoderState*>(data);
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
  auto* state = static_cast<JsonEncoderState*>(data);
  Value call_args[] = {args[0], args[1].as.i64 < 0 ? Value::int64(0) : args[1]};
  Value generated;
  if (!runtime_call_callable(runtime, state->iterencode, call_args, 2, generated, error)) {
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
  auto* float_state = new JsonFloatState{bound[8]};
  Value floatstr = runtime.make_native_function(
      "_json._floatstr", json_floatstr, float_state, json_float_state_cleanup,
      nullptr, false, nullptr, false);
  Value helper_args[] = {
      bound[0], bound[1], bound[2], bound[3], floatstr,
      bound[4], bound[5], bound[6], bound[7], Value::boolean(true)};
  Value iterencode;
  if (!call_python_json_helper(
      runtime, "json.encoder", "_make_iterencode", helper_args, 10,
      nullptr, 0, iterencode, error)) {
    return false;
  }
  auto* state = new JsonEncoderState{std::move(iterencode)};
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
      .function("encode_basestring_ascii", json_encode_basestring_ascii)
      .function("scanstring", json_scanstring, nullptr, false, json_scanstring_kw)
      .function("make_scanner", json_make_scanner)
      .function("make_encoder", json_make_encoder, nullptr, false, json_make_encoder_kw);
  runtime.register_module("_json", builder.finish());
}

} // namespace xlang3
