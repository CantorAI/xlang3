/* Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
   Licensed under the Apache License, Version 2.0. */
#include "xlang3/xlang3.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

namespace {

constexpr const char* kBlake2bType = "_blake2.blake2b";
constexpr const char* kBlake2sType = "_blake2.blake2s";

constexpr uint64_t kIvB[8] = {
    0x6A09E667F3BCC908ULL, 0xBB67AE8584CAA73BULL,
    0x3C6EF372FE94F82BULL, 0xA54FF53A5F1D36F1ULL,
    0x510E527FADE682D1ULL, 0x9B05688C2B3E6C1FULL,
    0x1F83D9ABFB41BD6BULL, 0x5BE0CD19137E2179ULL};
constexpr uint32_t kIvS[8] = {
    0x6A09E667U, 0xBB67AE85U, 0x3C6EF372U, 0xA54FF53AU,
    0x510E527FU, 0x9B05688CU, 0x1F83D9ABU, 0x5BE0CD19U};
constexpr uint8_t kSigma[12][16] = {
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3},
    {11,8,12,0,5,2,15,13,10,14,3,6,7,1,9,4},
    {7,9,3,1,13,12,11,14,2,6,5,10,4,0,15,8},
    {9,0,5,7,2,4,10,15,14,1,11,12,6,8,3,13},
    {2,12,6,10,0,11,8,3,4,13,7,5,15,14,1,9},
    {12,5,1,15,14,13,4,10,0,7,6,3,9,2,8,11},
    {13,11,7,14,12,1,3,9,5,0,15,4,8,6,2,10},
    {6,15,14,9,11,3,0,8,12,2,13,7,1,4,10,5},
    {10,2,8,4,7,6,1,5,15,11,9,14,3,12,13,0},
    {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15},
    {14,10,4,8,9,15,13,6,1,12,0,2,11,7,5,3}};

struct PackageState;
struct ConstructorState {
  PackageState* package = nullptr;
  bool blake2b = false;
};

struct PackageState {
  X3PackageHost* host = nullptr;
  X3Value blake2b_class = x3_value_invalid();
  X3Value blake2s_class = x3_value_invalid();
  ConstructorState blake2b_constructor;
  ConstructorState blake2s_constructor;
};

struct Blake2bState {
  uint64_t h[8]{};
  uint64_t t[2]{};
  uint64_t f[2]{};
  uint8_t buffer[128]{};
  size_t buffered = 0;
  uint8_t output_size = 64;
  bool last_node = false;
};

struct Blake2sState {
  uint32_t h[8]{};
  uint32_t t[2]{};
  uint32_t f[2]{};
  uint8_t buffer[64]{};
  size_t buffered = 0;
  uint8_t output_size = 32;
  bool last_node = false;
};

struct BlakeState {
  bool blake2b = false;
  Blake2bState b;
  Blake2sState s;
};

uint64_t load64(const uint8_t* input) {
  uint64_t value = 0;
  for (unsigned int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(input[i]) << (8 * i);
  return value;
}

uint32_t load32(const uint8_t* input) {
  uint32_t value = 0;
  for (unsigned int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(input[i]) << (8 * i);
  return value;
}

void store64(uint8_t* output, uint64_t value) {
  for (unsigned int i = 0; i < 8; ++i) output[i] = static_cast<uint8_t>(value >> (8 * i));
}

void store32(uint8_t* output, uint32_t value) {
  for (unsigned int i = 0; i < 4; ++i) output[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint64_t rotate64(uint64_t value, unsigned int count) {
  return (value >> count) | (value << (64 - count));
}

uint32_t rotate32(uint32_t value, unsigned int count) {
  return (value >> count) | (value << (32 - count));
}

void mix_b(uint64_t* v, int a, int b, int c, int d, uint64_t x, uint64_t y) {
  v[a] = v[a] + v[b] + x; v[d] = rotate64(v[d] ^ v[a], 32);
  v[c] += v[d]; v[b] = rotate64(v[b] ^ v[c], 24);
  v[a] = v[a] + v[b] + y; v[d] = rotate64(v[d] ^ v[a], 16);
  v[c] += v[d]; v[b] = rotate64(v[b] ^ v[c], 63);
}

void mix_s(uint32_t* v, int a, int b, int c, int d, uint32_t x, uint32_t y) {
  v[a] = v[a] + v[b] + x; v[d] = rotate32(v[d] ^ v[a], 16);
  v[c] += v[d]; v[b] = rotate32(v[b] ^ v[c], 12);
  v[a] = v[a] + v[b] + y; v[d] = rotate32(v[d] ^ v[a], 8);
  v[c] += v[d]; v[b] = rotate32(v[b] ^ v[c], 7);
}

void compress(Blake2bState& state, const uint8_t block[128]) {
  uint64_t message[16], v[16];
  for (int i = 0; i < 16; ++i) message[i] = load64(block + i * 8);
  for (int i = 0; i < 8; ++i) { v[i] = state.h[i]; v[i + 8] = kIvB[i]; }
  v[12] ^= state.t[0]; v[13] ^= state.t[1]; v[14] ^= state.f[0]; v[15] ^= state.f[1];
  for (int round = 0; round < 12; ++round) {
    const auto* s = kSigma[round];
    mix_b(v,0,4,8,12,message[s[0]],message[s[1]]);
    mix_b(v,1,5,9,13,message[s[2]],message[s[3]]);
    mix_b(v,2,6,10,14,message[s[4]],message[s[5]]);
    mix_b(v,3,7,11,15,message[s[6]],message[s[7]]);
    mix_b(v,0,5,10,15,message[s[8]],message[s[9]]);
    mix_b(v,1,6,11,12,message[s[10]],message[s[11]]);
    mix_b(v,2,7,8,13,message[s[12]],message[s[13]]);
    mix_b(v,3,4,9,14,message[s[14]],message[s[15]]);
  }
  for (int i = 0; i < 8; ++i) state.h[i] ^= v[i] ^ v[i + 8];
}

void compress(Blake2sState& state, const uint8_t block[64]) {
  uint32_t message[16], v[16];
  for (int i = 0; i < 16; ++i) message[i] = load32(block + i * 4);
  for (int i = 0; i < 8; ++i) { v[i] = state.h[i]; v[i + 8] = kIvS[i]; }
  v[12] ^= state.t[0]; v[13] ^= state.t[1]; v[14] ^= state.f[0]; v[15] ^= state.f[1];
  for (int round = 0; round < 10; ++round) {
    const auto* s = kSigma[round];
    mix_s(v,0,4,8,12,message[s[0]],message[s[1]]);
    mix_s(v,1,5,9,13,message[s[2]],message[s[3]]);
    mix_s(v,2,6,10,14,message[s[4]],message[s[5]]);
    mix_s(v,3,7,11,15,message[s[6]],message[s[7]]);
    mix_s(v,0,5,10,15,message[s[8]],message[s[9]]);
    mix_s(v,1,6,11,12,message[s[10]],message[s[11]]);
    mix_s(v,2,7,8,13,message[s[12]],message[s[13]]);
    mix_s(v,3,4,9,14,message[s[14]],message[s[15]]);
  }
  for (int i = 0; i < 8; ++i) state.h[i] ^= v[i] ^ v[i + 8];
}

void add_counter(Blake2bState& state, uint64_t increment) {
  const uint64_t previous = state.t[0];
  state.t[0] += increment;
  if (state.t[0] < previous) ++state.t[1];
}

void add_counter(Blake2sState& state, uint32_t increment) {
  const uint32_t previous = state.t[0];
  state.t[0] += increment;
  if (state.t[0] < previous) ++state.t[1];
}

template <typename State, size_t BlockSize>
void update_state(State& state, const uint8_t* input, size_t size) {
  if (!size) return;
  size_t fill = BlockSize - state.buffered;
  if (size > fill) {
    std::memcpy(state.buffer + state.buffered, input, fill);
    state.buffered = 0;
    add_counter(state, BlockSize);
    compress(state, state.buffer);
    input += fill; size -= fill;
    while (size > BlockSize) {
      add_counter(state, BlockSize);
      compress(state, input);
      input += BlockSize; size -= BlockSize;
    }
  }
  std::memcpy(state.buffer + state.buffered, input, size);
  state.buffered += size;
}

void final_state(Blake2bState state, uint8_t* output) {
  add_counter(state, static_cast<uint64_t>(state.buffered));
  state.f[0] = ~uint64_t{0};
  if (state.last_node) state.f[1] = ~uint64_t{0};
  std::memset(state.buffer + state.buffered, 0, sizeof(state.buffer) - state.buffered);
  compress(state, state.buffer);
  uint8_t full[64];
  for (int i = 0; i < 8; ++i) store64(full + i * 8, state.h[i]);
  std::memcpy(output, full, state.output_size);
}

void final_state(Blake2sState state, uint8_t* output) {
  add_counter(state, static_cast<uint32_t>(state.buffered));
  state.f[0] = ~uint32_t{0};
  if (state.last_node) state.f[1] = ~uint32_t{0};
  std::memset(state.buffer + state.buffered, 0, sizeof(state.buffer) - state.buffered);
  compress(state, state.buffer);
  uint8_t full[32];
  for (int i = 0; i < 8; ++i) store32(full + i * 4, state.h[i]);
  std::memcpy(output, full, state.output_size);
}

void cleanup_blake(void* pointer) { delete static_cast<BlakeState*>(pointer); }

void cleanup_package(void* pointer) {
  auto* state = static_cast<PackageState*>(pointer);
  if (!state) return;
  state->host->value_release(state->blake2b_class);
  state->host->value_release(state->blake2s_class);
  delete state;
}

const X3Value* keyword(const X3KeywordArg* kwargs, uint32_t count, std::string_view name) {
  for (uint32_t i = 0; i < count; ++i)
    if (kwargs[i].name && name == kwargs[i].name) return &kwargs[i].value;
  return nullptr;
}

bool bytes_data(PackageState* state, X3Runtime* runtime, X3Value value,
                const uint8_t** data, size_t* size) {
  const void* raw = nullptr;
  uint64_t length = 0;
  if (state->host->value_bytes_data(runtime, value, &raw, &length) != X3_STATUS_OK ||
      length > std::numeric_limits<size_t>::max()) return false;
  *data = static_cast<const uint8_t*>(raw);
  *size = static_cast<size_t>(length);
  return true;
}

bool unsigned_value(X3Value value, uint64_t* output, bool* negative) {
  *negative = false;
  if (value.tag == X3_TAG_UINT64) { *output = value.as.u64; return true; }
  if (value.tag == X3_TAG_INT64) {
    if (value.as.i64 < 0) { *negative = true; return false; }
    *output = static_cast<uint64_t>(value.as.i64); return true;
  }
  return false;
}

bool truth_value(X3Value value, bool* output) {
  if (value.tag == X3_TAG_BOOL) { *output = value.as.b != 0; return true; }
  if (value.tag == X3_TAG_INT64) { *output = value.as.i64 != 0; return true; }
  if (value.tag == X3_TAG_UINT64) { *output = value.as.u64 != 0; return true; }
  return false;
}

X3Status argument_error(PackageState* state, X3CallContext* call, const char* message,
                        const char* type = "ValueError") {
  return state->host->raise_class_error(call, type, message);
}

BlakeState* blake_state(PackageState* state, X3CallContext* call, X3Value self) {
  auto* native = static_cast<BlakeState*>(state->host->instance_get_native_data(self, kBlake2bType));
  if (!native) native = static_cast<BlakeState*>(state->host->instance_get_native_data(self, kBlake2sType));
  if (!native) state->host->raise_class_error(call, "TypeError", "invalid BLAKE2 object");
  return native;
}

X3Status blake_init_kw(X3CallContext* call, X3Runtime* runtime, void* user_data,
                       const X3Value* args, uint32_t argc,
                       const X3KeywordArg* kwargs, uint32_t kwargc, X3Value* result) {
  auto* constructor = static_cast<ConstructorState*>(user_data);
  auto* state = constructor->package;
  if (argc < 1 || argc > 2)
    return argument_error(state, call, "BLAKE2 constructor received an invalid number of arguments", "TypeError");
  static constexpr std::string_view allowed[] = {
      "data", "digest_size", "key", "salt", "person", "fanout", "depth",
      "leaf_size", "node_offset", "node_depth", "inner_size", "last_node",
      "usedforsecurity", "string"};
  for (uint32_t i = 0; i < kwargc; ++i) {
    const std::string_view name(kwargs[i].name ? kwargs[i].name : "");
    if (std::find(std::begin(allowed), std::end(allowed), name) == std::end(allowed))
      return argument_error(state, call, "BLAKE2 constructor got an unexpected keyword argument", "TypeError");
  }
  const X3Value* data_kw = keyword(kwargs, kwargc, "data");
  const X3Value* string_kw = keyword(kwargs, kwargc, "string");
  if ((argc == 2 && data_kw) || (string_kw && (argc == 2 || data_kw)))
    return argument_error(state, call,
        "'data' and 'string' are mutually exclusive and support for 'string' keyword parameter is deprecated",
        "TypeError");
  X3Value data_value = argc == 2 ? args[1] : (data_kw ? *data_kw : (string_kw ? *string_kw : x3_value_none()));
  const bool has_data = argc == 2 || data_kw || string_kw;

  const uint64_t max_digest = constructor->blake2b ? 64 : 32;
  const uint64_t max_key = constructor->blake2b ? 64 : 32;
  const uint64_t salt_size = constructor->blake2b ? 16 : 8;
  uint64_t digest_size = max_digest, fanout = 1, depth = 1, leaf_size = 0,
           node_offset = 0, node_depth = 0, inner_size = 0;
  struct Numeric { const char* name; uint64_t* target; uint64_t maximum; bool nonzero; bool overflow; };
  Numeric numerics[] = {
      {"digest_size", &digest_size, max_digest, true, false},
      {"fanout", &fanout, 255, false, false}, {"depth", &depth, 255, true, false},
      {"leaf_size", &leaf_size, 0xFFFFFFFFULL, false, true},
      {"node_offset", &node_offset, constructor->blake2b ? UINT64_MAX : 0xFFFFFFFFFFFFULL, false, true},
      {"node_depth", &node_depth, 255, false, false},
      {"inner_size", &inner_size, max_digest, false, false}};
  for (auto& numeric : numerics) {
    if (const X3Value* value = keyword(kwargs, kwargc, numeric.name)) {
      uint64_t parsed = 0; bool negative = false;
      if (!unsigned_value(*value, &parsed, &negative))
        return argument_error(state, call, negative ? "value must be non-negative" : "an integer is required",
                              negative ? "ValueError" : "TypeError");
      if (parsed > numeric.maximum)
        return argument_error(state, call, numeric.overflow ? "value is too large" : "value is out of range",
                              numeric.overflow ? "OverflowError" : "ValueError");
      if (numeric.nonzero && parsed == 0)
        return argument_error(state, call, "value must be greater than zero");
      *numeric.target = parsed;
    }
  }
  bool last_node = false;
  if (const X3Value* value = keyword(kwargs, kwargc, "last_node"))
    if (!truth_value(*value, &last_node))
      return argument_error(state, call, "last_node must be a boolean", "TypeError");

  const uint8_t *key_data = nullptr, *salt_data = nullptr, *person_data = nullptr, *initial_data = nullptr;
  size_t key_length = 0, salt_length = 0, person_length = 0, initial_length = 0;
  auto get_buffer = [&](const char* name, const uint8_t** output, size_t* length) -> bool {
    const X3Value* value = keyword(kwargs, kwargc, name);
    if (!value) return true;
    return bytes_data(state, runtime, *value, output, length);
  };
  if (!get_buffer("key", &key_data, &key_length) || !get_buffer("salt", &salt_data, &salt_length) ||
      !get_buffer("person", &person_data, &person_length))
    return argument_error(state, call, "a bytes-like object is required", "TypeError");
  if (key_length > max_key) return argument_error(state, call, "maximum key length exceeded");
  if (salt_length > salt_size) return argument_error(state, call, "maximum salt length exceeded");
  if (person_length > salt_size) return argument_error(state, call, "maximum person length exceeded");
  if (has_data && !bytes_data(state, runtime, data_value, &initial_data, &initial_length))
    return argument_error(state, call, "a bytes-like object is required", "TypeError");

  auto native = std::make_unique<BlakeState>();
  native->blake2b = constructor->blake2b;
  if (native->blake2b) {
    uint8_t parameter[64]{};
    parameter[0] = static_cast<uint8_t>(digest_size); parameter[1] = static_cast<uint8_t>(key_length);
    parameter[2] = static_cast<uint8_t>(fanout); parameter[3] = static_cast<uint8_t>(depth);
    store32(parameter + 4, static_cast<uint32_t>(leaf_size)); store64(parameter + 8, node_offset);
    parameter[16] = static_cast<uint8_t>(node_depth); parameter[17] = static_cast<uint8_t>(inner_size);
    if (salt_length) std::memcpy(parameter + 32, salt_data, salt_length);
    if (person_length) std::memcpy(parameter + 48, person_data, person_length);
    for (int i = 0; i < 8; ++i) native->b.h[i] = kIvB[i] ^ load64(parameter + i * 8);
    native->b.output_size = static_cast<uint8_t>(digest_size); native->b.last_node = last_node;
    if (key_length) {
      uint8_t block[128]{}; std::memcpy(block, key_data, key_length);
      update_state<Blake2bState, 128>(native->b, block, sizeof(block));
    }
    if (initial_length) update_state<Blake2bState, 128>(native->b, initial_data, initial_length);
  } else {
    uint8_t parameter[32]{};
    parameter[0] = static_cast<uint8_t>(digest_size); parameter[1] = static_cast<uint8_t>(key_length);
    parameter[2] = static_cast<uint8_t>(fanout); parameter[3] = static_cast<uint8_t>(depth);
    store32(parameter + 4, static_cast<uint32_t>(leaf_size));
    for (int i = 0; i < 6; ++i) parameter[8 + i] = static_cast<uint8_t>(node_offset >> (8 * i));
    parameter[14] = static_cast<uint8_t>(node_depth); parameter[15] = static_cast<uint8_t>(inner_size);
    if (salt_length) std::memcpy(parameter + 16, salt_data, salt_length);
    if (person_length) std::memcpy(parameter + 24, person_data, person_length);
    for (int i = 0; i < 8; ++i) native->s.h[i] = kIvS[i] ^ load32(parameter + i * 4);
    native->s.output_size = static_cast<uint8_t>(digest_size); native->s.last_node = last_node;
    if (key_length) {
      uint8_t block[64]{}; std::memcpy(block, key_data, key_length);
      update_state<Blake2sState, 64>(native->s, block, sizeof(block));
    }
    if (initial_length) update_state<Blake2sState, 64>(native->s, initial_data, initial_length);
  }
  const char* type = native->blake2b ? kBlake2bType : kBlake2sType;
  if (state->host->instance_set_native_data(args[0], type, native.get(), cleanup_blake) != X3_STATUS_OK)
    return argument_error(state, call, "cannot initialize BLAKE2 object", "RuntimeError");
  native.release();
  *result = x3_value_none();
  return X3_STATUS_OK;
}

X3Status blake_init(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  return blake_init_kw(call, runtime, user_data, args, argc, nullptr, 0, result);
}

X3Status blake_update(X3CallContext* call, X3Runtime* runtime, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 2) return argument_error(state, call, "update() takes exactly one argument", "TypeError");
  auto* native = blake_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  const uint8_t* data = nullptr; size_t size = 0;
  if (!bytes_data(state, runtime, args[1], &data, &size))
    return argument_error(state, call, "a bytes-like object is required", "TypeError");
  if (native->blake2b) update_state<Blake2bState, 128>(native->b, data, size);
  else update_state<Blake2sState, 64>(native->s, data, size);
  *result = x3_value_none();
  return X3_STATUS_OK;
}

void digest_bytes(const BlakeState& state, uint8_t* output) {
  if (state.blake2b) final_state(state.b, output); else final_state(state.s, output);
}

X3Status blake_digest_common(PackageState* state, X3CallContext* call, X3Runtime* runtime,
                             const X3Value* args, uint32_t argc, bool hexadecimal, X3Value* result) {
  if (argc != 1) return argument_error(state, call, "digest() takes no arguments", "TypeError");
  auto* native = blake_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  uint8_t bytes[64]{};
  digest_bytes(*native, bytes);
  const size_t size = native->blake2b ? native->b.output_size : native->s.output_size;
  if (!hexadecimal) {
    *result = state->host->value_bytes(runtime, bytes, size);
  } else {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text(size * 2, '0');
    for (size_t i = 0; i < size; ++i) { text[i * 2] = digits[bytes[i] >> 4]; text[i * 2 + 1] = digits[bytes[i] & 15]; }
    *result = state->host->value_string_utf8(runtime, text.data(), text.size());
  }
  return X3_STATUS_OK;
}

X3Status blake_digest(X3CallContext* call, X3Runtime* runtime, void* user_data,
                      const X3Value* args, uint32_t argc, X3Value* result) {
  return blake_digest_common(static_cast<PackageState*>(user_data), call, runtime, args, argc, false, result);
}

X3Status blake_hexdigest(X3CallContext* call, X3Runtime* runtime, void* user_data,
                         const X3Value* args, uint32_t argc, X3Value* result) {
  return blake_digest_common(static_cast<PackageState*>(user_data), call, runtime, args, argc, true, result);
}

X3Status blake_copy(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return argument_error(state, call, "copy() takes no arguments", "TypeError");
  auto* source = blake_state(state, call, args[0]);
  if (!source) return X3_STATUS_ERROR;
  auto native = std::make_unique<BlakeState>(*source);
  X3Value klass = source->blake2b ? state->blake2b_class : state->blake2s_class;
  X3Value instance = state->host->value_instance(runtime, klass);
  const char* type = source->blake2b ? kBlake2bType : kBlake2sType;
  if (instance.tag == X3_TAG_INVALID ||
      state->host->instance_set_native_data(instance, type, native.get(), cleanup_blake) != X3_STATUS_OK) {
    if (instance.tag != X3_TAG_INVALID) state->host->value_release(instance);
    return argument_error(state, call, "cannot copy BLAKE2 object", "RuntimeError");
  }
  native.release(); *result = instance; return X3_STATUS_OK;
}

X3Status blake_name(X3CallContext* call, X3Runtime* runtime, void* user_data,
                    const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return argument_error(state, call, "invalid property access", "TypeError");
  auto* native = blake_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  *result = state->host->value_string(runtime, native->blake2b ? "blake2b" : "blake2s");
  return X3_STATUS_OK;
}

X3Status blake_digest_size(X3CallContext* call, X3Runtime*, void* user_data,
                           const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return argument_error(state, call, "invalid property access", "TypeError");
  auto* native = blake_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  *result = x3_value_int64(native->blake2b ? native->b.output_size : native->s.output_size);
  return X3_STATUS_OK;
}

X3Status blake_block_size(X3CallContext* call, X3Runtime*, void* user_data,
                          const X3Value* args, uint32_t argc, X3Value* result) {
  auto* state = static_cast<PackageState*>(user_data);
  if (argc != 1) return argument_error(state, call, "invalid property access", "TypeError");
  auto* native = blake_state(state, call, args[0]);
  if (!native) return X3_STATUS_ERROR;
  *result = x3_value_int64(native->blake2b ? 128 : 64);
  return X3_STATUS_OK;
}

void define_method(X3NativeFunctionDef& definition, const char* name, X3NativeFn function,
                   void* data, X3NativeKeywordFn keywords = nullptr) {
  definition = {}; definition.size = sizeof(definition); definition.name = name;
  definition.callback = function; definition.keyword_callback = keywords; definition.user_data = data;
}

bool add_property(PackageState* state, X3Runtime* runtime, X3Value klass,
                  const char* name, X3NativeFn getter) {
  X3Value property = x3_value_invalid();
  if (state->host->property_create(runtime, name, getter, nullptr, state, &property) != X3_STATUS_OK) return false;
  const bool ok = state->host->class_add_value(klass, name, property) == X3_STATUS_OK;
  state->host->value_release(property); return ok;
}

void add_class_int(PackageState* state, X3Value klass, const char* name, int64_t value) {
  state->host->class_add_value(klass, name, x3_value_int64(value));
}

X3Status register_module(X3PackageHost* host) {
  auto* state = new PackageState(); state->host = host;
  if (host->package_set_cleanup(host, state, cleanup_package) != X3_STATUS_OK) { delete state; return X3_STATUS_ERROR; }
  X3Module* module = nullptr;
  if (host->add_module(host, "_blake2", &module) != X3_STATUS_OK) return X3_STATUS_ERROR;

  state->blake2b_constructor = {state, true};
  state->blake2s_constructor = {state, false};
  auto add_class = [&](const char* name, ConstructorState* constructor, X3Value* klass) {
    X3NativeFunctionDef methods[5]{};
    define_method(methods[0], "__init__", blake_init, constructor, blake_init_kw);
    define_method(methods[1], "update", blake_update, state);
    define_method(methods[2], "digest", blake_digest, state);
    define_method(methods[3], "hexdigest", blake_hexdigest, state);
    define_method(methods[4], "copy", blake_copy, state);
    return host->module_add_class(module, name, methods, 5, klass) == X3_STATUS_OK &&
           add_property(state, host->runtime, *klass, "name", blake_name) &&
           add_property(state, host->runtime, *klass, "digest_size", blake_digest_size) &&
           add_property(state, host->runtime, *klass, "block_size", blake_block_size);
  };
  if (!add_class("blake2b", &state->blake2b_constructor, &state->blake2b_class) ||
      !add_class("blake2s", &state->blake2s_constructor, &state->blake2s_class)) return X3_STATUS_ERROR;
  add_class_int(state, state->blake2b_class, "SALT_SIZE", 16);
  add_class_int(state, state->blake2b_class, "PERSON_SIZE", 16);
  add_class_int(state, state->blake2b_class, "MAX_KEY_SIZE", 64);
  add_class_int(state, state->blake2b_class, "MAX_DIGEST_SIZE", 64);
  add_class_int(state, state->blake2s_class, "SALT_SIZE", 8);
  add_class_int(state, state->blake2s_class, "PERSON_SIZE", 8);
  add_class_int(state, state->blake2s_class, "MAX_KEY_SIZE", 32);
  add_class_int(state, state->blake2s_class, "MAX_DIGEST_SIZE", 32);
  const struct { const char* name; int64_t value; } constants[] = {
      {"BLAKE2B_SALT_SIZE",16},{"BLAKE2B_PERSON_SIZE",16},{"BLAKE2B_MAX_KEY_SIZE",64},{"BLAKE2B_MAX_DIGEST_SIZE",64},
      {"BLAKE2S_SALT_SIZE",8},{"BLAKE2S_PERSON_SIZE",8},{"BLAKE2S_MAX_KEY_SIZE",32},{"BLAKE2S_MAX_DIGEST_SIZE",32},
      {"_GIL_MINSIZE",2048}};
  for (const auto& constant : constants) host->module_add_value(module, constant.name, x3_value_int64(constant.value));
  return X3_STATUS_OK;
}

}  // namespace

extern "C" XLANG3_PACKAGE_EXPORT const uint32_t xlang3_package_abi_version = X3_ABI_VERSION;

extern "C" XLANG3_PACKAGE_EXPORT X3Status Load(void* host_pointer, X3Value) {
  auto* host = static_cast<X3PackageHost*>(host_pointer);
  if (!host || host->abi_version != X3_ABI_VERSION) return X3_STATUS_ERROR;
  host->package_set_metadata(host, "package", "xlang__blake2");
  host->package_set_metadata(host, "version", "0.1.0");
  return register_module(host);
}
