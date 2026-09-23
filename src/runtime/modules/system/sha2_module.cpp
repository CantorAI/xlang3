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
#include "xlang3/module_object.h"
#include "xlang3/object_model.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xlang3 {

namespace {

constexpr const char* kSha2NativeType = "_sha2.HASH";

struct Sha2State {
  std::string data;
  enum class Variant { Sha224, Sha256, Sha384, Sha512 } variant = Variant::Sha256;
};

Value g_sha224_class;
Value g_sha256_class;
Value g_sha384_class;
Value g_sha512_class;

void sha2_cleanup(void* data) {
  delete static_cast<Sha2State*>(data);
}

bool sha2_bytes_view(const Value& value, std::string_view& out, std::string& error) {
  if (auto* bytes = value_as_bytes(value)) {
    out = bytes_object_view(*bytes);
    return true;
  }
  if (auto* array = value_as_bytearray(value)) {
    out = array->value;
    return true;
  }
  if (auto* view = value_as_memoryview(value); view != nullptr && !view->released) {
    out = memoryview_object_view(*view);
    return true;
  }
  error = "object supporting the buffer API required";
  return false;
}

Sha2State* sha2_state(const Value& self, std::string& error) {
  auto* state = static_cast<Sha2State*>(instance_get_native_data(self, kSha2NativeType));
  if (state == nullptr) {
    error = "invalid SHA-2 hash object";
  }
  return state;
}

constexpr std::array<uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

uint32_t rotate_right(uint32_t value, unsigned shift) {
  return (value >> shift) | (value << (32u - shift));
}

std::string sha2_digest(std::string_view source, bool sha224) {
  std::string padded(source);
  const uint64_t bit_length = static_cast<uint64_t>(source.size()) * 8u;
  padded.push_back(static_cast<char>(0x80));
  while ((padded.size() % 64u) != 56u) {
    padded.push_back('\0');
  }
  for (int shift = 56; shift >= 0; shift -= 8) {
    padded.push_back(static_cast<char>((bit_length >> shift) & 0xffu));
  }

  std::array<uint32_t, 8> hash = sha224
      ? std::array<uint32_t, 8>{
            0xc1059ed8u, 0x367cd507u, 0x3070dd17u, 0xf70e5939u,
            0xffc00b31u, 0x68581511u, 0x64f98fa7u, 0xbefa4fa4u}
      : std::array<uint32_t, 8>{
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

  for (size_t block = 0; block < padded.size(); block += 64) {
    std::array<uint32_t, 64> words{};
    for (size_t index = 0; index < 16; ++index) {
      const size_t offset = block + index * 4;
      words[index] =
          (static_cast<uint32_t>(static_cast<unsigned char>(padded[offset])) << 24u) |
          (static_cast<uint32_t>(static_cast<unsigned char>(padded[offset + 1])) << 16u) |
          (static_cast<uint32_t>(static_cast<unsigned char>(padded[offset + 2])) << 8u) |
          static_cast<uint32_t>(static_cast<unsigned char>(padded[offset + 3]));
    }
    for (size_t index = 16; index < words.size(); ++index) {
      const uint32_t s0 = rotate_right(words[index - 15], 7) ^
                          rotate_right(words[index - 15], 18) ^
                          (words[index - 15] >> 3u);
      const uint32_t s1 = rotate_right(words[index - 2], 17) ^
                          rotate_right(words[index - 2], 19) ^
                          (words[index - 2] >> 10u);
      words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    uint32_t a = hash[0];
    uint32_t b = hash[1];
    uint32_t c = hash[2];
    uint32_t d = hash[3];
    uint32_t e = hash[4];
    uint32_t f = hash[5];
    uint32_t g = hash[6];
    uint32_t h = hash[7];
    for (size_t index = 0; index < words.size(); ++index) {
      const uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
      const uint32_t choose = (e & f) ^ ((~e) & g);
      const uint32_t temp1 = h + sum1 + choose + kSha256RoundConstants[index] + words[index];
      const uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  const size_t digest_words = sha224 ? 7 : 8;
  std::string digest;
  digest.reserve(digest_words * 4);
  for (size_t index = 0; index < digest_words; ++index) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      digest.push_back(static_cast<char>((hash[index] >> shift) & 0xffu));
    }
  }
  return digest;
}

constexpr std::array<uint64_t, 80> kSha512RoundConstants = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffCull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
};

uint64_t rotate_right64(uint64_t value, unsigned shift) {
  return (value >> shift) | (value << (64u - shift));
}

std::string sha512_digest(std::string_view source, bool sha384) {
  std::string padded(source);
  const uint64_t bit_length = static_cast<uint64_t>(source.size()) * 8u;
  padded.push_back(static_cast<char>(0x80));
  while ((padded.size() % 128u) != 112u) padded.push_back('\0');
  for (int shift = 56; shift >= 0; shift -= 8) padded.push_back('\0');
  for (int shift = 56; shift >= 0; shift -= 8)
    padded.push_back(static_cast<char>((bit_length >> shift) & 0xffu));

  std::array<uint64_t, 8> hash = sha384
      ? std::array<uint64_t, 8>{0xcbbb9d5dc1059ed8ull, 0x629a292a367cd507ull,
          0x9159015a3070dd17ull, 0x152fecd8f70e5939ull, 0x67332667ffc00b31ull,
          0x8eb44a8768581511ull, 0xdb0c2e0d64f98fa7ull, 0x47b5481dbefa4fa4ull}
      : std::array<uint64_t, 8>{0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull,
          0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull, 0x510e527fade682d1ull,
          0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
  for (size_t block = 0; block < padded.size(); block += 128) {
    std::array<uint64_t, 80> words{};
    for (size_t index = 0; index < 16; ++index) {
      const size_t offset = block + index * 8;
      for (size_t byte = 0; byte < 8; ++byte)
        words[index] = (words[index] << 8u) |
            static_cast<unsigned char>(padded[offset + byte]);
    }
    for (size_t index = 16; index < words.size(); ++index) {
      const uint64_t s0 = rotate_right64(words[index - 15], 1) ^
          rotate_right64(words[index - 15], 8) ^ (words[index - 15] >> 7u);
      const uint64_t s1 = rotate_right64(words[index - 2], 19) ^
          rotate_right64(words[index - 2], 61) ^ (words[index - 2] >> 6u);
      words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }
    uint64_t a=hash[0], b=hash[1], c=hash[2], d=hash[3];
    uint64_t e=hash[4], f=hash[5], g=hash[6], h=hash[7];
    for (size_t index = 0; index < words.size(); ++index) {
      const uint64_t sum1 = rotate_right64(e,14)^rotate_right64(e,18)^rotate_right64(e,41);
      const uint64_t choose = (e&f)^((~e)&g);
      const uint64_t temp1 = h + sum1 + choose + kSha512RoundConstants[index] + words[index];
      const uint64_t sum0 = rotate_right64(a,28)^rotate_right64(a,34)^rotate_right64(a,39);
      const uint64_t majority = (a&b)^(a&c)^(b&c);
      const uint64_t temp2 = sum0 + majority;
      h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
    }
    hash[0]+=a; hash[1]+=b; hash[2]+=c; hash[3]+=d;
    hash[4]+=e; hash[5]+=f; hash[6]+=g; hash[7]+=h;
  }
  const size_t digest_words = sha384 ? 6 : 8;
  std::string digest;
  digest.reserve(digest_words * 8);
  for (size_t index = 0; index < digest_words; ++index)
    for (int shift = 56; shift >= 0; shift -= 8)
      digest.push_back(static_cast<char>((hash[index] >> shift) & 0xffu));
  return digest;
}

std::string sha2_digest(std::string_view source, Sha2State::Variant variant) {
  if (variant == Sha2State::Variant::Sha384 || variant == Sha2State::Variant::Sha512)
    return sha512_digest(source, variant == Sha2State::Variant::Sha384);
  return sha2_digest(source, variant == Sha2State::Variant::Sha224);
}

bool sha2_update(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 2) {
    error = "HASH.update() expected one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = sha2_state(args[0], error);
  std::string_view data;
  if (state == nullptr || !sha2_bytes_view(args[1], data, error)) {
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  state->data.append(data.data(), data.size());
  value_set_none(out);
  return true;
}

bool sha2_digest_method(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "HASH.digest() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = sha2_state(args[0], error);
  if (state == nullptr) return false;
  out = Value::bytes(sha2_digest(state->data, state->variant));
  return true;
}

bool sha2_hexdigest(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "HASH.hexdigest() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = sha2_state(args[0], error);
  if (state == nullptr) return false;
  static constexpr char digits[] = "0123456789abcdef";
  const std::string digest = sha2_digest(state->data, state->variant);
  std::string hex;
  hex.reserve(digest.size() * 2);
  for (unsigned char byte : digest) {
    hex.push_back(digits[byte >> 4u]);
    hex.push_back(digits[byte & 0x0fu]);
  }
  out = Value::string(std::move(hex));
  return true;
}

bool initialize_sha2_instance(Value instance, std::string data, Sha2State::Variant variant,
                              Value& out, std::string& error) {
  auto* state = new Sha2State{std::move(data), variant};
  if (!instance_set_native_data(instance, kSha2NativeType, state, sha2_cleanup, error)) {
    delete state;
    return false;
  }
  const bool wide = variant == Sha2State::Variant::Sha384 || variant == Sha2State::Variant::Sha512;
  const char* name = variant == Sha2State::Variant::Sha224 ? "sha224" :
      variant == Sha2State::Variant::Sha256 ? "sha256" :
      variant == Sha2State::Variant::Sha384 ? "sha384" : "sha512";
  const int64_t digest_size = variant == Sha2State::Variant::Sha224 ? 28 :
      variant == Sha2State::Variant::Sha256 ? 32 :
      variant == Sha2State::Variant::Sha384 ? 48 : 64;
  if (!object_set_attr(instance, "name", Value::string(name), error) ||
      !object_set_attr(instance, "digest_size", Value::int64(digest_size), error) ||
      !object_set_attr(instance, "block_size", Value::int64(wide ? 128 : 64), error)) {
    return false;
  }
  out = std::move(instance);
  return true;
}

bool sha2_copy(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 1) {
    error = "HASH.copy() expected no arguments";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  auto* state = sha2_state(args[0], error);
  if (state == nullptr) return false;
  const Value& klass = state->variant == Sha2State::Variant::Sha224 ? g_sha224_class :
      state->variant == Sha2State::Variant::Sha256 ? g_sha256_class :
      state->variant == Sha2State::Variant::Sha384 ? g_sha384_class : g_sha512_class;
  Value instance = Value::instance(klass);
  return initialize_sha2_instance(std::move(instance), state->data, state->variant, out, error);
}

bool sha2_constructor(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* user_data) {
  if (argc > 1) {
    error = "SHA-2 constructor expected at most one argument";
    runtime.raise_class_error("TypeError", error);
    return false;
  }
  const auto variant = static_cast<Sha2State::Variant>(reinterpret_cast<uintptr_t>(user_data));
  std::string data;
  if (argc == 1) {
    std::string_view view;
    if (!sha2_bytes_view(args[0], view, error)) {
      runtime.raise_class_error("TypeError", error);
      return false;
    }
    data.assign(view.data(), view.size());
  }
  const Value& klass = variant == Sha2State::Variant::Sha224 ? g_sha224_class :
      variant == Sha2State::Variant::Sha256 ? g_sha256_class :
      variant == Sha2State::Variant::Sha384 ? g_sha384_class : g_sha512_class;
  Value instance = Value::instance(klass);
  return initialize_sha2_instance(std::move(instance), std::move(data), variant, out, error);
}

bool sha2_constructor_kw(
    Runtime& runtime,
    const Value* args,
    uint32_t argc,
    const NativeKeywordArg* kwargs,
    uint32_t kwargc,
    Value& out,
    std::string& error,
    void* user_data) {
  for (uint32_t index = 0; index < kwargc; ++index) {
    const std::string name = kwargs[index].name == nullptr ? "" : kwargs[index].name;
    if (name != "usedforsecurity") {
      error = "SHA-2 constructor got an unexpected keyword argument '" + name + "'";
      runtime.raise_class_error("TypeError", error);
      return false;
    }
  }
  return sha2_constructor(runtime, args, argc, out, error, user_data);
}

bool sha224_constructor(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return sha2_constructor(runtime, args, argc, out, error,
      reinterpret_cast<void*>(static_cast<uintptr_t>(Sha2State::Variant::Sha224)));
}

bool sha256_constructor(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return sha2_constructor(runtime, args, argc, out, error,
      reinterpret_cast<void*>(static_cast<uintptr_t>(Sha2State::Variant::Sha256)));
}

bool sha384_constructor(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return sha2_constructor(runtime, args, argc, out, error,
      reinterpret_cast<void*>(static_cast<uintptr_t>(Sha2State::Variant::Sha384)));
}

bool sha512_constructor(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  return sha2_constructor(runtime, args, argc, out, error,
      reinterpret_cast<void*>(static_cast<uintptr_t>(Sha2State::Variant::Sha512)));
}

bool sha224_constructor_kw(
    Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs,
    uint32_t kwargc, Value& out, std::string& error, void*) {
  return sha2_constructor_kw(runtime, args, argc, kwargs, kwargc, out, error,
      reinterpret_cast<void*>(static_cast<uintptr_t>(Sha2State::Variant::Sha224)));
}

bool sha256_constructor_kw(
    Runtime& runtime, const Value* args, uint32_t argc, const NativeKeywordArg* kwargs,
    uint32_t kwargc, Value& out, std::string& error, void*) {
  return sha2_constructor_kw(runtime, args, argc, kwargs, kwargc, out, error,
      reinterpret_cast<void*>(static_cast<uintptr_t>(Sha2State::Variant::Sha256)));
}

bool sha384_constructor_kw(Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out, std::string& error, void*) {
  return sha2_constructor_kw(runtime, args, argc, kwargs, kwargc, out, error,
      reinterpret_cast<void*>(static_cast<uintptr_t>(Sha2State::Variant::Sha384)));
}

bool sha512_constructor_kw(Runtime& runtime, const Value* args, uint32_t argc,
    const NativeKeywordArg* kwargs, uint32_t kwargc, Value& out, std::string& error, void*) {
  return sha2_constructor_kw(runtime, args, argc, kwargs, kwargc, out, error,
      reinterpret_cast<void*>(static_cast<uintptr_t>(Sha2State::Variant::Sha512)));
}

Value make_sha2_class(Runtime& runtime, const char* name) {
  std::vector<std::pair<std::string, Value>> attrs;
  attrs.push_back({"__module__", Value::string("_sha2")});
  attrs.push_back({"__qualname__", Value::string(name)});
  attrs.push_back({"update", runtime.make_native_function("_sha2.HASH.update", sha2_update)});
  attrs.push_back({"digest", runtime.make_native_function("_sha2.HASH.digest", sha2_digest_method)});
  attrs.push_back({"hexdigest", runtime.make_native_function("_sha2.HASH.hexdigest", sha2_hexdigest)});
  attrs.push_back({"copy", runtime.make_native_function("_sha2.HASH.copy", sha2_copy)});
  return Value::class_object(name, std::move(attrs));
}

} // namespace

void register_sha2_module(Runtime& runtime) {
  g_sha224_class = make_sha2_class(runtime, "SHA224Type");
  g_sha256_class = make_sha2_class(runtime, "SHA256Type");
  g_sha384_class = make_sha2_class(runtime, "SHA384Type");
  g_sha512_class = make_sha2_class(runtime, "SHA512Type");
  NativeModuleBuilder builder(runtime, "_sha2");
  builder.value("SHA224Type", g_sha224_class)
      .value("SHA256Type", g_sha256_class)
      .value("SHA384Type", g_sha384_class)
      .value("SHA512Type", g_sha512_class)
      .function("sha224", sha224_constructor, nullptr, false, sha224_constructor_kw)
      .function("sha256", sha256_constructor, nullptr, false, sha256_constructor_kw)
      .function("sha384", sha384_constructor, nullptr, false, sha384_constructor_kw)
      .function("sha512", sha512_constructor, nullptr, false, sha512_constructor_kw);
  runtime.register_module("_sha2", builder.finish());
}

} // namespace xlang3
