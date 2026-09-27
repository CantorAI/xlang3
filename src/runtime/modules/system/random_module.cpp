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
#include <array>
#include <algorithm>
#include <cstdlib>
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
  // MT19937 state layout and seeding follow CPython's _random module.
  std::array<uint32_t, 624> words{};
  size_t index = 624;
};

// MT19937 by Matsumoto and Nishimura; init_by_array and the 53-bit float
// construction match CPython 3.14 Modules/_randommodule.c (BSD licensed).
uint32_t random_next(RandomState& state) {
  constexpr size_t n = 624, m = 397;
  if (state.index >= n) {
    for (size_t i = 0; i < n; ++i) {
      const uint32_t y = (state.words[i] & 0x80000000u) |
                         (state.words[(i + 1) % n] & 0x7fffffffu);
      state.words[i] = state.words[(i + m) % n] ^ (y >> 1) ^
                       ((y & 1u) ? 0x9908b0dfu : 0u);
    }
    state.index = 0;
  }
  uint32_t y = state.words[state.index++];
  y ^= y >> 11;
  y ^= (y << 7) & 0x9d2c5680u;
  y ^= (y << 15) & 0xefc60000u;
  y ^= y >> 18;
  return y;
}

void random_reseed(RandomState& state, const std::vector<uint32_t>& key) {
  auto& mt = state.words;
  mt[0] = 19650218u;
  for (size_t i = 1; i < mt.size(); ++i)
    mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30)) +
            static_cast<uint32_t>(i);
  state.index = mt.size();
  size_t i = 1, j = 0;
  for (size_t k = std::max(mt.size(), key.size()); k != 0; --k) {
    mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525u)) +
            key[j] + static_cast<uint32_t>(j);
    if (++i >= mt.size()) { mt[0] = mt.back(); i = 1; }
    if (++j >= key.size()) j = 0;
  }
  for (size_t k = mt.size() - 1; k != 0; --k) {
    mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941u)) -
            static_cast<uint32_t>(i);
    if (++i >= mt.size()) { mt[0] = mt.back(); i = 1; }
  }
  mt[0] = 0x80000000u;
}

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

std::vector<uint32_t> seed_from_value(const Value& value) {
  if (value.tag == ValueTag::None || value.tag == ValueTag::Invalid) {
    const uint64_t seed = default_seed();
    return {static_cast<uint32_t>(seed), static_cast<uint32_t>(seed >> 32)};
  }
  if (value.tag == ValueTag::Int64) {
    const uint64_t magnitude = value.as.i64 < 0
        ? static_cast<uint64_t>(-(value.as.i64 + 1)) + 1
        : static_cast<uint64_t>(value.as.i64);
    if (magnitude <= UINT32_MAX) return {static_cast<uint32_t>(magnitude)};
    return {static_cast<uint32_t>(magnitude),
            static_cast<uint32_t>(magnitude >> 32)};
  }
  if (value_as_bigint(value) != nullptr) {
    bool negative = false;
    const uint32_t* limbs = nullptr;
    uint32_t count = 0;
    if (value_bigint_limb_view(value, negative, limbs, count)) {
      if (count == 0) return {0};
      return std::vector<uint32_t>(limbs, limbs + count);
    }
  }
  if (value.tag == ValueTag::Double) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value.as.f64));
    std::memcpy(&bits, &value.as.f64, sizeof(bits));
    return {static_cast<uint32_t>(bits), static_cast<uint32_t>(bits >> 32)};
  }
  if (auto* string = value_as_string(value)) {
    const uint64_t hash = fnv1a_bytes(string_object_view(*string));
    return {static_cast<uint32_t>(hash), static_cast<uint32_t>(hash >> 32)};
  }
  if (auto* bytes = value_as_bytes(value)) {
    const uint64_t hash = fnv1a_bytes(bytes_object_view(*bytes));
    return {static_cast<uint32_t>(hash), static_cast<uint32_t>(hash >> 32)};
  }
  if (auto* bytearray = value_as_bytearray(value)) {
    const uint64_t hash = fnv1a_bytes(bytearray->value);
    return {static_cast<uint32_t>(hash), static_cast<uint32_t>(hash >> 32)};
  }
  const uint64_t hash = fnv1a_bytes(value_to_string(value));
  return {static_cast<uint32_t>(hash), static_cast<uint32_t>(hash >> 32)};
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
  random_reseed(*state, seed_from_value(Value::none()));
  if (!instance_set_native_data(self, kRandomNativeType, state, [](void* data) { delete static_cast<RandomState*>(data); }, error)) {
    delete state;
    return nullptr;
  }
  return state;
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
  const auto seed = argc == 2 ? seed_from_value(args[1]) : seed_from_value(Value::none());
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
  const auto seed = argc == 2 ? seed_from_value(args[1]) : seed_from_value(Value::none());
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
  uint32_t a = 0, b = 0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    a = random_next(*state) >> 5;
    b = random_next(*state) >> 6;
  }
  value_set_number(out, (a * 67108864.0 + b) * (1.0 / 9007199254740992.0));
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
  if (static_cast<uint64_t>(bit_count) >
      static_cast<uint64_t>(std::numeric_limits<size_t>::max() / 4) * 32) {
    error = "number of bits is too large";
    return false;
  }
  const size_t word_count = static_cast<size_t>((static_cast<uint64_t>(bit_count) + 31) / 32);
  std::vector<uint32_t> words(word_count);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    for (size_t i = 0; i < word_count; ++i) {
      const uint64_t remaining = static_cast<uint64_t>(bit_count) - i * 32;
      words[i] = random_next(*state);
      if (remaining < 32) words[i] >>= (32 - remaining);
    }
  }
  if (word_count <= 2) {
    const uint64_t value = words[0] |
        (word_count == 2 ? static_cast<uint64_t>(words[1]) << 32 : 0);
    if (value <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      value_set_int64(out, static_cast<int64_t>(value));
      return true;
    }
  }
  while (!words.empty() && words.back() == 0) words.pop_back();
  if (words.empty()) {
    value_set_int64(out, 0);
    return true;
  }
  return value_bigint_from_binary_limbs(words.data(), words.size() * sizeof(uint32_t),
                                        false, out, error);
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
  std::vector<Value> items;
  items.reserve(625);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    for (uint32_t word : state->words) items.push_back(Value::int64(word));
    items.push_back(Value::int64(static_cast<int64_t>(state->index)));
  }
  out = Value::tuple(std::move(items));
  return true;
}

bool random_setstate(Runtime&, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "_random.Random.setstate expected state";
    return false;
  }
  auto* tuple = value_as_tuple(args[1]);
  if (tuple == nullptr) {
    error = "state vector must be a tuple";
    return false;
  }
  if (tuple->items.size() != 625) {
    error = "state vector is the wrong size";
    return false;
  }
  std::array<uint32_t, 624> words{};
  for (size_t i = 0; i < words.size(); ++i) {
    if (tuple->items[i].tag != ValueTag::Int64 ||
        tuple->items[i].as.i64 < 0 || tuple->items[i].as.i64 > UINT32_MAX) {
      error = "state vector is invalid";
      return false;
    }
    words[i] = static_cast<uint32_t>(tuple->items[i].as.i64);
  }
  if (tuple->items[624].tag != ValueTag::Int64 ||
      tuple->items[624].as.i64 < 0 || tuple->items[624].as.i64 > 624) {
    error = "invalid state";
    return false;
  }
  auto* state = ensure_random_state(args[0], error);
  if (state == nullptr) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->words = words;
    state->index = static_cast<size_t>(tuple->items[624].as.i64);
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
