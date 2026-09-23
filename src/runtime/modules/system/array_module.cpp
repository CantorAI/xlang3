/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
*/
#include "xlang3/builtins.h"

#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"
#include "xlang3/sequence.h"

#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace xlang3 {
namespace {

constexpr const char* kArrayNativeType = "array.array";

struct ArrayState {
  char typecode = 'b';
  size_t itemsize = 1;
  std::string bytes;
};

size_t array_itemsize(char typecode) {
  switch (typecode) {
    case 'b': case 'B': return 1;
    case 'h': case 'H': case 'u': return 2;
    case 'i': case 'I': case 'l': case 'L': case 'f': case 'w': return 4;
    case 'q': case 'Q': case 'd': return 8;
    default: return 0;
  }
}

ArrayState* array_state(const Value& self, std::string& error) {
  auto* state = static_cast<ArrayState*>(instance_get_native_data(self, kArrayNativeType));
  if (state == nullptr) error = "invalid array.array object";
  return state;
}

bool publish_array_bytes(Value self, const ArrayState& state, std::string& error) {
  return object_set_attr(self, "__xlang3_bytes_value__", Value::bytearray(state.bytes), error) &&
      object_set_attr(self, "typecode", Value::string(std::string(1, state.typecode)), error) &&
      object_set_attr(self, "itemsize", Value::int64(static_cast<int64_t>(state.itemsize)), error);
}

void sync_array_bytes(const Value& self, ArrayState& state) {
  Value payload;
  std::string ignored;
  if (object_get_attr(self, "__xlang3_bytes_value__", payload, ignored)) {
    if (auto* bytes = value_as_bytearray(payload)) state.bytes = bytes->value;
  }
}

bool array_as_index(Runtime& runtime, const Value& value, int64_t& out,
                    std::string& error);

bool append_array_value(Runtime& runtime, ArrayState& state, const Value& item,
                        std::string& error) {
  if (state.typecode == 'f' || state.typecode == 'd') {
    double number = item.tag == ValueTag::Double ? item.as.f64 :
        item.tag == ValueTag::Int64 ? static_cast<double>(item.as.i64) : 0.0;
    if (item.tag != ValueTag::Double && item.tag != ValueTag::Int64) {
      error = "must be real number";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (state.typecode == 'f') {
      const float value = static_cast<float>(number);
      state.bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    } else {
      state.bytes.append(reinterpret_cast<const char*>(&number), sizeof(number));
    }
    return true;
  }
  int64_t integer = 0;
  if (!array_as_index(runtime, item, integer, error)) {
    error = "an integer is required";
    return false;
  }
  const bool signed_type = state.typecode == 'b' || state.typecode == 'h' ||
      state.typecode == 'i' || state.typecode == 'l' || state.typecode == 'q';
  const uint32_t bits = static_cast<uint32_t>(state.itemsize * 8);
  bool in_range = true;
  if (signed_type && bits < 64) {
    const int64_t minimum = -(int64_t{1} << (bits - 1));
    const int64_t maximum = (int64_t{1} << (bits - 1)) - 1;
    in_range = integer >= minimum && integer <= maximum;
  } else if (!signed_type) {
    in_range = integer >= 0 &&
        (bits == 64 || static_cast<uint64_t>(integer) < (uint64_t{1} << bits));
  }
  if (!in_range) {
    error = signed_type ? "signed integer is out of range"
                        : "unsigned integer is out of range";
    runtime.raise_class_error("OverflowError", error);
    return false;
  }
  const uint64_t value = static_cast<uint64_t>(integer);
  state.bytes.append(reinterpret_cast<const char*>(&value), state.itemsize);
  return true;
}

bool array_as_index(Runtime& runtime, const Value& value, int64_t& out,
                    std::string& error) {
  if (value_int_like_to_i64(value, out) || value_bigint_to_i64(value, out)) {
    return true;
  }
  Value method;
  std::string lookup_error;
  if (!object_get_attr(value, "__index__", method, lookup_error)) {
    error = "object cannot be interpreted as an integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  Value converted;
  if (!runtime_call_callable(runtime, method, nullptr, 0, converted, error)) {
    return false;
  }
  if (value_int_like_to_i64(converted, out) ||
      value_bigint_to_i64(converted, out)) {
    return true;
  }
  error = "__index__ returned non-int";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool repeat_array_bytes(Runtime& runtime, const std::string& source,
                        int64_t count, std::string& repeated,
                        std::string& error) {
  if (count <= 0 || source.empty()) {
    repeated.clear();
    return true;
  }
  const auto repeats = static_cast<uint64_t>(count);
  if (repeats > static_cast<uint64_t>(std::numeric_limits<size_t>::max() /
                                      source.size())) {
    error = "repeated array is too long";
    runtime.raise_class_error("MemoryError", error);
    return false;
  }
  const size_t target_size = source.size() * static_cast<size_t>(repeats);
  try {
    repeated.clear();
    repeated.reserve(target_size);
    std::string block = source;
    uint64_t remaining = repeats;
    while (remaining != 0) {
      if ((remaining & 1u) != 0) repeated.append(block);
      remaining >>= 1u;
      if (remaining != 0) block.append(block);
    }
  } catch (const std::bad_alloc&) {
    error = "repeated array is too long";
    runtime.raise_class_error("MemoryError", error);
    return false;
  } catch (const std::length_error&) {
    error = "repeated array is too long";
    runtime.raise_class_error("MemoryError", error);
    return false;
  }
  return true;
}

bool array_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 2 || argc > 3) {
    error = "array() takes at least 1 argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* typecode_value = value_as_string(args[1]);
  if (typecode_value == nullptr || string_object_view(*typecode_value).size() != 1) {
    error = "array() argument 1 must be a unicode character";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const char typecode = string_object_view(*typecode_value)[0];
  const size_t itemsize = array_itemsize(typecode);
  if (itemsize == 0) {
    error = "bad typecode (must be b, B, u, w, h, H, i, I, l, L, q, Q, f or d)";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  auto* state = new ArrayState{typecode, itemsize, {}};
  if (!instance_set_native_data(args[0], kArrayNativeType, state,
                                [](void* data) { delete static_cast<ArrayState*>(data); }, error)) {
    delete state;
    return false;
  }
  if (argc == 3) {
    if (auto* bytes = value_as_bytes(args[2])) {
      state->bytes.assign(bytes_object_view(*bytes));
      if (state->bytes.size() % itemsize != 0) {
        error = "bytes length not a multiple of item size";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
    } else if (auto* bytearray = value_as_bytearray(args[2])) {
      state->bytes = bytearray->value;
      if (state->bytes.size() % itemsize != 0) {
        error = "bytes length not a multiple of item size";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
    } else {
      std::vector<Value> items;
      if (!runtime_collect_iterable(runtime, args[2], items, error)) {
        return false;
      }
      for (const auto& item : items) {
        if (!append_array_value(runtime, *state, item, error)) {
          return false;
        }
      }
    }
  }
  if (!publish_array_bytes(args[0], *state, error)) return false;
  value_set_none(out);
  return true;
}

bool array_len(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "array.__len__ expected no arguments"; return false; }
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  sync_array_bytes(args[0], *state);
  value_set_int64(out, static_cast<int64_t>(state->bytes.size() / state->itemsize));
  return true;
}

bool array_tobytes(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "array.tobytes expected no arguments"; return false; }
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  sync_array_bytes(args[0], *state);
  out = Value::bytes(state->bytes);
  return true;
}

bool array_frombytes(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) { error = "array.frombytes expected one argument"; return false; }
  auto* state = array_state(args[0], error);
  auto* bytes = value_as_bytes(args[1]);
  if (state == nullptr || bytes == nullptr) {
    if (state != nullptr) { error = "a bytes-like object is required"; runtime.raise_class_error("TypeError", error); }
    return false;
  }
  sync_array_bytes(args[0], *state);
  const auto data = bytes_object_view(*bytes);
  if (data.size() % state->itemsize != 0) {
    error = "bytes length not a multiple of item size";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  state->bytes.append(data);
  if (!publish_array_bytes(args[0], *state, error)) return false;
  value_set_none(out);
  return true;
}

bool array_append(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) { error = "array.append expected one argument"; return false; }
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  sync_array_bytes(args[0], *state);
  if (!append_array_value(runtime, *state, args[1], error)) return false;
  if (!publish_array_bytes(args[0], *state, error)) return false;
  value_set_none(out);
  return true;
}

bool array_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "array indices must be integers"; runtime.raise_class_error("TypeError", error); return false;
  }
  int64_t index = 0;
  if (!array_as_index(runtime, args[1], index, error)) return false;
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  sync_array_bytes(args[0], *state);
  const int64_t length = static_cast<int64_t>(state->bytes.size() / state->itemsize);
  if (index < 0) index += length;
  if (index < 0 || index >= length) {
    error = "array index out of range"; runtime.raise_class_error("IndexError", error); return false;
  }
  const char* data = state->bytes.data() + static_cast<size_t>(index) * state->itemsize;
  if (state->typecode == 'f') { float value; std::memcpy(&value, data, 4); value_set_number(out, value); return true; }
  if (state->typecode == 'd') { double value; std::memcpy(&value, data, 8); value_set_number(out, value); return true; }
  uint64_t value = 0;
  std::memcpy(&value, data, state->itemsize);
  const bool signed_type = state->typecode == 'b' || state->typecode == 'h' ||
      state->typecode == 'i' || state->typecode == 'l' || state->typecode == 'q';
  if (signed_type && state->itemsize < sizeof(value)) {
    const uint64_t sign_bit = uint64_t{1} << (state->itemsize * 8 - 1);
    if ((value & sign_bit) != 0) {
      value |= (~uint64_t{0}) << (state->itemsize * 8);
    }
  }
  if (!signed_type && state->itemsize == sizeof(value) &&
      value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    out = value_bigint_from_u64(value);
  } else {
    value_set_int64(out, static_cast<int64_t>(value));
  }
  return true;
}

bool array_setitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                   std::string& error, void*) {
  if (argc != 3) {
    error = "array assignment requires an index and a value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  int64_t index = 0;
  if (!array_as_index(runtime, args[1], index, error)) return false;
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  sync_array_bytes(args[0], *state);
  const int64_t length = static_cast<int64_t>(state->bytes.size() / state->itemsize);
  if (index < 0) index += length;
  if (index < 0 || index >= length) {
    error = "array assignment index out of range";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  ArrayState encoded{state->typecode, state->itemsize, {}};
  if (!append_array_value(runtime, encoded, args[2], error)) return false;
  std::memcpy(state->bytes.data() + static_cast<size_t>(index) * state->itemsize,
              encoded.bytes.data(), state->itemsize);
  if (!publish_array_bytes(args[0], *state, error)) return false;
  value_set_none(out);
  return true;
}

bool array_repeat_impl(Runtime& runtime, const Value* args, uint32_t argc,
                       Value& out, std::string& error, bool in_place) {
  if (argc != 2) {
    error = "array repetition requires one integer";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  uint32_t array_index = 0;
  uint32_t count_index = 1;
  auto* state = static_cast<ArrayState*>(
      instance_get_native_data(args[array_index], kArrayNativeType));
  if (state == nullptr && !in_place) {
    array_index = 1;
    count_index = 0;
    state = static_cast<ArrayState*>(
        instance_get_native_data(args[array_index], kArrayNativeType));
  }
  if (state == nullptr) {
    error = "invalid array.array object";
    return false;
  }
  int64_t count = 0;
  if (!array_as_index(runtime, args[count_index], count, error)) return false;
  sync_array_bytes(args[array_index], *state);
  std::string repeated;
  if (!repeat_array_bytes(runtime, state->bytes, count, repeated, error)) {
    return false;
  }
  if (in_place) {
    state->bytes = std::move(repeated);
    if (!publish_array_bytes(args[array_index], *state, error)) return false;
    value_assign_fast(out, args[array_index]);
    return true;
  }
  auto* instance = value_as_instance(args[array_index]);
  if (instance == nullptr) return false;
  out = Value::instance(instance->klass);
  auto* result_state = new (std::nothrow)
      ArrayState{state->typecode, state->itemsize, std::move(repeated)};
  if (result_state == nullptr) {
    error = "repeated array is too long";
    runtime.raise_class_error("MemoryError", error);
    return false;
  }
  if (!instance_set_native_data(
          out, kArrayNativeType, result_state,
          [](void* data) { delete static_cast<ArrayState*>(data); }, error)) {
    delete result_state;
    return false;
  }
  return publish_array_bytes(out, *result_state, error);
}

bool array_mul(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
               std::string& error, void*) {
  return array_repeat_impl(runtime, args, argc, out, error, false);
}

bool array_imul(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                std::string& error, void*) {
  return array_repeat_impl(runtime, args, argc, out, error, true);
}

} // namespace

void register_array_module(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.emplace_back("__module__", Value::string("array"));
  attrs.emplace_back("__qualname__", Value::string("array"));
  attrs.emplace_back("__init__", runtime.make_native_function("array.array.__init__", array_init));
  attrs.emplace_back("__len__", runtime.make_native_function("array.array.__len__", array_len));
  attrs.emplace_back("__getitem__", runtime.make_native_function("array.array.__getitem__", array_getitem));
  attrs.emplace_back("__setitem__", runtime.make_native_function("array.array.__setitem__", array_setitem));
  attrs.emplace_back("__mul__", runtime.make_native_function("array.array.__mul__", array_mul));
  attrs.emplace_back("__rmul__", runtime.make_native_function("array.array.__rmul__", array_mul));
  attrs.emplace_back("__imul__", runtime.make_native_function("array.array.__imul__", array_imul));
  attrs.emplace_back("append", runtime.make_native_function("array.array.append", array_append));
  attrs.emplace_back("frombytes", runtime.make_native_function("array.array.frombytes", array_frombytes));
  attrs.emplace_back("tobytes", runtime.make_native_function("array.array.tobytes", array_tobytes));
  Value array_class = Value::class_object("array", std::move(attrs));
  NativeModuleBuilder builder(runtime, "array");
  builder.value("array", array_class)
      .value("ArrayType", array_class)
      .value("typecodes", Value::string("bBuhHiIlLqQfdw"));
  runtime.register_module("array", builder.finish());
}

} // namespace xlang3
