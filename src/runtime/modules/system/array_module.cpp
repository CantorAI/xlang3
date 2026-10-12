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
#include <memory>
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
  // The bytearray is the single authoritative buffer, including exported
  // views. Copying it back into a second string on every scalar read made
  // SciMark's constant-size accesses proportional to the entire array.
  Value storage;
  Value result_class;
  ArrayState(char code, size_t size, std::string bytes = {}, Value klass = Value::none())
      : typecode(code), itemsize(size), storage(Value::bytearray(std::move(bytes))),
        result_class(std::move(klass)) {}
  std::string& bytes() { return value_as_bytearray(storage)->value; }
  const std::string& bytes() const { return value_as_bytearray(storage)->value; }
};

size_t array_itemsize(char typecode) {
  switch (typecode) {
    case 'b': case 'B': return 1;
    case 'h': case 'H': return 2;
    case 'u': return sizeof(wchar_t);
    case 'i': case 'I': case 'f': case 'w': return 4;
    case 'l': case 'L': return sizeof(long);
    case 'q': case 'Q': case 'd': return 8;
    default: return 0;
  }
}

ArrayState* array_state(const Value& self, std::string& error) {
  auto* state = static_cast<ArrayState*>(instance_get_native_data(self, kArrayNativeType));
  if (state == nullptr) error = "invalid array.array object";
  return state;
}

bool array_get_attr(const Value& self, const std::string& name, Value& out, std::string& error) {
  auto* state = array_state(self, error);
  if (state == nullptr) return false;
  if (name == "__xlang3_bytes_value__") { value_assign_fast(out, state->storage); return true; }
  if (name == "__xlang3_array_format__") { out = Value::string(std::string(1, state->typecode)); return true; }
  if (name == "__xlang3_array_itemsize__") { value_set_int64(out, static_cast<int64_t>(state->itemsize)); return true; }
  return false;
}

bool array_metadata_property(Runtime&, const Value* args, uint32_t argc, Value& out,
                              std::string& error, void* user_data) {
  if (argc != 1) { error = "array metadata descriptor expected an array"; return false; }
  return array_get_attr(args[0], static_cast<const char*>(user_data), out, error);
}

bool publish_array_bytes(Value self, const ArrayState& state, std::string& error) {
  // Publish stable metadata once. Scalar writes and appends mutate storage in
  // place; replacing the payload would detach existing exports from the array.
  const Value references[] = {state.storage, state.result_class};
  return object_set_attr(self, "__xlang3_bytes_value__", state.storage, error) &&
      instance_set_native_gc_references(self, references, 2, [](void* data) {
        auto* state = static_cast<ArrayState*>(data);
        value_set_none(state->storage);
        value_set_none(state->result_class);
      }, error) &&
      instance_set_native_attr_hooks(self, array_get_attr, nullptr, nullptr, error);
}

bool array_check_resize(Runtime& runtime, const ArrayState& state, size_t size, std::string& error) {
  // Every derived memoryview owns an export on this same bytearray. Reject
  // resizing before mutation, but permit scalar/equal-length writes in place.
  if (size != state.bytes().size() && value_as_bytearray(state.storage)->buffer_exports != 0) {
    error = "cannot resize an array that is exporting buffers";
    runtime.raise_class_error("BufferError", error);
    return false;
  }
  return true;
}

bool array_as_index(Runtime& runtime, const Value& value, int64_t& out,
                    std::string& error);

std::string array_utf8(uint32_t codepoint) {
  std::string text;
  if (codepoint < 0x80u) text.push_back(static_cast<char>(codepoint));
  else if (codepoint < 0x800u) {
    text.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
    text.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else if (codepoint < 0x10000u) {
    text.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
    text.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
    text.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else {
    text.push_back(static_cast<char>(0xf0u | (codepoint >> 18)));
    text.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu)));
    text.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
    text.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  }
  return text;
}

bool append_array_value(Runtime& runtime, char typecode, size_t itemsize,
                        std::string& bytes, const Value& item, std::string& error) {
  if (typecode == 'w') {
    const auto* string = value_as_string(item);
    const auto text = string == nullptr ? std::string_view{} : string_object_view(*string);
    if (string == nullptr || utf8_codepoint_count(text) != 1) {
      error = "array item must be a unicode character, not " +
          std::string(value_binary_type_name(item));
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    const auto lead = static_cast<unsigned char>(text[0]);
    uint32_t codepoint = lead;
    if (text.size() == 2) codepoint = lead & 0x1fu;
    else if (text.size() == 3) codepoint = lead & 0x0fu;
    else if (text.size() == 4) codepoint = lead & 0x07u;
    for (size_t i = 1; i < text.size(); ++i) {
      codepoint = (codepoint << 6) |
          (static_cast<unsigned char>(text[i]) & 0x3fu);
    }
    bytes.append(reinterpret_cast<const char*>(&codepoint), sizeof(codepoint));
    return true;
  }
  if (typecode == 'f' || typecode == 'd') {
    double number = item.tag == ValueTag::Double ? item.as.f64 :
        item.tag == ValueTag::Int64 ? static_cast<double>(item.as.i64) : 0.0;
    if (item.tag != ValueTag::Double && item.tag != ValueTag::Int64) {
      error = "must be real number";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (typecode == 'f') {
      const float value = static_cast<float>(number);
      bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    } else {
      bytes.append(reinterpret_cast<const char*>(&number), sizeof(number));
    }
    return true;
  }
  int64_t integer = 0;
  if (!array_as_index(runtime, item, integer, error)) {
    error = "an integer is required";
    return false;
  }
  const bool signed_type = typecode == 'b' || typecode == 'h' ||
      typecode == 'i' || typecode == 'l' || typecode == 'q';
  const uint32_t bits = static_cast<uint32_t>(itemsize * 8);
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
  bytes.append(reinterpret_cast<const char*>(&value), itemsize);
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

bool array_init(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*);

Value array_result_class(const Value& self) {
  auto* instance = value_as_instance(self);
  if (instance == nullptr) return Value::none();
  Value result = instance->klass;
  auto examine = [&](const Value& candidate) {
    if (auto* klass = value_as_class(candidate)) {
      auto found = klass->attrs.find("__init__");
      if (found != klass->attrs.end()) {
        const auto* function = value_as_native_function(found->second);
        if (function != nullptr && function->callback == array_init) result = candidate;
      }
    }
  };
  if (auto* klass = value_as_class(instance->klass); klass != nullptr && !klass->mro_cache.empty()) {
    for (const auto& candidate : klass->mro_cache) examine(candidate);
  } else {
    for (Value current = instance->klass; value_as_class(current) != nullptr;
         current = value_as_class(current)->base) examine(current);
  }
  return result;
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
  std::unique_ptr<ArrayState> fresh(new ArrayState(typecode, itemsize, {}, array_result_class(args[0])));
  auto* state = fresh.get();
  if (argc == 3) {
    if (auto* bytes = value_as_bytes(args[2])) {
      state->bytes().assign(bytes_object_view(*bytes));
      if (state->bytes().size() % itemsize != 0) {
        error = "bytes length not a multiple of item size";
        runtime.raise_class_error("ValueError", error);
        return false;
      }
    } else if (auto* bytearray = value_as_bytearray(args[2])) {
      state->bytes() = bytearray->value;
      if (state->bytes().size() % itemsize != 0) {
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
        if (!append_array_value(runtime, state->typecode, state->itemsize, state->bytes(), item, error)) {
          return false;
        }
      }
    }
  }
  if (auto* previous = static_cast<ArrayState*>(instance_get_native_data(args[0], kArrayNativeType));
      previous != nullptr && value_as_bytearray(previous->storage)->buffer_exports != 0) {
    error = "cannot resize an array that is exporting buffers";
    runtime.raise_class_error("BufferError", error);
    return false;
  }
  if (!instance_set_native_data(args[0], kArrayNativeType, state,
      [](void* data) { delete static_cast<ArrayState*>(data); }, error)) return false;
  fresh.release();
  if (!publish_array_bytes(args[0], *state, error)) return false;
  value_set_none(out);
  return true;
}

bool array_len(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "array.__len__ expected no arguments"; return false; }
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  value_set_int64(out, static_cast<int64_t>(state->bytes().size() / state->itemsize));
  return true;
}

bool array_tobytes(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "array.tobytes expected no arguments"; return false; }
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  out = Value::bytes(state->bytes());
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
  const auto data = bytes_object_view(*bytes);
  if (data.size() % state->itemsize != 0) {
    error = "bytes length not a multiple of item size";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  if (!array_check_resize(runtime, *state, state->bytes().size() + data.size(), error)) return false;
  state->bytes().append(data);
  value_set_none(out);
  return true;
}

bool array_append(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) { error = "array.append expected one argument"; return false; }
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  const char typecode = state->typecode;
  const size_t itemsize = state->itemsize;
  std::string encoded;
  if (!append_array_value(runtime, typecode, itemsize, encoded, args[1], error)) return false;
  state = array_state(args[0], error); // Conversion may execute Python.
  if (state == nullptr) return false;
  if (state->typecode != typecode) { error = "array type changed during conversion"; return false; }
  if (!array_check_resize(runtime, *state, state->bytes().size() + encoded.size(), error)) return false;
  state->bytes().append(encoded);
  value_set_none(out);
  return true;
}

bool array_read_item(const ArrayState& state, size_t index, Value& out) {
  const char* data = state.bytes().data() + index * state.itemsize;
  if (state.typecode == 'f') { float value; std::memcpy(&value, data, 4); value_set_number(out, value); return true; }
  if (state.typecode == 'd') { double value; std::memcpy(&value, data, 8); value_set_number(out, value); return true; }
  if (state.typecode == 'w') {
    uint32_t codepoint;
    std::memcpy(&codepoint, data, sizeof(codepoint));
    out = Value::string(array_utf8(codepoint));
    return true;
  }
  uint64_t value = 0;
  std::memcpy(&value, data, state.itemsize);
  const bool signed_type = state.typecode == 'b' || state.typecode == 'h' ||
      state.typecode == 'i' || state.typecode == 'l' || state.typecode == 'q';
  if (signed_type && state.itemsize < sizeof(value)) {
    const uint64_t sign_bit = uint64_t{1} << (state.itemsize * 8 - 1);
    if ((value & sign_bit) != 0) {
      value |= (~uint64_t{0}) << (state.itemsize * 8);
    }
  }
  if (!signed_type && state.itemsize == sizeof(value) &&
      value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
    out = value_bigint_from_u64(value);
  } else {
    value_set_int64(out, static_cast<int64_t>(value));
  }
  return true;
}

struct ArraySlice {
  int64_t start = 0;
  int64_t stop = 0;
  int64_t step = 1;
  bool start_none = false;
  bool stop_none = false;
  size_t count = 0;
};

bool array_slice_index(Runtime& runtime, const Value& value, int64_t& out, std::string& error) {
  if (value_int_like_to_i64(value, out)) return true;
  Value converted = value;
  if (value_as_bigint(converted) == nullptr) {
    Value method;
    if (!object_get_attr(value, "__index__", method, error)) {
      error = "slice indices must be integers or None or have an __index__ method";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    if (!runtime_call_callable(runtime, method, nullptr, 0, converted, error)) return false;
    if (value_int_like_to_i64(converted, out)) return true;
  }
  if (value_bigint_to_i64(converted, out)) return true;
  bool negative = false;
  const uint32_t* limbs = nullptr;
  uint32_t count = 0;
  if (value_bigint_limb_view(converted, negative, limbs, count)) {
    out = negative ? std::numeric_limits<int64_t>::min() : std::numeric_limits<int64_t>::max();
    return true;
  }
  error = "__index__ returned non-int";
  runtime.raise_class_error("TypeError", error);
  return false;
}

bool array_unpack_slice(Runtime& runtime, const SliceObject& slice, ArraySlice& plan, std::string& error) {
  // Run index coercion before obtaining the current array length: Python
  // __index__ callbacks can mutate the array or create buffer exports.
  if (slice.step.tag != ValueTag::None && !array_slice_index(runtime, slice.step, plan.step, error)) return false;
  if (plan.step == 0) {
    error = "slice step cannot be zero"; runtime.raise_class_error("ValueError", error); return false;
  }
  plan.start_none = slice.start.tag == ValueTag::None;
  plan.stop_none = slice.stop.tag == ValueTag::None;
  return (plan.start_none || array_slice_index(runtime, slice.start, plan.start, error)) &&
      (plan.stop_none || array_slice_index(runtime, slice.stop, plan.stop, error));
}

void array_adjust_slice(ArraySlice& plan, int64_t length) {
  auto adjust = [&](int64_t index, bool is_none, bool is_start) {
    if (is_none) return plan.step < 0 ? (is_start ? length - 1 : int64_t{-1})
                                    : (is_start ? int64_t{0} : length);
    if (index < 0) index += length;
    const int64_t low = plan.step < 0 ? -1 : 0;
    const int64_t high = plan.step < 0 ? length - 1 : length;
    return std::max(low, std::min(high, index));
  };
  plan.start = adjust(plan.start, plan.start_none, true);
  plan.stop = adjust(plan.stop, plan.stop_none, false);
  const uint64_t stride = plan.step < 0 ? uint64_t(-(plan.step + 1)) + 1 : uint64_t(plan.step);
  if (plan.step > 0 && plan.start < plan.stop)
    plan.count = 1 + static_cast<size_t>(uint64_t(plan.stop - 1 - plan.start) / stride);
  else if (plan.step < 0 && plan.start > plan.stop)
    plan.count = 1 + static_cast<size_t>(uint64_t(plan.start - 1 - plan.stop) / stride);
}

bool array_new_result(const ArrayState& state, std::string bytes, Value& out, std::string& error) {
  const char typecode = state.typecode;
  const size_t itemsize = state.itemsize;
  Value klass = state.result_class;
  out = Value::instance(klass);
  std::unique_ptr<ArrayState> fresh(new ArrayState(typecode, itemsize, std::move(bytes), std::move(klass)));
  auto* result = fresh.get();
  if (!instance_set_native_data(out, kArrayNativeType, result,
      [](void* data) { delete static_cast<ArrayState*>(data); }, error)) return false;
  fresh.release();
  return publish_array_bytes(out, *result, error);
}

bool array_getslice(Runtime& runtime, const Value& self, const SliceObject& slice, Value& out, std::string& error) {
  ArraySlice plan;
  if (!array_unpack_slice(runtime, slice, plan, error)) return false;
  auto* state = array_state(self, error);
  if (state == nullptr) return false;
  array_adjust_slice(plan, static_cast<int64_t>(state->bytes().size() / state->itemsize));
  std::string selected;
  selected.reserve(plan.count * state->itemsize);
  if (plan.step == 1 && plan.count != 0) {
    selected.assign(state->bytes().data() + static_cast<size_t>(plan.start) * state->itemsize,
                    plan.count * state->itemsize);
  } else {
    int64_t index = plan.start;
    for (size_t remaining = plan.count; remaining != 0; --remaining) {
      selected.append(state->bytes().data() + static_cast<size_t>(index) * state->itemsize, state->itemsize);
      if (remaining > 1) index += plan.step; // Do not overflow after the last item.
    }
  }
  return array_new_result(*state, std::move(selected), out, error);
}

bool array_setslice(Runtime& runtime, const Value& self, const SliceObject& slice,
                    const Value& replacement, std::string& error) {
  ArraySlice plan;
  if (!array_unpack_slice(runtime, slice, plan, error)) return false;
  auto* state = array_state(self, error);
  auto* source = static_cast<ArrayState*>(instance_get_native_data(replacement, kArrayNativeType));
  if (state == nullptr) return false;
  if (source == nullptr || source->typecode != state->typecode) {
    error = "can only assign array of same kind to array slice";
    runtime.raise_class_error("TypeError", error); return false;
  }
  array_adjust_slice(plan, static_cast<int64_t>(state->bytes().size() / state->itemsize));
  const size_t source_count = source->bytes().size() / state->itemsize;
  if (plan.step != 1 && source_count != plan.count) {
    error = "attempt to assign array of size " + std::to_string(source_count) +
        " to extended slice of size " + std::to_string(plan.count);
    runtime.raise_class_error("ValueError", error); return false;
  }
  if (plan.count == 0 && source_count == 0 && value_as_bytearray(state->storage)->buffer_exports != 0) {
    error = "cannot resize an array that is exporting buffers";
    runtime.raise_class_error("BufferError", error); return false;
  }
  const size_t new_size = state->bytes().size() - plan.count * state->itemsize + source->bytes().size();
  if (!array_check_resize(runtime, *state, new_size, error)) return false;
  // Snapshot only self-aliases. Other arrays already own independent buffers.
  std::string snapshot;
  std::string_view input(source->bytes());
  if (source->storage.as.obj == state->storage.as.obj) {
    snapshot = source->bytes();
    input = snapshot;
  }
  if (plan.step == 1) {
    const size_t offset = static_cast<size_t>(plan.start) * state->itemsize;
    if (source_count == plan.count) {
      if (!input.empty()) std::memcpy(state->bytes().data() + offset, input.data(), input.size());
    } else {
      state->bytes().replace(offset, plan.count * state->itemsize, input.data(), input.size());
    }
  } else {
    int64_t index = plan.start;
    size_t source_offset = 0;
    for (size_t remaining = plan.count; remaining != 0; --remaining) {
      std::memcpy(state->bytes().data() + static_cast<size_t>(index) * state->itemsize,
                  input.data() + source_offset, state->itemsize);
      source_offset += state->itemsize;
      if (remaining > 1) index += plan.step;
    }
  }
  return true;
}

bool array_tolist(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) { error = "array.tolist expected no arguments"; return false; }
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  const size_t count = state->bytes().size() / state->itemsize;
  std::vector<Value> items;
  items.reserve(count);
  for (size_t index = 0; index < count; ++index) {
    Value item;
    if (!array_read_item(*state, index, item)) return false;
    items.push_back(std::move(item));
  }
  out = Value::list(std::move(items));
  return true;
}

bool array_getitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "array indices must be integers"; runtime.raise_class_error("TypeError", error); return false;
  }
  if (auto* slice = value_as_slice(args[1])) {
    return array_getslice(runtime, args[0], *slice, out, error);
  }
  int64_t index = 0;
  if (!array_slice_index(runtime, args[1], index, error)) return false;
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  const int64_t length = static_cast<int64_t>(state->bytes().size() / state->itemsize);
  if (index < 0) index += length;
  if (index < 0 || index >= length) {
    error = "array index out of range"; runtime.raise_class_error("IndexError", error); return false;
  }
  return array_read_item(*state, static_cast<size_t>(index), out);
}

bool array_setitem(Runtime& runtime, const Value* args, uint32_t argc, Value& out,
                   std::string& error, void*) {
  if (argc != 3) {
    error = "array assignment requires an index and a value";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  if (auto* slice = value_as_slice(args[1])) {
    if (!array_setslice(runtime, args[0], *slice, args[2], error)) return false;
    value_set_none(out);
    return true;
  }
  int64_t index = 0;
  if (!array_slice_index(runtime, args[1], index, error)) return false;
  auto* state = array_state(args[0], error);
  if (state == nullptr) return false;
  const int64_t length = static_cast<int64_t>(state->bytes().size() / state->itemsize);
  if (index < 0) index += length;
  if (index < 0 || index >= length) {
    error = "array assignment index out of range";
    runtime.raise_class_error("IndexError", error);
    return false;
  }
  const char typecode = state->typecode;
  const size_t itemsize = state->itemsize;
  std::string encoded;
  if (!append_array_value(runtime, typecode, itemsize, encoded, args[2], error)) return false;
  state = array_state(args[0], error);
  if (state == nullptr) return false;
  if (state->typecode != typecode) { error = "array type changed during conversion"; return false; }
  if (index >= static_cast<int64_t>(state->bytes().size() / itemsize)) {
    error = "array assignment index out of range"; runtime.raise_class_error("IndexError", error); return false;
  }
  std::memcpy(state->bytes().data() + static_cast<size_t>(index) * itemsize, encoded.data(), itemsize);
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
  std::string repeated;
  if (!repeat_array_bytes(runtime, state->bytes(), count, repeated, error)) {
    return false;
  }
  if (in_place) {
    if (!array_check_resize(runtime, *state, repeated.size(), error)) return false;
    if (repeated.size() != state->bytes().size()) state->bytes() = std::move(repeated);
    value_assign_fast(out, args[array_index]);
    return true;
  }
  auto* instance = value_as_instance(args[array_index]);
  if (instance == nullptr) return false;
  out = Value::instance(state->result_class);
  auto* result_state = new (std::nothrow)
      ArrayState{state->typecode, state->itemsize, std::move(repeated), state->result_class};
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
  attrs.emplace_back("typecode", Value::property(
      runtime.make_native_function("array.array.typecode", array_metadata_property,
          const_cast<char*>("__xlang3_array_format__")), Value::none(), Value::none(), Value::none()));
  attrs.emplace_back("itemsize", Value::property(
      runtime.make_native_function("array.array.itemsize", array_metadata_property,
          const_cast<char*>("__xlang3_array_itemsize__")), Value::none(), Value::none(), Value::none()));
  attrs.emplace_back("__len__", runtime.make_native_function("array.array.__len__", array_len));
  attrs.emplace_back("__getitem__", runtime.make_native_function("array.array.__getitem__", array_getitem));
  attrs.emplace_back("__setitem__", runtime.make_native_function("array.array.__setitem__", array_setitem));
  attrs.emplace_back("__mul__", runtime.make_native_function("array.array.__mul__", array_mul));
  attrs.emplace_back("__rmul__", runtime.make_native_function("array.array.__rmul__", array_mul));
  attrs.emplace_back("__imul__", runtime.make_native_function("array.array.__imul__", array_imul));
  attrs.emplace_back("append", runtime.make_native_function("array.array.append", array_append));
  attrs.emplace_back("frombytes", runtime.make_native_function("array.array.frombytes", array_frombytes));
  attrs.emplace_back("tolist", runtime.make_native_function("array.array.tolist", array_tolist));
  attrs.emplace_back("tobytes", runtime.make_native_function("array.array.tobytes", array_tobytes));
  Value array_class = Value::class_object("array", std::move(attrs));
  NativeModuleBuilder builder(runtime, "array");
  builder.value("array", array_class)
      .value("ArrayType", array_class)
      .value("typecodes", Value::string("bBuhHiIlLqQfdw"));
  runtime.register_module("array", builder.finish());
}

} // namespace xlang3
