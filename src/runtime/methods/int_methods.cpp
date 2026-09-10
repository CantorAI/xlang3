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
#include "xlang3/builtin_methods.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/object_model.h"
#include "xlang3/runtime.h"
#include "xlang3/sequence.h"

#include <array>
#include <limits>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

bool int_bit_length_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "int.bit_length expected no arguments";
    return false;
  }
  int64_t bits = 0;
  if (!value_int_like_bit_length(args[0], bits)) {
    error = "int.bit_length target must be int";
    return false;
  }
  value_set_int64(out, bits);
  return true;
}

bool int_index_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "int.__index__ expected no arguments";
    return false;
  }
  if (args[0].tag != ValueTag::Int64 && value_as_bigint(args[0]) == nullptr) {
    error = "int.__index__ target must be int";
    return false;
  }
  value_assign_fast(out, args[0]);
  return true;
}

bool int_compare_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    Value& out,
    std::string& error,
    const char* op) {
  if (argc != 2) {
    error = "int comparison expected 1 argument";
    return false;
  }
  if (value_compare(op, args[0], args[1], out, error)) {
    return true;
  }
  error.clear();
  if (const Value* not_implemented = runtime.find_builtin("NotImplemented")) {
    value_assign_fast(out, *not_implemented);
    return true;
  }
  error = "NotImplemented is unavailable";
  return false;
}

#define XLANG3_INT_COMPARE_METHOD(name, op) \
  bool name(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) { \
    return int_compare_method(runtime, args, argc, out, error, op); \
  }

XLANG3_INT_COMPARE_METHOD(int_lt_method, "<")
XLANG3_INT_COMPARE_METHOD(int_le_method, "<=")
XLANG3_INT_COMPARE_METHOD(int_gt_method, ">")
XLANG3_INT_COMPARE_METHOD(int_ge_method, ">=")

#undef XLANG3_INT_COMPARE_METHOD

bool int_add_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "int.__add__ expected 1 argument";
    return false;
  }
  return value_add(args[0], args[1], out, error);
}

bool int_pow_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 && argc != 3) {
    error = "int.__pow__ expected 1 or 2 arguments";
    return false;
  }
  Value base = args[0];
  if (value_as_instance(base) != nullptr) {
    Value stored;
    std::string ignored;
    if (object_get_attr(base, "__xlang3_int_value__", stored, ignored)) {
      base = std::move(stored);
    }
  }
  if (argc == 2 || args[2].tag == ValueTag::None) {
    return value_pow(base, args[1], out, error);
  }
  const Value* pow_function = runtime.find_builtin("pow");
  if (pow_function == nullptr) {
    error = "pow is unavailable";
    return false;
  }
  Value pow_args[] = {base, args[1], args[2]};
  return runtime_call_callable(runtime, *pow_function, pow_args, 3, out, error);
}

bool int_to_bytes_method(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 3) {
    error = "int.to_bytes expected at most length and byteorder";
    return false;
  }
  const Value length_value = argc >= 2 ? args[1] : Value::int64(1);
  const Value order_value = argc >= 3 ? args[2] : Value::string("big");
  if (length_value.tag != ValueTag::Int64) {
    error = "int.to_bytes value and length must be int";
    return false;
  }
  auto* order = value_as_string(order_value);
  if (order == nullptr) {
    error = "int.to_bytes byteorder must be str";
    return false;
  }
  const std::string byteorder = string_object_to_string(*order);
  if (byteorder != "little" && byteorder != "big") {
    error = "byteorder must be either 'little' or 'big'";
    return false;
  }
  if (length_value.as.i64 < 0 ||
      static_cast<uint64_t>(length_value.as.i64) > (std::numeric_limits<uint32_t>::max)()) {
    error = "int.to_bytes length is out of supported range";
    return false;
  }
  const uint32_t length = static_cast<uint32_t>(length_value.as.i64);
  std::string bytes;
  if (!value_int_like_to_bytes(args[0], length, byteorder == "big", false, bytes, error)) {
    return false;
  }
  out = Value::bytes(bytes);
  return true;
}

bool byteorder_is_big(const Value& value, bool& is_big, std::string& error) {
  auto* order = value_as_string(value);
  if (order == nullptr) {
    error = "byteorder must be str";
    return false;
  }
  const std::string_view byteorder = string_object_view(*order);
  if (byteorder == "big") {
    is_big = true;
    return true;
  }
  if (byteorder == "little") {
    is_big = false;
    return true;
  }
  error = "byteorder must be either 'little' or 'big'";
  return false;
}

bool bool_arg(const Value& value, bool& out) {
  if (value.tag == ValueTag::Bool) {
    out = value.as.b;
    return true;
  }
  if (value.tag == ValueTag::Int64) {
    out = value.as.i64 != 0;
    return true;
  }
  return false;
}

bool collect_from_bytes_input(const Value& value, std::vector<uint8_t>& bytes, std::string& error) {
  if (auto* object = value_as_bytes(value)) {
    const auto view = bytes_object_view(*object);
    bytes.assign(view.begin(), view.end());
    return true;
  }
  if (auto* object = value_as_bytearray(value)) {
    bytes.assign(object->value.begin(), object->value.end());
    return true;
  }
  if (auto* view = value_as_memoryview(value)) {
    if (view->released) {
      error = "operation forbidden on released memoryview object";
      return false;
    }
    const auto data = memoryview_object_view(*view);
    if (!data.data()) {
      error = "memoryview owner is not byte-addressable";
      return false;
    }
    bytes.assign(data.begin(), data.end());
    return true;
  }

  Value iterator;
  if (!sequence_get_iter(value, iterator, error)) {
    return false;
  }
  for (;;) {
    bool done = false;
    Value item;
    if (!sequence_iter_next(iterator, done, item, error)) {
      return false;
    }
    if (done) {
      return true;
    }
    if (item.tag != ValueTag::Int64 || item.as.i64 < 0 || item.as.i64 > 255) {
      error = "bytes must be in range(0, 256)";
      return false;
    }
    bytes.push_back(static_cast<uint8_t>(item.as.i64));
  }
}

bool int_from_bytes_impl(
    Runtime&,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error) {
  if (argc < 1 || argc > 2) {
    error = "int.from_bytes expected bytes and optional byteorder";
    return false;
  }

  bool is_big = true;
  bool signed_value = false;
  if (argc >= 2 && !byteorder_is_big(args[1], is_big, error)) {
    return false;
  }
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view name(kwargs[i].name == nullptr ? "" : kwargs[i].name);
    if (name == "byteorder") {
      if (argc >= 2) {
        error = "int.from_bytes got multiple values for argument 'byteorder'";
        return false;
      }
      if (!byteorder_is_big(*kwargs[i].value, is_big, error)) {
        return false;
      }
    } else if (name == "signed") {
      if (!bool_arg(*kwargs[i].value, signed_value)) {
        error = "signed must be bool";
        return false;
      }
    } else {
      error = "int.from_bytes got an unexpected keyword argument '" + std::string(name) + "'";
      return false;
    }
  }

  std::vector<uint8_t> bytes;
  if (!collect_from_bytes_input(args[0], bytes, error)) {
    return false;
  }
  return value_bigint_from_bytes(bytes.data(), bytes.size(), is_big, signed_value, out, error);
}

bool int_from_bytes_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || value_as_class(args[0]) == nullptr) {
    error = "int.from_bytes expected a type and bytes";
    return false;
  }
  Value parsed;
  if (!int_from_bytes_impl(runtime, args + 1, argc - 1, nullptr, 0, parsed, error)) return false;
  const Value* int_class = runtime.find_builtin("int");
  if (int_class != nullptr && value_is(args[0], *int_class)) {
    value_assign_fast(out, parsed);
    return true;
  }
  out = Value::instance(args[0]);
  return object_set_attr(out, "__xlang3_int_value__", parsed, error);
}

bool int_from_bytes_kw_method(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void*) {
  if (argc < 2 || value_as_class(args[0]) == nullptr) {
    error = "int.from_bytes expected a type and bytes";
    return false;
  }
  Value parsed;
  if (!int_from_bytes_impl(runtime, args + 1, argc - 1, kwargs, kwargc, parsed, error)) return false;
  const Value* int_class = runtime.find_builtin("int");
  if (int_class != nullptr && value_is(args[0], *int_class)) {
    value_assign_fast(out, parsed);
    return true;
  }
  out = Value::instance(args[0]);
  return object_set_attr(out, "__xlang3_int_value__", parsed, error);
}

static constexpr BuiltinMethodSpec kIntMethods[] = {
    {"__index__", "int.__index__", int_index_method},
    {"__add__", "int.__add__", int_add_method},
    {"__pow__", "int.__pow__", int_pow_method, nullptr, false, nullptr, "($self, value, mod=None, /)"},
    {"__lt__", "int.__lt__", int_lt_method},
    {"__le__", "int.__le__", int_le_method},
    {"__gt__", "int.__gt__", int_gt_method},
    {"__ge__", "int.__ge__", int_ge_method},
    {"bit_length", "int.bit_length", int_bit_length_method},
    {"to_bytes", "int.to_bytes", int_to_bytes_method},
};

} // namespace

bool int_get_method(const Value& object, const std::string& name, Value& out) {
  if (object.tag != ValueTag::Int64 && value_as_bigint(object) == nullptr) {
    return false;
  }
  if (name == "real" || name == "numerator") {
    value_assign_fast(out, object);
    return true;
  }
  if (name == "imag") {
    out = Value::int64(0);
    return true;
  }
  if (name == "denominator") {
    out = Value::int64(1);
    return true;
  }
  return bind_builtin_method_from_table(object, name, kIntMethods, std::size(kIntMethods), out);
}

bool int_install_class_methods(Runtime& runtime, ClassObject& int_class) {
  Value owner;
  owner.tag = ValueTag::Object;
  owner.as.obj = &int_class.header;
  retain(owner);
  for (const auto& method : kIntMethods) {
    Value function = runtime.make_native_function(
        method.full_name,
        method.callback,
        nullptr,
        nullptr,
        method.fast_callback,
        method.fast_releases_vm_lock,
        method.keyword_callback);
    builtin_method_set_text_signature(function, method.text_signature);
    std::string ignored;
    object_set_attr(function, "__objclass__", owner, ignored);
    int_class.attrs[method.name] = std::move(function);
  }
  int_class.attrs["from_bytes"] = Value::class_method(runtime.make_native_function(
      "int.from_bytes",
      int_from_bytes_method,
      nullptr,
      nullptr,
      nullptr,
      false,
      int_from_bytes_kw_method));
  for (const char* name : {"real", "imag", "numerator", "denominator"}) {
    Value descriptor = slot_descriptor("int", name, UINT32_MAX);
    slot_descriptor_set_owner_class(descriptor, owner);
    int_class.attrs[name] = std::move(descriptor);
  }
  ++int_class.version;
  return true;
}

} // namespace xlang3
