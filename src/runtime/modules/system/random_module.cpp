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

#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kRandomNativeType = "_random.Random";

struct RandomState {
  std::mutex mutex;
  std::mt19937_64 engine;
  uint64_t seed = 0;
  uint64_t draws = 0;
};

uint64_t fnv1a_bytes(std::string_view bytes) {
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char ch : bytes) {
    hash ^= ch;
    hash *= 1099511628211ull;
  }
  return hash;
}

uint64_t default_seed() {
  uint64_t seed = static_cast<uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  std::random_device device;
  seed ^= static_cast<uint64_t>(device()) << 32u;
  seed ^= static_cast<uint64_t>(device());
  return seed;
}

uint64_t seed_from_value(const Value& value) {
  if (value.tag == ValueTag::None || value.tag == ValueTag::Invalid) {
    return default_seed();
  }
  if (value.tag == ValueTag::Int64) {
    return static_cast<uint64_t>(value.as.i64);
  }
  if (value.tag == ValueTag::Double) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value.as.f64));
    std::memcpy(&bits, &value.as.f64, sizeof(bits));
    return bits;
  }
  if (auto* string = value_as_string(value)) {
    return fnv1a_bytes(string_object_view(*string));
  }
  if (auto* bytes = value_as_bytes(value)) {
    return fnv1a_bytes(bytes_object_view(*bytes));
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    return fnv1a_bytes(bytearray->value);
  }
  if (value_as_bigint(value) != nullptr) {
    return fnv1a_bytes(value_bigint_to_string(value));
  }
  return fnv1a_bytes(value_to_string(value));
}

RandomState* random_state(const Value& self, std::string& error) {
  auto* state = static_cast<RandomState*>(instance_get_native_data(self, kRandomNativeType));
  if (state == nullptr) {
    error = "invalid _random.Random object";
  }
  return state;
}

RandomState* ensure_random_state(const Value& self, std::string& error) {
  if (auto* state = random_state(self, error)) {
    return state;
  }
  error.clear();
  auto* state = new RandomState();
  state->seed = default_seed();
  state->engine.seed(state->seed);
  if (!instance_set_native_data(self, kRandomNativeType, state, [](void* data) { delete static_cast<RandomState*>(data); }, error)) {
    delete state;
    return nullptr;
  }
  return state;
}

void random_reseed(RandomState& state, uint64_t seed) {
  state.seed = seed;
  state.draws = 0;
  state.engine.seed(seed);
}

uint64_t random_next(RandomState& state) {
  ++state.draws;
  return state.engine();
}

bool random_init(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc > 2) {
    error = "_random.Random.__init__ expected optional seed";
    return false;
  }
  auto* state = ensure_random_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  const uint64_t seed = argc == 2 ? seed_from_value(args[1]) : default_seed();
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    random_reseed(*state, seed);
  }
  value_set_none(out);
  return true;
}

bool random_seed(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc < 1 || argc > 2) {
    error = "_random.Random.seed expected optional seed";
    return false;
  }
  auto* state = ensure_random_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  const uint64_t seed = argc == 2 ? seed_from_value(args[1]) : default_seed();
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    random_reseed(*state, seed);
  }
  value_set_none(out);
  return true;
}

bool random_random(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_random.Random.random expected no arguments";
    return false;
  }
  auto* state = ensure_random_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  uint64_t bits = 0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    bits = random_next(*state);
  }
  value_set_number(out, static_cast<double>(bits >> 11u) * (1.0 / 9007199254740992.0));
  return true;
}

bool random_getrandbits(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2 || args[1].tag != ValueTag::Int64) {
    error = "_random.Random.getrandbits expected integer bit count";
    return false;
  }
  const int64_t bit_count = args[1].as.i64;
  if (bit_count < 0) {
    error = "number of bits must be non-negative";
    return false;
  }
  auto* state = ensure_random_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  if (bit_count == 0) {
    value_set_int64(out, 0);
    return true;
  }
  const size_t byte_count = static_cast<size_t>((bit_count + 7) / 8);
  std::vector<uint8_t> bytes(byte_count);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    for (size_t offset = 0; offset < byte_count;) {
      uint64_t word = random_next(*state);
      for (uint32_t i = 0; i < 8 && offset < byte_count; ++i, ++offset) {
        bytes[offset] = static_cast<uint8_t>((word >> (i * 8u)) & 0xffu);
      }
    }
  }
  const uint32_t extra_bits = static_cast<uint32_t>(byte_count * 8 - bit_count);
  if (extra_bits != 0) {
    bytes.back() &= static_cast<uint8_t>(0xffu >> extra_bits);
  }
  if (byte_count <= 8) {
    uint64_t value = 0;
    for (size_t i = 0; i < byte_count; ++i) {
      value |= static_cast<uint64_t>(bytes[i]) << (i * 8u);
    }
    if (value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      value_set_int64(out, static_cast<int64_t>(value));
      return true;
    }
  }
  return value_bigint_from_bytes(bytes.data(), bytes.size(), false, false, out, error);
}

bool random_getstate(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "_random.Random.getstate expected no arguments";
    return false;
  }
  auto* state = ensure_random_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  uint64_t seed = 0;
  uint64_t draws = 0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    seed = state->seed;
    draws = state->draws;
  }
  out = Value::tuple({Value::int64(static_cast<int64_t>(seed)), Value::int64(static_cast<int64_t>(draws))});
  return true;
}

bool random_setstate(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "_random.Random.setstate expected state";
    return false;
  }
  auto* tuple = value_as_tuple(args[1]);
  if (tuple == nullptr || tuple->items.size() < 2 || tuple->items[0].tag != ValueTag::Int64 || tuple->items[1].tag != ValueTag::Int64) {
    error = "state vector is invalid";
    return false;
  }
  auto* state = ensure_random_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    random_reseed(*state, static_cast<uint64_t>(tuple->items[0].as.i64));
    const uint64_t draws = static_cast<uint64_t>(tuple->items[1].as.i64);
    for (uint64_t i = 0; i < draws; ++i) {
      (void)random_next(*state);
    }
  }
  value_set_none(out);
  return true;
}

} // namespace

void register_random_module(Runtime& runtime) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.emplace_back("__module__", Value::string("_random"));
  attrs.emplace_back("__qualname__", Value::string("Random"));
  attrs.emplace_back("__init__", runtime.make_native_function("_random.Random.__init__", random_init));
  attrs.emplace_back("seed", runtime.make_native_function("_random.Random.seed", random_seed));
  attrs.emplace_back("random", runtime.make_native_function("_random.Random.random", random_random));
  attrs.emplace_back("getrandbits", runtime.make_native_function("_random.Random.getrandbits", random_getrandbits));
  attrs.emplace_back("getstate", runtime.make_native_function("_random.Random.getstate", random_getstate));
  attrs.emplace_back("setstate", runtime.make_native_function("_random.Random.setstate", random_setstate));

  NativeModuleBuilder builder(runtime, "_random");
  builder.value("Random", Value::class_object("Random", std::move(attrs)));
  runtime.register_module("_random", builder.finish());
}

} // namespace xlang3
